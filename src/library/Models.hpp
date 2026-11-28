#pragma once

#include <array>
#include <cctype>
#include <string>
#include <vector>

inline constexpr const char* APP_VERSION = "1.4.1";
inline constexpr const char* APP_GITHUB_REPO = "poupou-cheater/music-player-poupou";

enum class Platform {
    YouTubeMusic = 0,
    YouTube,
    TikTok,
    Local
};

enum class DownloadState {
    None = 0,
    Queued,
    Downloading,
    Downloaded,
    Failed
};

enum class GuiTemplate {
    YouTubeMusic = 0,
    Spotify,
    TikTok,
    Minimal,
    ColorHuntNeonNight,
    ColorHuntCyberMint,
    ColorHuntSunsetSynth,
    ColorHuntEmeraldDark
};

enum class AudioPreset {
    Flat = 0,
    YouTubeMusic,
    Spotify,
    TikTok,
    Custom
};

struct AudioOverride {
    bool enabled = false;
    float gainDb = 0.0f; // -12.0 .. +12.0 dB
    AudioPreset preset = AudioPreset::Custom;
    std::array<float, 5> eqGainsDb = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
};

struct Track {
    std::string id;
    std::string title;
    std::string artist;
    Platform platform = Platform::YouTubeMusic;
    std::string sourceUrl;
    double durationSec = 0.0;
    bool isVideo = false;
    std::string thumbnailUrl;
    std::string localThumbnailPath;
    std::string localMediaPath;
    DownloadState downloadState = DownloadState::None;
    bool liked = false;
    std::vector<std::string> tags;
    AudioOverride audioOverride;
};

inline std::string normalizeUtf8Display(const std::string& in) {
    // Transliterate Mathematical Bold/Italic/Script ASCII (U+1D400..U+1D7FF) & strip zero-width/combining marks
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        uint32_t cp = 0;
        size_t len = 1;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < in.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(in[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < in.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(in[i + 3]) & 0x3F);
            len = 4;
        }
        // Skip combining diacritics & zero-width chars
        if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F)) {
            i += len;
            continue;
        }
        // Map Mathematical Bold/Italic/Sans-Serif letters (U+1D400..U+1D6A3) to ASCII A-Z / a-z
        if (cp >= 0x1D400 && cp <= 0x1D6A3) {
            uint32_t offset = (cp - 0x1D400) % 52;
            out.push_back(offset < 26 ? static_cast<char>('A' + offset) : static_cast<char>('a' + (offset - 26)));
            i += len;
            continue;
        }
        out.append(in, i, len);
        i += len;
    }
    return out;
}

inline void extractHashtags(const std::string& rawTitle, std::string& cleanTitle, std::vector<std::string>& outTags) {
    cleanTitle.clear();
    outTags.clear();
    size_t i = 0;
    while (i < rawTitle.size()) {
        if (rawTitle[i] == '#' && i + 1 < rawTitle.size() && !std::isspace(static_cast<unsigned char>(rawTitle[i + 1]))) {
            size_t start = i + 1;
            size_t end = start;
            while (end < rawTitle.size() && !std::isspace(static_cast<unsigned char>(rawTitle[end]))) {
                ++end;
            }
            outTags.push_back(rawTitle.substr(start, end - start));
            i = end;
        } else {
            cleanTitle.push_back(rawTitle[i]);
            ++i;
        }
    }
    // Collapse multiple spaces and trim
    std::string collapsed;
    bool prevSpace = true;
    for (char c : cleanTitle) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prevSpace) { collapsed.push_back(' '); prevSpace = true; }
        } else {
            collapsed.push_back(c);
            prevSpace = false;
        }
    }
    if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    cleanTitle = collapsed.empty() ? rawTitle : collapsed;
}

struct Playlist {
    std::string id;
    std::string name;
    Platform platform = Platform::YouTubeMusic;
    std::string sourceUrl;
    bool autoSync = true;
    bool autoDownload = false;
    bool preferVideo = false;
    bool favorite = false;
    std::string lastSynced;
    std::vector<Track> tracks;
    AudioOverride audioOverride;
};

struct CustomTheme {
    std::string name = "My Theme";
    std::array<float, 4> bgMain = { 0.06f, 0.06f, 0.08f, 1.0f };
    std::array<float, 4> bgSidebar = { 0.04f, 0.04f, 0.05f, 1.0f };
    std::array<float, 4> bgCard = { 0.12f, 0.12f, 0.15f, 1.0f };
    std::array<float, 4> accent = { 1.00f, 0.15f, 0.25f, 1.0f };
    std::array<float, 4> accentSecondary = { 0.20f, 0.75f, 1.00f, 1.0f };
    std::array<float, 4> textPrimary = { 0.96f, 0.96f, 0.98f, 1.0f };
    float rounding = 10.0f;
};

struct CustomAudioProfile {
    std::string name = "Mon Profil Audio";
    std::array<float, 5> eqGainsDb = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float preampDb = 0.0f;
    float balance = 0.0f;
    float stereoWidth = 1.0f;
    bool monoMix = false;
    bool softLimiter = true;
};

struct AppSettings {
    bool offlineMode = false;
    GuiTemplate activeTemplate = GuiTemplate::YouTubeMusic;
    int activeCustomThemeIndex = -1;
    std::vector<CustomTheme> customThemes;
    float volume = 0.85f; // 0.0 .. 1.5 (0% .. 150%)
    bool muted = false;
    bool shuffle = false;
    int repeatMode = 0; // 0 = None, 1 = Repeat Same, 2 = Repeat Playlist/Queue
    int viewMode = 0; // 0 = Liste, 1 = Grandes Cartes, 2 = Compact
    int uiLayoutMode = 0; // 0 = Auto (selon largeur), 1 = Desktop, 2 = Téléphone (Mobile)
    float uiScale = 1.0f; // 0.75 .. 1.60 (Ctrl + / -)
    std::string selectedPlaylistId;
    int winX = -1;
    int winY = -1;
    int winW = 1400;
    int winH = 860;
    bool winMaximized = false;
    bool winOsFullscreen = false;
    int winMonitorIndex = 0;
    long long lastCacheCleanupEpoch = 0;
    AudioPreset audioPreset = AudioPreset::YouTubeMusic;
    std::array<float, 5> eqGainsDb = { 2.0f, 1.0f, 0.0f, 1.5f, 1.0f }; // 60, 230, 910, 3.6k, 14k Hz
    int activeCustomAudioIndex = -1;
    std::vector<CustomAudioProfile> customAudioProfiles;
    float preampDb = 0.0f;
    float balance = 0.0f;
    float stereoWidth = 1.0f;
    bool monoMix = false;
    bool softLimiter = true;
    bool autoHideBarsFullscreen = true;
    bool autoHideTopBar = true;
    bool autoHideBottomBar = true;
    bool autoHideFocusQueue = false;
    bool autoCheckUpdates = true;
    bool autoInstallUpdates = false;
    bool enableMediaKeys = true;
    bool mediaKeysExclusive = true; // Intercept media keys exclusively for StreamPulse
    bool forceFocusOnMediaKey = false; // Bring app to foreground on media key
    bool enableDiscordRPC = true;
    std::string discordClientId = "1206348731234566144";

    // Customizable Keybinds (GLFW key codes)
    int keyPlayPause = 32;   // GLFW_KEY_SPACE
    int keyMute = 77;        // GLFW_KEY_M
    int keyFullscreen = 70;  // GLFW_KEY_F (Mode Focus)
    int keyOsFullscreen = 300; // GLFW_KEY_F11 (Fullscreen OS)
    int keySeekBack = 263;   // GLFW_KEY_LEFT
    int keySeekFwd = 262;    // GLFW_KEY_RIGHT
    int keyVolUp = 265;      // GLFW_KEY_UP
    int keyVolDown = 264;    // GLFW_KEY_DOWN
};

inline const char* platformName(Platform p) {
    switch (p) {
        case Platform::YouTubeMusic: return "YouTube Music";
        case Platform::YouTube:      return "YouTube";
        case Platform::TikTok:       return "TikTok";
        case Platform::Local:        return "Local";
    }
    return "Unknown";
}

inline const char* platformIconKey(Platform p) {
    switch (p) {
        case Platform::YouTubeMusic: return "youtubemusic";
        case Platform::YouTube:      return "youtube";
        case Platform::TikTok:       return "tiktok";
        case Platform::Local:        return "local";
    }
    return "music";
}

inline std::array<float, 5> presetEqGains(AudioPreset preset) {
    switch (preset) {
        case AudioPreset::Flat:         return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        case AudioPreset::YouTubeMusic: return { 2.0f, 1.0f, 0.0f, 1.5f, 1.0f };
        case AudioPreset::Spotify:      return { 3.5f, 1.5f, -0.5f, 2.0f, 2.5f };
        case AudioPreset::TikTok:       return { 4.5f, 2.0f, 1.0f, 3.0f, 2.0f };
        case AudioPreset::Custom:       return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }
    return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
}
