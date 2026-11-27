#include "library/LibraryManager.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void to_json(json& j, const Track& t) {
    j = json{
        {"id", t.id},
        {"title", t.title},
        {"artist", t.artist},
        {"platform", static_cast<int>(t.platform)},
        {"sourceUrl", t.sourceUrl},
        {"durationSec", t.durationSec},
        {"isVideo", t.isVideo},
        {"thumbnailUrl", t.thumbnailUrl},
        {"localThumbnailPath", t.localThumbnailPath},
        {"localMediaPath", t.localMediaPath},
        {"downloadState", static_cast<int>(t.downloadState)},
        {"liked", t.liked},
        {"tags", t.tags}
    };
}

static void from_json(const json& j, Track& t) {
    std::string rawTitle = normalizeUtf8Display(j.value("title", "Untitled"));
    t.id = j.value("id", "");
    t.artist = normalizeUtf8Display(j.value("artist", "Unknown"));
    t.platform = static_cast<Platform>(j.value("platform", 0));
    t.sourceUrl = j.value("sourceUrl", "");
    t.durationSec = j.value("durationSec", 0.0);
    t.isVideo = j.value("isVideo", false);
    t.thumbnailUrl = j.value("thumbnailUrl", "");
    t.localThumbnailPath = j.value("localThumbnailPath", "");
    t.localMediaPath = j.value("localMediaPath", "");
    t.downloadState = static_cast<DownloadState>(j.value("downloadState", 0));
    t.liked = j.value("liked", false);
    if (j.contains("tags") && j["tags"].is_array()) {
        t.tags = j.at("tags").get<std::vector<std::string>>();
        t.title = rawTitle;
    } else {
        extractHashtags(rawTitle, t.title, t.tags);
    }

    if (t.downloadState == DownloadState::Downloaded && !t.localMediaPath.empty()) {
        if (!fs::exists(t.localMediaPath)) {
            t.downloadState = DownloadState::None;
            t.localMediaPath.clear();
        }
    }
    if (t.downloadState == DownloadState::Downloading) {
        t.downloadState = DownloadState::Queued;
    }
}

static void to_json(json& j, const Playlist& p) {
    j = json{
        {"id", p.id},
        {"name", p.name},
        {"platform", static_cast<int>(p.platform)},
        {"sourceUrl", p.sourceUrl},
        {"autoSync", p.autoSync},
        {"autoDownload", p.autoDownload},
        {"preferVideo", p.preferVideo},
        {"lastSynced", p.lastSynced},
        {"tracks", p.tracks}
    };
}

static void from_json(const json& j, Playlist& p) {
    p.id = j.value("id", "");
    p.name = j.value("name", "Playlist");
    p.platform = static_cast<Platform>(j.value("platform", 0));
    p.sourceUrl = j.value("sourceUrl", "");
    p.autoSync = j.value("autoSync", true);
    p.autoDownload = j.value("autoDownload", false);
    p.preferVideo = j.value("preferVideo", false);
    p.lastSynced = j.value("lastSynced", "");
    if (j.contains("tracks")) {
        p.tracks = j.at("tracks").get<std::vector<Track>>();
    }
}

static void to_json(json& j, const AppSettings& s) {
    j = json{
        {"offlineMode", s.offlineMode},
        {"activeTemplate", static_cast<int>(s.activeTemplate)},
        {"volume", s.volume},
        {"muted", s.muted},
        {"shuffle", s.shuffle},
        {"repeatMode", s.repeatMode},
        {"viewMode", s.viewMode},
        {"selectedPlaylistId", s.selectedPlaylistId},
        {"winX", s.winX},
        {"winY", s.winY},
        {"winW", s.winW},
        {"winH", s.winH},
        {"winMaximized", s.winMaximized},
        {"audioPreset", static_cast<int>(s.audioPreset)},
        {"eqGainsDb", s.eqGainsDb},
        {"autoHideBarsFullscreen", s.autoHideBarsFullscreen},
        {"enableMediaKeys", s.enableMediaKeys},
        {"mediaKeysExclusive", s.mediaKeysExclusive},
        {"forceFocusOnMediaKey", s.forceFocusOnMediaKey},
        {"enableDiscordRPC", s.enableDiscordRPC},
        {"discordClientId", s.discordClientId},
        {"keyPlayPause", s.keyPlayPause},
        {"keyMute", s.keyMute},
        {"keyFullscreen", s.keyFullscreen},
        {"keyLyrics", s.keyLyrics},
        {"keySeekBack", s.keySeekBack},
        {"keySeekFwd", s.keySeekFwd},
        {"keyVolUp", s.keyVolUp},
        {"keyVolDown", s.keyVolDown}
    };
}

static void from_json(const json& j, AppSettings& s) {
    s.offlineMode = j.value("offlineMode", false);
    s.activeTemplate = static_cast<GuiTemplate>(j.value("activeTemplate", 0));
    s.volume = j.value("volume", 0.85f);
    s.muted = j.value("muted", false);
    s.shuffle = j.value("shuffle", false);
    s.repeatMode = j.value("repeatMode", j.value("repeat", false) ? 2 : 0);
    s.viewMode = j.value("viewMode", 0);
    s.selectedPlaylistId = j.value("selectedPlaylistId", "");
    s.winX = j.value("winX", -1);
    s.winY = j.value("winY", -1);
    s.winW = j.value("winW", 1400);
    s.winH = j.value("winH", 860);
    s.winMaximized = j.value("winMaximized", false);
    s.audioPreset = static_cast<AudioPreset>(j.value("audioPreset", 1));
    if (j.contains("eqGainsDb") && j["eqGainsDb"].is_array() && j["eqGainsDb"].size() == 5) {
        for (size_t i = 0; i < 5; ++i) s.eqGainsDb[i] = j["eqGainsDb"][i].get<float>();
    }
    s.autoHideBarsFullscreen = j.value("autoHideBarsFullscreen", true);
    s.enableMediaKeys = j.value("enableMediaKeys", true);
    s.mediaKeysExclusive = j.value("mediaKeysExclusive", true);
    s.forceFocusOnMediaKey = j.value("forceFocusOnMediaKey", false);
    s.enableDiscordRPC = j.value("enableDiscordRPC", true);
    s.discordClientId = j.value("discordClientId", s.discordClientId);
    s.keyPlayPause = j.value("keyPlayPause", 32);
    s.keyMute = j.value("keyMute", 77);
    s.keyFullscreen = j.value("keyFullscreen", 70);
    s.keyLyrics = j.value("keyLyrics", 76);
    s.keySeekBack = j.value("keySeekBack", 263);
    s.keySeekFwd = j.value("keySeekFwd", 262);
    s.keyVolUp = j.value("keyVolUp", 265);
    s.keyVolDown = j.value("keyVolDown", 264);
}

Platform LibraryManager::detectPlatform(const std::string& url) {
    std::string low = url;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (low.find("music.youtube.com") != std::string::npos) return Platform::YouTubeMusic;
    if (low.find("youtube.com") != std::string::npos || low.find("youtu.be") != std::string::npos) return Platform::YouTube;
    if (low.find("tiktok.com") != std::string::npos) return Platform::TikTok;
    if (low.rfind("http://", 0) != 0 && low.rfind("https://", 0) != 0) return Platform::Local;
    return Platform::YouTube;
}

std::string LibraryManager::sanitizeId(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_') out.push_back(static_cast<char>(c));
        else out.push_back('_');
    }
    if (out.empty()) {
        out = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    return out;
}

void LibraryManager::load(const std::string& jsonPath) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_jsonPath = jsonPath;

    fs::create_directories("data");
    fs::create_directories("downloads");
    fs::create_directories("cache/thumbnails");
    fs::create_directories("cache/lyrics");

    if (fs::exists(m_jsonPath)) {
        try {
            std::ifstream f(m_jsonPath);
            json j;
            f >> j;
            if (j.contains("settings")) {
                m_settings = j.at("settings").get<AppSettings>();
            } else {
                m_settings.offlineMode = j.value("offlineMode", false);
                m_settings.activeTemplate = static_cast<GuiTemplate>(j.value("activeTemplate", 0));
                m_settings.volume = j.value("volume", 0.85f);
            }
            if (j.contains("playlists")) {
                m_playlists = j.at("playlists").get<std::vector<Playlist>>();
            }
            // Auto-remove Untitled / Private / Deleted YouTube tracks
            for (auto& pl : m_playlists) {
                if (pl.platform == Platform::YouTube || pl.platform == Platform::YouTubeMusic) {
                    pl.tracks.erase(
                        std::remove_if(pl.tracks.begin(), pl.tracks.end(), [](const Track& t) {
                            return t.title.empty() || t.title == "Untitled" ||
                                   t.title == "[Private video]" || t.title == "[Deleted video]";
                        }),
                        pl.tracks.end()
                    );
                }
            }
        } catch (...) {
            m_playlists.clear();
        }
    }

    ensureDefaultPlaylists();
}

void LibraryManager::save() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    fs::create_directories(fs::path(m_jsonPath).parent_path());
    json j = {
        {"settings", m_settings},
        {"playlists", m_playlists}
    };
    std::ofstream f(m_jsonPath);
    f << j.dump(2);
}

void LibraryManager::ensureDefaultPlaylists() {
    if (!m_playlists.empty()) return;

    Playlist ytm;
    ytm.id = "ytm_favorites";
    ytm.name = "YT Music Mix";
    ytm.platform = Platform::YouTubeMusic;
    ytm.autoSync = true;
    ytm.autoDownload = false;
    ytm.preferVideo = false;

    Playlist yt;
    yt.id = "yt_videos";
    yt.name = "YouTube Clips & Live";
    yt.platform = Platform::YouTube;
    yt.autoSync = true;
    yt.autoDownload = false;
    yt.preferVideo = true;

    Playlist tt;
    tt.id = "tt_sounds";
    tt.name = "TikTok Sounds & Videos";
    tt.platform = Platform::TikTok;
    tt.autoSync = true;
    tt.autoDownload = false;
    tt.preferVideo = true;

    Playlist loc;
    loc.id = "local_music";
    loc.name = "Local Music & Video";
    loc.platform = Platform::Local;
    loc.autoSync = false;
    loc.autoDownload = false;

    m_playlists = { ytm, yt, tt, loc };
}

std::vector<Playlist> LibraryManager::playlistsCopy() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_playlists;
}

std::optional<Playlist> LibraryManager::getPlaylist(const std::string& playlistId) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (const auto& p : m_playlists) {
        if (p.id == playlistId) return p;
    }
    return std::nullopt;
}

std::optional<Track> LibraryManager::getTrack(const std::string& playlistId, const std::string& trackId) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (const auto& p : m_playlists) {
        if (p.id == playlistId) {
            for (const auto& t : p.tracks) {
                if (t.id == trackId) return t;
            }
        }
    }
    return std::nullopt;
}

std::string LibraryManager::createPlaylist(const std::string& name, Platform platform, const std::string& sourceUrl,
                                           bool autoSync, bool autoDownload, bool preferVideo) {
    std::lock_guard<std::mutex> lock(m_mtx);
    Playlist p;
    p.id = sanitizeId(name) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
    p.name = name.empty() ? "New Playlist" : name;
    p.platform = platform;
    p.sourceUrl = sourceUrl;
    p.autoSync = autoSync;
    p.autoDownload = autoDownload;
    p.preferVideo = preferVideo;
    m_playlists.push_back(std::move(p));
    return m_playlists.back().id;
}

void LibraryManager::deletePlaylist(const std::string& playlistId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto it = m_playlists.begin(); it != m_playlists.end(); ++it) {
        if (it->id == playlistId) {
            m_deletedPlaylists.push_back(std::move(*it));
            m_playlists.erase(it);
            break;
        }
    }
}

bool LibraryManager::canUndoDeletePlaylist() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return !m_deletedPlaylists.empty();
}

std::string LibraryManager::undoDeletePlaylist() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_deletedPlaylists.empty()) return "";
    Playlist p = std::move(m_deletedPlaylists.back());
    m_deletedPlaylists.pop_back();
    std::string id = p.id;
    m_playlists.push_back(std::move(p));
    return id;
}

void LibraryManager::clearPlaylistLocalFiles(const std::string& playlistId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (!t.localMediaPath.empty() && fs::exists(t.localMediaPath)) {
                std::error_code ec;
                fs::remove(t.localMediaPath, ec);
            }
            t.localMediaPath.clear();
            t.downloadState = DownloadState::None;
        }
        break;
    }
}

void LibraryManager::renamePlaylist(const std::string& playlistId, const std::string& newName) {
    if (newName.empty()) return;
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id == playlistId) {
            p.name = newName;
            break;
        }
    }
}

void LibraryManager::setPlaylistOptions(const std::string& playlistId, bool autoSync, bool autoDownload, bool preferVideo) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id == playlistId) {
            p.autoSync = autoSync;
            p.autoDownload = autoDownload;
            p.preferVideo = preferVideo;
            break;
        }
    }
}

void LibraryManager::mergeSyncedPlaylist(const std::string& playlistId, const std::string& resolvedName,
                                         const std::vector<Track>& fetchedTracks, const std::string& timestamp) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        if (!resolvedName.empty() && p.name == "New Playlist") {
            p.name = resolvedName;
        }
        p.lastSynced = timestamp;

        if (!fetchedTracks.empty()) {
            // 1. Remove tracks that were deleted from the remote playlist
            p.tracks.erase(
                std::remove_if(p.tracks.begin(), p.tracks.end(), [&](const Track& existing) {
                    bool stillPresent = std::any_of(fetchedTracks.begin(), fetchedTracks.end(), [&](const Track& inc) {
                        return inc.id == existing.id || (!inc.sourceUrl.empty() && inc.sourceUrl == existing.sourceUrl);
                    });
                    if (!stillPresent && !existing.localMediaPath.empty() && fs::exists(existing.localMediaPath)) {
                        std::error_code ec;
                        fs::remove(existing.localMediaPath, ec);
                    }
                    return !stillPresent;
                }),
                p.tracks.end()
            );
        }

        // 2. Add new tracks and update missing metadata on existing tracks
        for (const auto& incoming : fetchedTracks) {
            auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](const Track& existing) {
                return existing.id == incoming.id || (!incoming.sourceUrl.empty() && existing.sourceUrl == incoming.sourceUrl);
            });
            if (it == p.tracks.end()) {
                p.tracks.push_back(incoming);
            } else {
                if (it->title.empty() || it->title.find('?') != std::string::npos) it->title = incoming.title;
                if (it->artist.empty() || it->artist.find('?') != std::string::npos) it->artist = incoming.artist;
                if (it->thumbnailUrl.empty()) it->thumbnailUrl = incoming.thumbnailUrl;
                if (it->durationSec <= 0.0) it->durationSec = incoming.durationSec;
            }
        }
        break;
    }
}

void LibraryManager::addTrackToPlaylist(const std::string& playlistId, const Track& track) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](const Track& ex) {
            return ex.id == track.id;
        });
        if (it == p.tracks.end()) {
            p.tracks.push_back(track);
        }
        break;
    }
}

void LibraryManager::renameTrack(const std::string& playlistId, const std::string& trackId,
                                 const std::string& newTitle, const std::string& newArtist) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (t.id == trackId) {
                if (!newTitle.empty()) t.title = newTitle;
                if (!newArtist.empty()) t.artist = newArtist;
                break;
            }
        }
        break;
    }
}

void LibraryManager::moveTrackInPlaylist(const std::string& playlistId, size_t fromIdx, size_t toIdx) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        if (fromIdx < p.tracks.size() && toIdx < p.tracks.size() && fromIdx != toIdx) {
            Track item = std::move(p.tracks[fromIdx]);
            p.tracks.erase(p.tracks.begin() + static_cast<ptrdiff_t>(fromIdx));
            p.tracks.insert(p.tracks.begin() + static_cast<ptrdiff_t>(toIdx), std::move(item));
        }
        break;
    }
}

void LibraryManager::removeTrackFromPlaylist(const std::string& playlistId, const std::string& trackId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        p.tracks.erase(
            std::remove_if(p.tracks.begin(), p.tracks.end(), [&](const Track& t) { return t.id == trackId; }),
            p.tracks.end()
        );
        break;
    }
}

void LibraryManager::clearTrackLocalMedia(const std::string& playlistId, const std::string& trackId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (t.id == trackId) {
                if (!t.localMediaPath.empty() && fs::exists(t.localMediaPath)) {
                    std::error_code ec;
                    fs::remove(t.localMediaPath, ec);
                }
                t.localMediaPath.clear();
                t.downloadState = DownloadState::None;
                break;
            }
        }
        break;
    }
}

void LibraryManager::addLocalTrack(const std::string& playlistId, const std::string& mediaPath,
                                   const std::string& coverImagePath, const std::string& title,
                                   const std::string& artist, bool isVideo) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        Track t;
        t.id = sanitizeId(fs::path(mediaPath).stem().string()) + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
        t.title = title.empty() ? fs::path(mediaPath).stem().string() : title;
        t.artist = artist.empty() ? "Local Artist" : artist;
        t.platform = Platform::Local;
        t.sourceUrl = mediaPath;
        t.localMediaPath = mediaPath;
        t.localThumbnailPath = coverImagePath;
        t.isVideo = isVideo;
        t.downloadState = DownloadState::Downloaded;
        p.tracks.push_back(std::move(t));
        break;
    }
}

void LibraryManager::updateTrackDownload(const std::string& playlistId, const std::string& trackId,
                                         DownloadState state, const std::string& localPath) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (t.id == trackId) {
                t.downloadState = state;
                if (!localPath.empty()) t.localMediaPath = localPath;
                break;
            }
        }
    }
}

void LibraryManager::updateTrackThumbnail(const std::string& playlistId, const std::string& trackId,
                                          const std::string& localThumbPath) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (t.id == trackId) {
                t.localThumbnailPath = localThumbPath;
                break;
            }
        }
    }
}

void LibraryManager::toggleTrackLike(const std::string& playlistId, const std::string& trackId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (t.id == trackId) {
                t.liked = !t.liked;
                break;
            }
        }
    }
}

AppSettings LibraryManager::settings() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_settings;
}

void LibraryManager::updateSettings(const AppSettings& s) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings = s;
}

bool LibraryManager::offlineMode() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_settings.offlineMode;
}

void LibraryManager::setOfflineMode(bool offline) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings.offlineMode = offline;
}

GuiTemplate LibraryManager::activeTemplate() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_settings.activeTemplate;
}

void LibraryManager::setActiveTemplate(GuiTemplate tpl) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings.activeTemplate = tpl;
}

float LibraryManager::volume() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_settings.volume;
}

void LibraryManager::setVolume(float vol) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings.volume = vol;
}
