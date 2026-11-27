#include "library/PlatformSync.hpp"
#include "ui/AppLog.hpp"

#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

static std::string ffmpegArg() {
    if (fs::exists("tools/ffmpeg.exe")) return "--ffmpeg-location \"tools\" ";
    if (fs::exists("ffmpeg.exe")) return "--ffmpeg-location \".\" ";
    return "";
}

static std::string currentTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmNow{};
    localtime_s(&tmNow, &now);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmNow);
    return std::string(buf);
}

static std::string findYtDlpPath() {
    if (fs::exists("tools/yt-dlp.exe")) return "tools\\yt-dlp.exe";
    if (fs::exists("yt-dlp.exe")) return "yt-dlp.exe";
    return "yt-dlp.exe";
}

static std::string strOr(const json& obj, const char* key, const std::string& fallback = "") {
    if (obj.contains(key) && obj[key].is_string()) return obj[key].get<std::string>();
    return fallback;
}

static double numOr(const json& obj, const char* key, double fallback = 0.0) {
    if (obj.contains(key) && obj[key].is_number()) return obj[key].get<double>();
    return fallback;
}

static Track parseTrackJson(const json& e, Platform plat, bool preferVid) {
    Track t;
    std::string rawId = strOr(e, "id");
    std::string rawTitle = normalizeUtf8Display(strOr(e, "title", "Untitled"));
    extractHashtags(rawTitle, t.title, t.tags);
    t.id = LibraryManager::sanitizeId(rawId.empty() ? t.title : rawId);

    std::string artist = strOr(e, "uploader");
    if (artist.empty()) artist = strOr(e, "channel");
    if (artist.empty()) artist = strOr(e, "creator", platformName(plat));
    t.artist = normalizeUtf8Display(artist);
    t.platform = plat;
    t.durationSec = numOr(e, "duration", 0.0);
    t.isVideo = preferVid || (plat == Platform::YouTube || plat == Platform::TikTok);

    std::string url = strOr(e, "webpage_url");
    if (url.empty()) url = strOr(e, "url");
    if (url.rfind("http", 0) != 0 && !rawId.empty() &&
        (plat == Platform::YouTube || plat == Platform::YouTubeMusic)) {
        url = "https://www.youtube.com/watch?v=" + rawId;
    }
    t.sourceUrl = url;

    t.thumbnailUrl = strOr(e, "thumbnail");
    if (t.thumbnailUrl.empty() && e.contains("thumbnails") && e["thumbnails"].is_array() && !e["thumbnails"].empty()) {
        t.thumbnailUrl = strOr(e["thumbnails"].back(), "url");
    }
    if (!rawId.empty() && (plat == Platform::YouTube || plat == Platform::YouTubeMusic)) {
        // Always prefer true HD 1280x720 maxresdefault.jpg over flat-playlist 336x188 thumbnails
        t.thumbnailUrl = "https://i.ytimg.com/vi/" + rawId + "/maxresdefault.jpg";
    } else {
        size_t webpPos = t.thumbnailUrl.find("/vi_webp/");
        if (webpPos != std::string::npos) {
            t.thumbnailUrl.replace(webpPos, 9, "/vi/");
            size_t extPos = t.thumbnailUrl.find(".webp");
            if (extPos != std::string::npos) t.thumbnailUrl.replace(extPos, 5, ".jpg");
        }
    }
    return t;
}

PlatformSync::PlatformSync(LibraryManager& library) : m_library(library) {}

PlatformSync::~PlatformSync() {
    stop();
}

void PlatformSync::start() {
    if (m_running.exchange(true)) return;
    fs::create_directories("cache/stream");
    fs::create_directories("cache/thumbnails");

    if (!m_jobHandle) {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
            m_jobHandle = job;
        }
    }

    m_fastWorker = std::thread(&PlatformSync::fastWorkerLoop, this);
    m_syncWorker = std::thread(&PlatformSync::syncWorkerLoop, this);
    m_dlWorker = std::thread(&PlatformSync::dlWorkerLoop, this);
    refreshNetworkStatusAsync();
}

void PlatformSync::stop() {
    if (!m_running.exchange(false)) return;
    m_cancelDownloads.store(true);
    m_fastQueue.clear();
    m_syncQueue.clear();
    m_dlQueue.clear();
    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        if (m_jobHandle) {
            TerminateJobObject(static_cast<HANDLE>(m_jobHandle), 1);
            CloseHandle(static_cast<HANDLE>(m_jobHandle));
            m_jobHandle = nullptr;
        }
        for (void* p : m_activeProcesses) {
            if (p) TerminateProcess(static_cast<HANDLE>(p), 1);
        }
    }
    m_fastQueue.cv.notify_all();
    m_syncQueue.cv.notify_all();
    m_dlQueue.cv.notify_all();
    if (m_fastWorker.joinable()) m_fastWorker.join();
    if (m_syncWorker.joinable()) m_syncWorker.join();
    if (m_dlWorker.joinable()) m_dlWorker.join();
}

void PlatformSync::setStatus(const std::string& msg) {
    AppLog::instance().add("[Sync] " + msg);
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMsg = msg;
}

std::string PlatformSync::statusMessage() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_statusMsg;
}

void PlatformSync::refreshNetworkStatusAsync() {
    m_fastQueue.push({TaskType::CheckNetwork, "", "", "", Platform::YouTubeMusic, false, 12, nullptr, nullptr});
}

void PlatformSync::syncPlaylistAsync(const std::string& playlistId) {
    if (m_library.offlineMode()) {
        setStatus("Offline Mode active - sync skipped");
        return;
    }
    m_syncQueue.push({TaskType::SyncPlaylist, playlistId, "", "", Platform::YouTubeMusic, false, 12, nullptr, nullptr});
}

void PlatformSync::syncAllAutoPlaylistsAsync() {
    auto st = m_library.settings();
    bool needCleanup = false;
    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    if (nowSec - st.lastCacheCleanupEpoch >= 86400) {
        st.lastCacheCleanupEpoch = nowSec;
        m_library.updateSettings(st);
        needCleanup = true;
    }
    if (!m_library.offlineMode()) {
        auto lists = m_library.playlistsCopy();
        for (const auto& p : lists) {
            if (p.autoSync && !p.sourceUrl.empty() && p.platform != Platform::Local) {
                syncPlaylistAsync(p.id);
            }
            for (const auto& t : p.tracks) {
                if (t.downloadState == DownloadState::Queued && !t.sourceUrl.empty()) {
                    queueDirectDownload(t, p.id, p.preferVideo);
                }
            }
        }
        hydrateMissingThumbnailsAsync();
    }
    if (needCleanup) {
        cleanupOrphanCacheAsync();
    }
}

void PlatformSync::cleanupOrphanCacheAsync() {
    m_syncQueue.push({TaskType::CleanupCache, "", "", "", Platform::YouTubeMusic, false, 12, nullptr, nullptr});
}

void PlatformSync::hydrateMissingThumbnailsAsync() {
    if (m_library.offlineMode()) return;
    m_syncQueue.push({TaskType::HydrateThumbnails, "", "", "", Platform::YouTubeMusic, false, 12, nullptr, nullptr});
}

void PlatformSync::reinstallTrack(const std::string& playlistId, const Track& track, bool asVideo) {
    m_library.clearTrackLocalMedia(playlistId, track.id);
    for (const char* ext : { "mp4", "m4a", "mp3", "webm" }) {
        std::string cf = "cache/stream/" + LibraryManager::sanitizeId(track.id) + "." + ext;
        if (fs::exists(cf)) {
            std::error_code ec;
            fs::remove(cf, ec);
        }
    }
    m_library.save();
    queueDirectDownload(track, playlistId, asVideo);
}

void PlatformSync::queueDownload(const std::string& playlistId, const std::string& trackId, bool asVideo) {
    if (m_library.offlineMode()) return;
    auto tr = m_library.getTrack(playlistId, trackId);
    if (!tr) return;
    queueDirectDownload(*tr, playlistId, asVideo);
}

void PlatformSync::queueDirectDownload(const Track& track, const std::string& playlistId, bool asVideo) {
    if (m_library.offlineMode() || track.sourceUrl.empty()) return;
    m_cancelDownloads.store(false);
    m_library.updateTrackDownload(playlistId, track.id, DownloadState::Queued);

    {
        std::lock_guard<std::mutex> lock(m_dlMutex);
        auto it = std::find_if(m_downloads.begin(), m_downloads.end(), [&](const DownloadItem& d) {
            return d.trackId == track.id;
        });
        DownloadItem item{ playlistId, track.id, track.title, track.artist, track.sourceUrl,
                           track.platform, asVideo, DownloadState::Queued, 0.0f, "", "", "" };
        if (it == m_downloads.end()) m_downloads.push_back(std::move(item));
        else *it = std::move(item);
    }

    m_dlQueue.push({TaskType::DownloadTrack, playlistId, track.id, track.sourceUrl, track.platform, asVideo, 12, nullptr, nullptr});
}

void PlatformSync::queuePlaylistDownload(const std::string& playlistId, bool asVideo) {
    auto pl = m_library.getPlaylist(playlistId);
    if (!pl) return;
    for (const auto& t : pl->tracks) {
        if (t.downloadState != DownloadState::Downloaded && !t.sourceUrl.empty()) {
            queueDirectDownload(t, playlistId, asVideo);
        }
    }
}

void PlatformSync::retryFailedDownloads() {
    if (m_library.offlineMode()) return;
    std::vector<DownloadItem> toRetry;
    {
        std::lock_guard<std::mutex> lock(m_dlMutex);
        for (auto& d : m_downloads) {
            if (d.state == DownloadState::Failed && !d.sourceUrl.empty()) {
                d.state = DownloadState::Queued;
                d.progressPct = 0.0f;
                d.errorMsg.clear();
                toRetry.push_back(d);
            }
        }
    }
    for (const auto& d : toRetry) {
        m_dlQueue.push({TaskType::DownloadTrack, d.playlistId, d.trackId, d.sourceUrl, d.platform, d.asVideo, 12, nullptr, nullptr});
    }
}

void PlatformSync::cancelAllDownloads() {
    m_cancelDownloads.store(true);
    m_dlQueue.clear();
    std::lock_guard<std::mutex> lock(m_dlMutex);
    for (auto& d : m_downloads) {
        if (d.state == DownloadState::Queued) d.state = DownloadState::Failed;
    }
    setStatus("Downloads cancelled");
}

std::vector<DownloadItem> PlatformSync::downloadsSnapshot() const {
    std::lock_guard<std::mutex> lock(m_dlMutex);
    return m_downloads;
}

void PlatformSync::clearCompletedDownloads() {
    std::lock_guard<std::mutex> lock(m_dlMutex);
    m_downloads.erase(
        std::remove_if(m_downloads.begin(), m_downloads.end(), [](const DownloadItem& d) {
            return d.state == DownloadState::Downloaded || d.state == DownloadState::Failed;
        }),
        m_downloads.end()
    );
}

void PlatformSync::updateDownloadItem(const std::string& trackId, const std::function<void(DownloadItem&)>& fn) {
    std::lock_guard<std::mutex> lock(m_dlMutex);
    for (auto& d : m_downloads) {
        if (d.trackId == trackId) {
            fn(d);
            break;
        }
    }
}

void PlatformSync::resolveDirectStreamAsync(const Track& track, StreamResolvedCb cb) {
    if (m_library.offlineMode()) {
        if (cb) cb(false, "", "Offline Mode is enabled");
        return;
    }
    uint64_t seq = cb ? ++m_latestStreamSeq : 0;
    m_fastQueue.push({TaskType::ResolveStream, "", track.id, track.sourceUrl, track.platform, track.isVideo, 12, std::move(cb), nullptr, seq});
}

void PlatformSync::searchPlatformAsync(Platform platform, const std::string& queryOrUrl, int limit, SearchCb cb) {
    if (m_library.offlineMode()) {
        if (cb) cb(false, {}, "Offline Mode is enabled");
        return;
    }
    m_fastQueue.push({TaskType::SearchPlatform, "", "", queryOrUrl, platform, false, limit, nullptr, std::move(cb)});
}

bool PlatformSync::checkNetworkWinHttp() {
    HINTERNET hSession = WinHttpOpen(L"StreamPulse/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 2000, 2000, 2000, 2000);
    HINTERNET hConnect = WinHttpConnect(hSession, L"www.youtube.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    bool ok = false;
    if (hConnect) {
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"HEAD", L"/", nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
        if (hReq) {
            if (WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(hReq, nullptr)) {
                ok = true;
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}

bool PlatformSync::downloadFileWinHttp(const std::string& url, const std::string& destPath) {
    if (url.empty()) return false;
    std::wstring wUrl = toWide(url);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc)) return false;

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 4000, 4000, 5000, 5000);
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, nullptr, nullptr, flags);
    bool success = false;

    if (hReq && WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
        if (status >= 200 && status < 300) {
            fs::create_directories(fs::path(destPath).parent_path());
            std::ofstream out(destPath, std::ios::binary);
            if (out.is_open()) {
                BYTE buffer[8192];
                DWORD bytesRead = 0;
                size_t totalBytes = 0;
                while (m_running.load() && WinHttpReadData(hReq, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                    out.write(reinterpret_cast<const char*>(buffer), bytesRead);
                    totalBytes += bytesRead;
                }
                success = (m_running.load() && totalBytes > 0);
            }
        }
    }

    if (hReq) WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}

std::string PlatformSync::runYtDlp(const std::string& args, int& exitCode,
                                   const std::function<void(const std::string& line)>& onLine,
                                   int timeoutMs) {
    exitCode = -1;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return "";
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + findYtDlpPath() + "\" " + args;
    std::wstring wCmd = toWide(cmd);

    BOOL ok = CreateProcessW(
        nullptr, wCmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi
    );
    CloseHandle(writePipe);

    if (!ok) {
        CloseHandle(readPipe);
        return "";
    }

    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        if (m_jobHandle) AssignProcessToJobObject(static_cast<HANDLE>(m_jobHandle), pi.hProcess);
        m_activeProcesses.push_back(pi.hProcess);
    }

    std::string output;
    std::string lineBuf;
    char buf[2048];
    DWORD bytesRead = 0;
    auto startTick = std::chrono::steady_clock::now();
    bool timedOut = false;
    while (m_running.load()) {
        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTick).count();
            if (elapsed > timeoutMs) {
                timedOut = true;
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 25) == WAIT_OBJECT_0) {
                if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            }
            continue;
        }
        if (!ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) || bytesRead == 0) break;
        output.append(buf, bytesRead);
        if (onLine) {
            lineBuf.append(buf, bytesRead);
            size_t pos = 0;
            while ((pos = lineBuf.find_first_of("\r\n")) != std::string::npos) {
                std::string l = lineBuf.substr(0, pos);
                lineBuf.erase(0, pos + 1);
                if (!l.empty()) onLine(l);
            }
        }
    }
    CloseHandle(readPipe);

    if (m_running.load() && !timedOut) WaitForSingleObject(pi.hProcess, 1500);
    {
        std::lock_guard<std::mutex> lock(m_procMutex);
        m_activeProcesses.erase(
            std::remove(m_activeProcesses.begin(), m_activeProcesses.end(), static_cast<void*>(pi.hProcess)),
            m_activeProcesses.end()
        );
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    exitCode = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return output;
}

void PlatformSync::fastWorkerLoop() {
    while (m_running.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_fastQueue.mtx);
            m_fastQueue.cv.wait(lock, [&]() { return !m_running.load() || !m_fastQueue.q.empty(); });
            if (!m_running.load()) break;
            task = std::move(m_fastQueue.q.front());
            m_fastQueue.q.pop();
        }

        ++m_activeTasks;

        if (task.type == TaskType::CheckNetwork) {
            bool online = checkNetworkWinHttp();
            m_networkAvailable.store(online);
            setStatus(online ? "Online - Ready" : "No network (Offline ready)");
        } else if (task.type == TaskType::ResolveStream) {
            if (task.seq != 0 && task.seq != m_latestStreamSeq.load()) {
                --m_activeTasks;
                continue; // Skip stale play request
            }
            setStatus("Loading stream...");
            std::string cacheKey = task.trackId + (task.asVideo ? "_v" : "_a");
            {
                std::lock_guard<std::mutex> lock(m_streamCacheMutex);
                auto it = m_streamCache.find(cacheKey);
                if (it != m_streamCache.end() && !it->second.empty()) {
                    setStatus("Playing (cached stream)");
                    if (task.streamCb) task.streamCb(true, it->second, "");
                    --m_activeTasks;
                    continue;
                }
            }

            bool isTikTok = (task.url.find("tiktok.com") != std::string::npos);
            bool isTikTokPhoto = (isTikTok && task.url.find("/photo/") != std::string::npos);

            if (isTikTok) {
                std::string safeId = LibraryManager::sanitizeId(task.trackId.empty() ? "tt" : task.trackId);
                std::string foundFile;
                for (const char* ext : { "mp4", "m4a", "mp3", "webm" }) {
                    std::string cand = "cache/stream/" + safeId + "." + ext;
                    if (fs::exists(cand) && fs::file_size(cand) > 1024) { foundFile = cand; break; }
                }
                if (foundFile.empty()) {
                    std::string ff = ffmpegArg();
                    std::string outTmpl = "cache/stream/" + safeId + ".%(ext)s";
                    int code = -1;
                    if (isTikTokPhoto) {
                        runYtDlp(ff + "--force-overwrites --no-part --no-warnings -f \"bestaudio/best\" " +
                                 (ff.empty() ? "" : "-x --audio-format m4a ") +
                                 "-o \"" + outTmpl + "\" \"" + task.url + "\"", code, nullptr, 15000);
                    } else {
                        runYtDlp(ff + "--force-overwrites --no-part --no-warnings -f \"best[ext=mp4]/best\" -o \"" +
                                 outTmpl + "\" \"" + task.url + "\"", code, nullptr, 15000);
                        if (code != 0) {
                            runYtDlp(ff + "--force-overwrites --no-part --no-warnings -f \"best\" -o \"" +
                                     outTmpl + "\" \"" + task.url + "\"", code, nullptr, 15000);
                        }
                    }
                    for (const char* ext : { "mp4", "m4a", "mp3", "webm" }) {
                        std::string cand = "cache/stream/" + safeId + "." + ext;
                        if (fs::exists(cand) && fs::file_size(cand) > 1024) { foundFile = cand; break; }
                    }
                }
                if (!foundFile.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(m_streamCacheMutex);
                        m_streamCache[cacheKey] = foundFile;
                    }
                    setStatus("Playing TikTok (Ad-Free)");
                    if (task.streamCb) task.streamCb(true, foundFile, "");
                } else {
                    setStatus("TikTok stream failed");
                    if (task.streamCb) task.streamCb(false, "", "Could not load TikTok media");
                }
            } else if (!task.asVideo) {
                std::string cacheFile = "cache/stream/" + LibraryManager::sanitizeId(task.trackId.empty() ? "yt" : task.trackId) + ".m4a";
                if (!fs::exists(cacheFile)) {
                    std::string ff = ffmpegArg();
                    int code = -1;
                    runYtDlp(ff + "--no-part --no-warnings -f \"140/bestaudio[ext=m4a]/bestaudio/18/best\" " +
                             (ff.empty() ? "" : "-x --audio-format m4a ") +
                             "-o \"" + cacheFile + "\" \"" + task.url + "\"", code, nullptr, 15000);
                }
                if (fs::exists(cacheFile) && fs::file_size(cacheFile) > 1024) {
                    {
                        std::lock_guard<std::mutex> lock(m_streamCacheMutex);
                        m_streamCache[cacheKey] = cacheFile;
                    }
                    setStatus("Playing Direct (Ad-Free)");
                    if (task.streamCb) task.streamCb(true, cacheFile, "");
                } else {
                    setStatus("Stream failed");
                    if (task.streamCb) task.streamCb(false, "", "Timeout ou flux indisponible");
                }
            } else {
                // Single fast call for 720p/360p MP4 video stream cache
                std::string safeId = LibraryManager::sanitizeId(task.trackId.empty() ? "ytv" : task.trackId);
                std::string cacheFile = "cache/stream/" + safeId + ".mp4";
                if (!fs::exists(cacheFile)) {
                    std::string ff = ffmpegArg();
                    int code = -1;
                    std::string vFmt = "22/18/best[ext=mp4][height<=720]/bv*[ext=mp4][height<=720]+ba[ext=m4a]/best[height<=720]/best";
                    runYtDlp(ff + "--force-overwrites --no-part --no-warnings -f \"" + vFmt + "\" " +
                             (ff.empty() ? "" : "--merge-output-format mp4 ") +
                             "-o \"" + cacheFile + "\" \"" + task.url + "\"", code, nullptr, 20000);
                }
                if (fs::exists(cacheFile) && fs::file_size(cacheFile) > 1024) {
                    {
                        std::lock_guard<std::mutex> lock(m_streamCacheMutex);
                        m_streamCache[cacheKey] = cacheFile;
                    }
                    setStatus("Playing video (Ad-Free)");
                    if (task.streamCb) task.streamCb(true, cacheFile, "");
                } else {
                    setStatus("Video stream failed");
                    if (task.streamCb) task.streamCb(false, "", "Timeout ou vidéo indisponible");
                }
            }
        } else if (task.type == TaskType::SearchPlatform) {
            setStatus("Searching " + std::string(platformName(task.platform)) + "...");
            std::string target = task.url;
            if (target.rfind("http", 0) != 0) {
                std::string n = std::to_string(task.searchLimit > 0 ? task.searchLimit : 12);
                if (task.platform == Platform::TikTok) {
                    target = "ytsearch" + n + ":" + target + " tiktok";
                } else {
                    target = "ytsearch" + n + ":" + target;
                }
            }
            int code = -1;
            std::string out = runYtDlp("--flat-playlist -J --no-warnings \"" + target + "\"", code);
            std::vector<Track> results;
            size_t jsonStart = out.find('{');
            if (code == 0 && jsonStart != std::string::npos) {
                try {
                    json j = json::parse(out.substr(jsonStart));
                    bool prefVid = (task.platform == Platform::TikTok || task.platform == Platform::YouTube);
                    auto processOne = [&](const json& e) {
                        Track t = parseTrackJson(e, task.platform, prefVid);
                        std::string thumbPath = "cache/thumbnails/" + t.id + ".jpg";
                        if (fs::exists(thumbPath)) {
                            t.localThumbnailPath = thumbPath;
                        } else if (!t.thumbnailUrl.empty()) {
                            bool ok = downloadFileWinHttp(t.thumbnailUrl, thumbPath);
                            if (!ok && (t.platform == Platform::YouTube || t.platform == Platform::YouTubeMusic)) {
                                ok = downloadFileWinHttp("https://i.ytimg.com/vi/" + t.id + "/hqdefault.jpg", thumbPath);
                            }
                            if (ok) t.localThumbnailPath = thumbPath;
                        }
                        results.push_back(std::move(t));
                    };
                    if (j.contains("entries") && j["entries"].is_array()) {
                        for (const auto& e : j["entries"]) processOne(e);
                    } else {
                        processOne(j);
                    }
                } catch (...) {}
            }
            setStatus("Found " + std::to_string(results.size()) + " results");
            if (task.searchCb) task.searchCb(!results.empty(), results, results.empty() ? "No results found" : "");
        }

        --m_activeTasks;
    }
}

void PlatformSync::syncWorkerLoop() {
    while (m_running.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_syncQueue.mtx);
            m_syncQueue.cv.wait(lock, [&]() { return !m_running.load() || !m_syncQueue.q.empty(); });
            if (!m_running.load()) break;
            task = std::move(m_syncQueue.q.front());
            m_syncQueue.q.pop();
        }

        ++m_activeTasks;
        if (task.type == TaskType::CleanupCache) {
            auto lists = m_library.playlistsCopy();
            std::vector<std::string> validIds;
            for (const auto& pl : lists) {
                for (const auto& t : pl.tracks) validIds.push_back(t.id);
            }
            std::sort(validIds.begin(), validIds.end());
            int removed = 0;
            for (const char* dir : { "cache/thumbnails", "cache/stream", "cache/lyrics" }) {
                if (!fs::exists(dir)) continue;
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (!entry.is_regular_file()) continue;
                    std::string stem = entry.path().stem().string();
                    if (!std::binary_search(validIds.begin(), validIds.end(), stem)) {
                        std::error_code ec;
                        if (fs::remove(entry.path(), ec)) ++removed;
                    }
                }
            }
            if (removed > 0) setStatus("Cleaned " + std::to_string(removed) + " unused cache files");
            --m_activeTasks;
            continue;
        }

        if (!m_library.offlineMode()) {
            if (task.type == TaskType::HydrateThumbnails) {
                auto lists = m_library.playlistsCopy();
                for (const auto& pl : lists) {
                    for (const auto& t : pl.tracks) {
                        if (!m_running.load()) break;
                        bool missing = t.localThumbnailPath.empty() || !fs::exists(t.localThumbnailPath);
                        if (!missing && (t.platform == Platform::YouTube || t.platform == Platform::YouTubeMusic)) {
                            std::error_code ec;
                            auto fsz = fs::file_size(t.localThumbnailPath, ec);
                            if (!ec && fsz < 25000) missing = true; // Upgrade low-res 336x188 thumbnails to HD
                            std::ifstream chk(t.localThumbnailPath, std::ios::binary);
                            char hdr[4]{};
                            if (chk.read(hdr, 4) && std::memcmp(hdr, "RIFF", 4) == 0) missing = true;
                        }
                        if (!missing) continue;
                        std::string url = t.thumbnailUrl;
                        if ((t.platform == Platform::YouTube || t.platform == Platform::YouTubeMusic) && !t.id.empty()) {
                            url = "https://i.ytimg.com/vi/" + t.id + "/maxresdefault.jpg";
                        }
                        if (!url.empty()) {
                            std::string thumbPath = "cache/thumbnails/" + t.id + ".jpg";
                            bool dlOk = downloadFileWinHttp(url, thumbPath);
                            if (!dlOk && (t.platform == Platform::YouTube || t.platform == Platform::YouTubeMusic)) {
                                dlOk = downloadFileWinHttp("https://i.ytimg.com/vi/" + t.id + "/sddefault.jpg", thumbPath) ||
                                       downloadFileWinHttp("https://i.ytimg.com/vi/" + t.id + "/hqdefault.jpg", thumbPath);
                            }
                            if (dlOk) {
                                m_library.updateTrackThumbnail(pl.id, t.id, thumbPath);
                            }
                        }
                    }
                }
                m_library.save();
                --m_activeTasks;
                continue;
            }
            auto pl = m_library.getPlaylist(task.playlistId);
            if (pl && !pl->sourceUrl.empty()) {
                setStatus("Syncing: " + pl->name + "...");
                int code = -1;
                std::string out = runYtDlp("--ignore-errors --yes-playlist --flat-playlist -J --no-warnings \"" + pl->sourceUrl + "\"", code);
                size_t jsonStart = out.find('{');
                if (jsonStart != std::string::npos && (code == 0 || out.find("\"entries\"") != std::string::npos)) {
                    try {
                        json j = json::parse(out.substr(jsonStart));
                        std::string resolvedName = strOr(j, "title", pl->name);
                        std::vector<Track> fetched;
                        auto isInvalidYt = [](const Track& t) {
                            return (t.platform == Platform::YouTube || t.platform == Platform::YouTubeMusic) &&
                                   (t.title.empty() || t.title == "Untitled" ||
                                    t.title == "[Private video]" || t.title == "[Deleted video]");
                        };
                        if (j.contains("entries") && j["entries"].is_array()) {
                            for (const auto& e : j["entries"]) {
                                Track t = parseTrackJson(e, pl->platform, pl->preferVideo);
                                if (!isInvalidYt(t)) fetched.push_back(std::move(t));
                            }
                        } else {
                            Track t = parseTrackJson(j, pl->platform, pl->preferVideo);
                            if (!isInvalidYt(t)) fetched.push_back(std::move(t));
                        }

                        m_library.mergeSyncedPlaylist(pl->id, resolvedName, fetched, currentTimestamp());
                        m_library.save();

                        auto updatedPl = m_library.getPlaylist(pl->id);
                        if (updatedPl) {
                            for (const auto& t : updatedPl->tracks) {
                                if (!m_running.load()) break;
                                if (t.localThumbnailPath.empty() && !t.thumbnailUrl.empty()) {
                                    std::string thumbPath = "cache/thumbnails/" + t.id + ".jpg";
                                    if (fs::exists(thumbPath) || downloadFileWinHttp(t.thumbnailUrl, thumbPath)) {
                                        m_library.updateTrackThumbnail(pl->id, t.id, thumbPath);
                                    }
                                }
                                if (updatedPl->autoDownload && t.downloadState == DownloadState::None && !t.sourceUrl.empty()) {
                                    queueDirectDownload(t, pl->id, updatedPl->preferVideo);
                                }
                            }
                            m_library.save();
                        }
                        setStatus("Synced " + std::to_string(fetched.size()) + " items in " + resolvedName);
                    } catch (...) {
                        setStatus("Failed to parse playlist metadata");
                    }
                } else {
                    setStatus("Sync failed (check URL or network)");
                }
            }
        }
        --m_activeTasks;
    }
}

void PlatformSync::dlWorkerLoop() {
    while (m_running.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_dlQueue.mtx);
            m_dlQueue.cv.wait(lock, [&]() { return !m_running.load() || !m_dlQueue.q.empty(); });
            if (!m_running.load()) break;
            task = std::move(m_dlQueue.q.front());
            m_dlQueue.q.pop();
        }

        if (m_cancelDownloads.load() || m_library.offlineMode() || task.url.empty()) continue;

        ++m_activeTasks;
        m_library.updateTrackDownload(task.playlistId, task.trackId, DownloadState::Downloading);
        updateDownloadItem(task.trackId, [](DownloadItem& d) {
            d.state = DownloadState::Downloading;
            d.progressPct = 0.0f;
        });

        std::string dir = "downloads/" + LibraryManager::sanitizeId(platformName(task.platform)) + "/" +
                          (task.playlistId.empty() ? "direct" : task.playlistId);
        fs::create_directories(dir);
        std::string ext = task.asVideo ? "mp4" : "m4a";
        std::string outPath = dir + "/" + task.trackId + "." + ext;
        std::string ff = ffmpegArg();
        std::string fmt = task.asVideo
            ? "bv*[ext=mp4]+ba[ext=m4a]/18/b[ext=mp4]/best"
            : "bestaudio[ext=m4a]/bestaudio/18/best";
        std::string postArgs = task.asVideo ? "--merge-output-format mp4 " : "-x --audio-format m4a ";

        int code = -1;
        std::string lastErrLine;
        auto parseLine = [&](const std::string& line) {
            if (line.find("ERROR:") != std::string::npos) {
                lastErrLine = line;
            }
            if (line.find("[download]") != std::string::npos) {
                float pct = 0.0f;
                size_t pctPos = line.find('%');
                if (pctPos != std::string::npos) {
                    size_t start = line.rfind(' ', pctPos);
                    if (start != std::string::npos) {
                        try { pct = std::stof(line.substr(start + 1, pctPos - start - 1)); } catch (...) {}
                    }
                }
                std::string speed, eta;
                size_t atPos = line.find(" at ");
                if (atPos != std::string::npos) {
                    size_t etaPos = line.find(" ETA ", atPos);
                    if (etaPos != std::string::npos) {
                        speed = line.substr(atPos + 4, etaPos - (atPos + 4));
                        eta = line.substr(etaPos + 5);
                    }
                }
                updateDownloadItem(task.trackId, [&](DownloadItem& d) {
                    d.progressPct = pct;
                    if (!speed.empty()) d.speed = speed;
                    if (!eta.empty()) d.eta = eta;
                });
            }
        };

        std::string args = ff + "--retries 3 --fragment-retries 3 --sleep-requests 0.5 --force-overwrites --newline --no-part --no-warnings -f \"" +
                           fmt + "\" " + (ff.empty() ? "" : postArgs) + "-o \"" + outPath + "\" \"" + task.url + "\"";
        runYtDlp(args, code, parseLine);

        // Fallback attempt if primary format failed (e.g., Video unavailable -> search by title + artist)
        if ((code != 0 || !fs::exists(outPath)) && !m_cancelDownloads.load()) {
            std::string fallbackTarget = task.url;
            auto trInfo = m_library.getTrack(task.playlistId, task.trackId);
            if (lastErrLine.find("unavailable") != std::string::npos && trInfo && !trInfo->title.empty()) {
                fallbackTarget = "ytsearch1:" + trInfo->title + " " + trInfo->artist;
            }
            std::string fbArgs = ff + "--retries 2 --force-overwrites --newline --no-part --no-warnings -f \"18/bestaudio[ext=m4a]/best\" " +
                                 (task.asVideo ? "" : (ff.empty() ? "" : "-x --audio-format m4a ")) +
                                 "-o \"" + outPath + "\" \"" + fallbackTarget + "\"";
            runYtDlp(fbArgs, code, parseLine);
        }

        if (code == 0 && fs::exists(outPath)) {
            m_library.updateTrackDownload(task.playlistId, task.trackId, DownloadState::Downloaded, outPath);
            m_library.save();
            updateDownloadItem(task.trackId, [&](DownloadItem& d) {
                d.state = DownloadState::Downloaded;
                d.progressPct = 100.0f;
                d.outputPath = outPath;
                d.errorMsg.clear();
            });
            setStatus("Download complete");
        } else {
            m_library.updateTrackDownload(task.playlistId, task.trackId, DownloadState::Failed);
            updateDownloadItem(task.trackId, [&](DownloadItem& d) {
                d.state = DownloadState::Failed;
                d.errorMsg = lastErrLine;
            });
            setStatus("Download failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        --m_activeTasks;
    }
}
