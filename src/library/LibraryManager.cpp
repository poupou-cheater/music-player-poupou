#include "library/LibraryManager.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void to_json(json& j, const AudioOverride& o) {
    j = json{
        {"enabled", o.enabled},
        {"gainDb", o.gainDb},
        {"preset", static_cast<int>(o.preset)},
        {"eqGainsDb", o.eqGainsDb}
    };
}

static void from_json(const json& j, AudioOverride& o) {
    o.enabled = j.value("enabled", false);
    o.gainDb = j.value("gainDb", 0.0f);
    o.preset = static_cast<AudioPreset>(j.value("preset", 4));
    if (j.contains("eqGainsDb") && j["eqGainsDb"].is_array() && j["eqGainsDb"].size() == 5) {
        for (size_t i = 0; i < 5; ++i) o.eqGainsDb[i] = j["eqGainsDb"][i].get<float>();
    }
}

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
        {"tags", t.tags},
        {"audioOverride", t.audioOverride}
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

    if (j.contains("audioOverride")) {
        t.audioOverride = j.at("audioOverride").get<AudioOverride>();
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
        {"favorite", p.favorite},
        {"lastSynced", p.lastSynced},
        {"tracks", p.tracks},
        {"audioOverride", p.audioOverride}
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
    p.favorite = j.value("favorite", false);
    p.lastSynced = j.value("lastSynced", "");
    if (j.contains("tracks")) {
        p.tracks = j.at("tracks").get<std::vector<Track>>();
    }
    if (j.contains("audioOverride")) {
        p.audioOverride = j.at("audioOverride").get<AudioOverride>();
    }
}

static void to_json(json& j, const CustomTheme& c) {
    j = json{
        {"name", c.name},
        {"bgMain", c.bgMain},
        {"bgSidebar", c.bgSidebar},
        {"bgCard", c.bgCard},
        {"accent", c.accent},
        {"accentSecondary", c.accentSecondary},
        {"textPrimary", c.textPrimary},
        {"rounding", c.rounding}
    };
}

static void from_json(const json& j, CustomTheme& c) {
    c.name = j.value("name", "Custom Theme");
    auto loadCol = [&](const char* k, std::array<float, 4>& dst) {
        if (j.contains(k) && j[k].is_array() && j[k].size() == 4) {
            for (size_t i = 0; i < 4; ++i) dst[i] = j[k][i].get<float>();
        }
    };
    loadCol("bgMain", c.bgMain);
    loadCol("bgSidebar", c.bgSidebar);
    loadCol("bgCard", c.bgCard);
    loadCol("accent", c.accent);
    loadCol("accentSecondary", c.accentSecondary);
    loadCol("textPrimary", c.textPrimary);
    c.rounding = j.value("rounding", 10.0f);
}

static void to_json(json& j, const CustomAudioProfile& a) {
    j = json{
        {"name", a.name},
        {"eqGainsDb", a.eqGainsDb},
        {"preampDb", a.preampDb},
        {"balance", a.balance},
        {"stereoWidth", a.stereoWidth},
        {"monoMix", a.monoMix},
        {"softLimiter", a.softLimiter}
    };
}

static void from_json(const json& j, CustomAudioProfile& a) {
    a.name = j.value("name", std::string("Mon Profil Audio"));
    if (j.contains("eqGainsDb") && j["eqGainsDb"].is_array() && j["eqGainsDb"].size() == 5) {
        for (size_t i = 0; i < 5; ++i) a.eqGainsDb[i] = j["eqGainsDb"][i].get<float>();
    }
    a.preampDb = j.value("preampDb", 0.0f);
    a.balance = j.value("balance", 0.0f);
    a.stereoWidth = j.value("stereoWidth", 1.0f);
    a.monoMix = j.value("monoMix", false);
    a.softLimiter = j.value("softLimiter", true);
}

static void to_json(json& j, const AppSettings& s) {
    j = json{
        {"offlineMode", s.offlineMode},
        {"activeTemplate", static_cast<int>(s.activeTemplate)},
        {"activeCustomThemeIndex", s.activeCustomThemeIndex},
        {"customThemes", s.customThemes},
        {"volume", s.volume},
        {"muted", s.muted},
        {"shuffle", s.shuffle},
        {"repeatMode", s.repeatMode},
        {"viewMode", s.viewMode},
        {"uiLayoutMode", s.uiLayoutMode},
        {"uiScale", s.uiScale},
        {"selectedPlaylistId", s.selectedPlaylistId},
        {"winX", s.winX},
        {"winY", s.winY},
        {"winW", s.winW},
        {"winH", s.winH},
        {"winMaximized", s.winMaximized},
        {"winOsFullscreen", s.winOsFullscreen},
        {"winMonitorIndex", s.winMonitorIndex},
        {"lastCacheCleanupEpoch", s.lastCacheCleanupEpoch},
        {"audioPreset", static_cast<int>(s.audioPreset)},
        {"eqGainsDb", s.eqGainsDb},
        {"activeCustomAudioIndex", s.activeCustomAudioIndex},
        {"customAudioProfiles", s.customAudioProfiles},
        {"preampDb", s.preampDb},
        {"balance", s.balance},
        {"stereoWidth", s.stereoWidth},
        {"monoMix", s.monoMix},
        {"softLimiter", s.softLimiter},
        {"autoHideBarsFullscreen", s.autoHideBarsFullscreen},
        {"autoHideTopBar", s.autoHideTopBar},
        {"autoHideBottomBar", s.autoHideBottomBar},
        {"autoCheckUpdates", s.autoCheckUpdates},
        {"enableMediaKeys", s.enableMediaKeys},
        {"mediaKeysExclusive", s.mediaKeysExclusive},
        {"forceFocusOnMediaKey", s.forceFocusOnMediaKey},
        {"enableDiscordRPC", s.enableDiscordRPC},
        {"discordClientId", s.discordClientId},
        {"keyPlayPause", s.keyPlayPause},
        {"keyMute", s.keyMute},
        {"keyFullscreen", s.keyFullscreen},
        {"keyOsFullscreen", s.keyOsFullscreen},
        {"keySeekBack", s.keySeekBack},
        {"keySeekFwd", s.keySeekFwd},
        {"keyVolUp", s.keyVolUp},
        {"keyVolDown", s.keyVolDown}
    };
}

static void from_json(const json& j, AppSettings& s) {
    s.offlineMode = j.value("offlineMode", false);
    s.activeTemplate = static_cast<GuiTemplate>(j.value("activeTemplate", 0));
    s.activeCustomThemeIndex = j.value("activeCustomThemeIndex", -1);
    if (j.contains("customThemes") && j["customThemes"].is_array()) {
        s.customThemes = j.at("customThemes").get<std::vector<CustomTheme>>();
    }
    s.volume = j.value("volume", 0.85f);
    s.muted = j.value("muted", false);
    s.shuffle = j.value("shuffle", false);
    s.repeatMode = j.value("repeatMode", j.value("repeat", false) ? 2 : 0);
    s.viewMode = j.value("viewMode", 0);
    s.uiLayoutMode = j.value("uiLayoutMode", 0);
    s.uiScale = j.value("uiScale", 1.0f);
    s.selectedPlaylistId = j.value("selectedPlaylistId", "");
    s.winX = j.value("winX", -1);
    s.winY = j.value("winY", -1);
    s.winW = j.value("winW", 1400);
    s.winH = j.value("winH", 860);
    s.winMaximized = j.value("winMaximized", false);
    s.winOsFullscreen = j.value("winOsFullscreen", false);
    s.winMonitorIndex = j.value("winMonitorIndex", 0);
    s.lastCacheCleanupEpoch = j.value("lastCacheCleanupEpoch", 0LL);
    s.audioPreset = static_cast<AudioPreset>(j.value("audioPreset", 1));
    if (j.contains("eqGainsDb") && j["eqGainsDb"].is_array() && j["eqGainsDb"].size() == 5) {
        for (size_t i = 0; i < 5; ++i) s.eqGainsDb[i] = j["eqGainsDb"][i].get<float>();
    }
    s.activeCustomAudioIndex = j.value("activeCustomAudioIndex", -1);
    if (j.contains("customAudioProfiles") && j["customAudioProfiles"].is_array()) {
        s.customAudioProfiles = j.at("customAudioProfiles").get<std::vector<CustomAudioProfile>>();
    }
    s.preampDb = j.value("preampDb", 0.0f);
    s.balance = j.value("balance", 0.0f);
    s.stereoWidth = j.value("stereoWidth", 1.0f);
    s.monoMix = j.value("monoMix", false);
    s.softLimiter = j.value("softLimiter", true);
    s.autoHideBarsFullscreen = j.value("autoHideBarsFullscreen", true);
    s.autoHideTopBar = j.value("autoHideTopBar", true);
    s.autoHideBottomBar = j.value("autoHideBottomBar", true);
    s.autoCheckUpdates = j.value("autoCheckUpdates", true);
    s.enableMediaKeys = j.value("enableMediaKeys", true);
    s.mediaKeysExclusive = j.value("mediaKeysExclusive", true);
    s.forceFocusOnMediaKey = j.value("forceFocusOnMediaKey", false);
    s.enableDiscordRPC = j.value("enableDiscordRPC", true);
    s.discordClientId = j.value("discordClientId", s.discordClientId);
    s.keyPlayPause = j.value("keyPlayPause", 32);
    s.keyMute = j.value("keyMute", 77);
    s.keyFullscreen = j.value("keyFullscreen", 70);
    s.keyOsFullscreen = j.value("keyOsFullscreen", 300);
    s.keySeekBack = j.value("keySeekBack", 263);
    s.keySeekFwd = j.value("keySeekFwd", 262);
    s.keyVolUp = j.value("keyVolUp", 265);
    s.keyVolDown = j.value("keyVolDown", 264);
}

bool LibraryManager::exportSettingsToFile(const std::string& path) const {
    if (path.empty()) return false;
    AppSettings s = settings();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    json j = s;
    out << j.dump(2);
    return out.good();
}

bool LibraryManager::importSettingsFromFile(const std::string& path) {
    if (path.empty() || !fs::exists(path)) return false;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;
        json j;
        in >> j;
        if (!j.is_object()) return false;
        AppSettings s = j.get<AppSettings>();
        updateSettings(s);
        return true;
    } catch (...) {
        return false;
    }
}

std::string LibraryManager::exportThemeText(const CustomTheme& t) {
    json j = t;
    return j.dump();
}

std::optional<CustomTheme> LibraryManager::importThemeText(const std::string& txt) {
    if (txt.empty()) return std::nullopt;
    try {
        json j = json::parse(txt);
        if (!j.is_object()) return std::nullopt;
        return j.get<CustomTheme>();
    } catch (...) {
        return std::nullopt;
    }
}

std::string LibraryManager::exportAudioProfileText(const CustomAudioProfile& a) {
    json j = a;
    return j.dump();
}

std::optional<CustomAudioProfile> LibraryManager::importAudioProfileText(const std::string& txt) {
    if (txt.empty()) return std::nullopt;
    try {
        json j = json::parse(txt);
        if (!j.is_object()) return std::nullopt;
        return j.get<CustomAudioProfile>();
    } catch (...) {
        return std::nullopt;
    }
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
    std::string dumped = j.dump(2);
    if (dumped == m_lastSavedDump) return;
    std::ofstream f(m_jsonPath);
    if (f.is_open()) {
        f << dumped;
        m_lastSavedDump = std::move(dumped);
    }
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

std::vector<PlaylistSummary> LibraryManager::playlistSummaries() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    std::vector<PlaylistSummary> out;
    out.reserve(m_playlists.size());
    for (const auto& p : m_playlists) {
        out.push_back({ p.id, p.name, p.platform, p.favorite, p.tracks.size() });
    }
    return out;
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

void LibraryManager::reorderPlaylist(const std::string& draggedId, const std::string& targetId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (draggedId == targetId) return;
    auto fromIt = std::find_if(m_playlists.begin(), m_playlists.end(), [&](const Playlist& p) { return p.id == draggedId; });
    auto toIt = std::find_if(m_playlists.begin(), m_playlists.end(), [&](const Playlist& p) { return p.id == targetId; });
    if (fromIt == m_playlists.end() || toIt == m_playlists.end()) return;
    size_t fromIdx = static_cast<size_t>(std::distance(m_playlists.begin(), fromIt));
    size_t toIdx = static_cast<size_t>(std::distance(m_playlists.begin(), toIt));
    Playlist item = std::move(m_playlists[fromIdx]);
    m_playlists.erase(m_playlists.begin() + static_cast<std::ptrdiff_t>(fromIdx));
    if (fromIdx < toIdx) --toIdx;
    m_playlists.insert(m_playlists.begin() + static_cast<std::ptrdiff_t>(toIdx), std::move(item));
}

void LibraryManager::movePlaylistToPlatform(const std::string& playlistId, Platform plat) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id == playlistId) {
            p.platform = plat;
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

void LibraryManager::togglePlaylistFavorite(const std::string& playlistId) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id == playlistId) {
            p.favorite = !p.favorite;
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
            // Preserve existing track state (likes, local files, EQ overrides) while matching remote order
            std::vector<Track> merged;
            merged.reserve(fetchedTracks.size());
            for (const auto& incoming : fetchedTracks) {
                auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](const Track& existing) {
                    return existing.id == incoming.id || (!incoming.sourceUrl.empty() && existing.sourceUrl == incoming.sourceUrl);
                });
                if (it == p.tracks.end()) {
                    merged.push_back(incoming);
                } else {
                    Track updated = *it;
                    if (updated.title.empty() || updated.title.find('?') != std::string::npos) updated.title = incoming.title;
                    if (updated.artist.empty() || updated.artist.find('?') != std::string::npos) updated.artist = incoming.artist;
                    if (updated.thumbnailUrl.empty()) updated.thumbnailUrl = incoming.thumbnailUrl;
                    if (updated.durationSec <= 0.0) updated.durationSec = incoming.durationSec;
                    merged.push_back(std::move(updated));
                }
            }

            // Clean up local files for tracks removed from the remote playlist
            for (const auto& existing : p.tracks) {
                bool stillPresent = std::any_of(merged.begin(), merged.end(), [&](const Track& m) {
                    return m.id == existing.id || (!m.sourceUrl.empty() && m.sourceUrl == existing.sourceUrl);
                });
                if (!stillPresent && !existing.localMediaPath.empty() && fs::exists(existing.localMediaPath)) {
                    std::error_code ec;
                    fs::remove(existing.localMediaPath, ec);
                }
            }
            p.tracks = std::move(merged);
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

void LibraryManager::setPlaylistAudioOverride(const std::string& playlistId, const AudioOverride& ov) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id == playlistId) {
            p.audioOverride = ov;
            break;
        }
    }
}

void LibraryManager::setTrackAudioOverride(const std::string& playlistId, const std::string& trackId, const AudioOverride& ov) {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& p : m_playlists) {
        if (p.id != playlistId) continue;
        for (auto& t : p.tracks) {
            if (t.id == trackId) {
                t.audioOverride = ov;
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
