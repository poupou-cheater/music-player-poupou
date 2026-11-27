#include "library/LyricsManager.hpp"

#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::wstring toWideStr(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

std::string LyricsManager::urlEncode(const std::string& s) {
    std::string out;
    char hex[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out.append(hex);
        }
    }
    return out;
}

std::string LyricsManager::httpGet(const std::string& host, const std::string& path) {
    HINTERNET hSession = WinHttpOpen(L"StreamPulse/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) return "";
    WinHttpSetTimeouts(hSession, 4000, 4000, 5000, 5000);

    std::wstring wHost = toWideStr(host);
    std::wstring wPath = toWideStr(path);
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE);
    std::string body;
    if (hReq && WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &statusCode, &size, nullptr);
        if (statusCode == 200) {
            char buf[8192];
            DWORD read = 0;
            while (WinHttpReadData(hReq, buf, sizeof(buf), &read) && read > 0) {
                body.append(buf, read);
            }
        }
    }

    if (hReq) WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
}

void LyricsManager::parseLrc(const std::string& lrc, bool synced) {
    std::vector<LyricLine> parsed;
    std::istringstream iss(lrc);
    std::string line;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (synced && line.size() > 2 && line[0] == '[') {
            size_t closePos = line.find(']');
            if (closePos != std::string::npos) {
                std::string ts = line.substr(1, closePos - 1);
                int min = 0;
                double sec = 0.0;
                if (std::sscanf(ts.c_str(), "%d:%lf", &min, &sec) == 2) {
                    std::string text = line.substr(closePos + 1);
                    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
                    parsed.push_back({ min * 60.0 + sec, text.empty() ? "..." : text });
                    continue;
                }
            }
        }
        if (!synced) {
            parsed.push_back({ -1.0, line });
        }
    }

    std::lock_guard<std::mutex> lock(m_mtx);
    m_lines = std::move(parsed);
    m_synced = synced && !m_lines.empty();
    m_status = m_lines.empty() ? "No lyrics found" : (m_synced ? "Synced LRC" : "Plain lyrics");
}

void LyricsManager::fetchForTrackAsync(const std::string& trackId, const std::string& title,
                                       const std::string& artist, double durationSec, bool offlineMode) {
    uint64_t seq = ++m_requestSeq;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_currentTrackId = trackId;
        m_lines.clear();
        m_synced = false;
        m_status = "Loading lyrics...";
    }

    std::string cachePath = "cache/lyrics/" + trackId + ".lrc";
    if (fs::exists(cachePath)) {
        std::ifstream f(cachePath);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
        if (!content.empty()) {
            bool looksSynced = content.find('[') != std::string::npos && content.find(':') != std::string::npos;
            parseLrc(content, looksSynced);
            return;
        }
    }

    if (offlineMode) {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_status = "Offline Mode (no cached lyrics)";
        return;
    }

    m_loading.store(true);
    std::thread([this, seq, trackId, title, artist, durationSec, cachePath]() {
        // Clean title from common YouTube/TikTok suffixes and brackets
        auto stripBrackets = [](std::string s) {
            std::string out;
            int depthParen = 0, depthBrack = 0;
            for (char c : s) {
                if (c == '(') { ++depthParen; continue; }
                if (c == ')') { if (depthParen > 0) --depthParen; continue; }
                if (c == '[') { ++depthBrack; continue; }
                if (c == ']') { if (depthBrack > 0) --depthBrack; continue; }
                if (depthParen == 0 && depthBrack == 0) out.push_back(c);
            }
            return out;
        };
        std::string cleanTitle = stripBrackets(title);
        std::string cleanArtist = (artist.find('?') != std::string::npos || artist == "YouTube Music" || artist == "TikTok")
            ? "" : stripBrackets(artist);

        std::string resp = httpGet("lrclib.net", "/api/search?q=" + urlEncode(cleanTitle + " " + cleanArtist));
        if ((resp.empty() || resp == "[]") && !cleanArtist.empty()) {
            resp = httpGet("lrclib.net", "/api/search?q=" + urlEncode(cleanTitle));
        }
        if (m_requestSeq.load() != seq) {
            m_loading.store(false);
            return;
        }

        std::string bestLrc;
        bool isSyncedLrc = false;

        if (!resp.empty()) {
            try {
                json arr = json::parse(resp);
                if (arr.is_array() && !arr.empty()) {
                    const json* chosen = &arr[0];
                    for (const auto& item : arr) {
                        std::string sl = item.value("syncedLyrics", "");
                        if (!sl.empty()) {
                            double d = item.value("duration", 0.0);
                            if (durationSec <= 0.0 || std::abs(d - durationSec) < 12.0) {
                                chosen = &item;
                                break;
                            }
                            if (chosen->value("syncedLyrics", "").empty()) chosen = &item;
                        }
                    }
                    bestLrc = chosen->value("syncedLyrics", "");
                    if (!bestLrc.empty()) {
                        isSyncedLrc = true;
                    } else {
                        bestLrc = chosen->value("plainLyrics", "");
                    }
                }
            } catch (...) {}
        }

        if (m_requestSeq.load() == seq) {
            if (!bestLrc.empty()) {
                fs::create_directories("cache/lyrics");
                std::ofstream out(cachePath);
                out << bestLrc;
                parseLrc(bestLrc, isSyncedLrc);
            } else {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_status = "No lyrics found on LRCLIB";
            }
        }
        m_loading.store(false);
    }).detach();
}

bool LyricsManager::hasLyrics() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return !m_lines.empty();
}

bool LyricsManager::isSynced() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_synced;
}

std::vector<LyricLine> LyricsManager::linesCopy() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_lines;
}

int LyricsManager::activeIndex(double positionSec) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_synced || m_lines.empty()) return -1;
    int idx = -1;
    for (int i = 0; i < static_cast<int>(m_lines.size()); ++i) {
        if (positionSec >= m_lines[i].timeSec) idx = i;
        else break;
    }
    return idx;
}

std::string LyricsManager::statusText() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_status;
}
