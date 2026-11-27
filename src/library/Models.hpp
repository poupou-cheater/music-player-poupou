#pragma once

#include <array>
#include <cctype>
#include <string>
#include <vector>

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
    Minimal
};

enum class AudioPreset {
    Flat = 0,
    YouTubeMusic,
    Spotify,
    TikTok,
    Custom
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
};

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
    std::string lastSynced;
    std::vector<Track> tracks;
};

struct AppSettings {
    bool offlineMode = false;
    GuiTemplate activeTemplate = GuiTemplate::YouTubeMusic;
    float volume = 0.85f; // 0.0 .. 1.5 (0% .. 150%)
    bool muted = false;
    bool shuffle = false;
    bool repeat = false;
    int viewMode = 0; // 0 = Liste, 1 = Grandes Cartes, 2 = Compact
    std::string selectedPlaylistId;
    AudioPreset audioPreset = AudioPreset::YouTubeMusic;
    std::array<float, 5> eqGainsDb = { 2.0f, 1.0f, 0.0f, 1.5f, 1.0f }; // 60, 230, 910, 3.6k, 14k Hz
    bool autoHideBarsFullscreen = true;
    bool enableMediaKeys = true;
    bool mediaKeysExclusive = true; // Intercept media keys exclusively for StreamPulse
    bool forceFocusOnMediaKey = false; // Bring app to foreground on media key
    bool enableDiscordRPC = true;
    std::string discordClientId = "1206348731234566144";

    // Customizable Keybinds (GLFW key codes)
    int keyPlayPause = 32;   // GLFW_KEY_SPACE
    int keyMute = 77;        // GLFW_KEY_M
    int keyFullscreen = 70;  // GLFW_KEY_F
    int keyLyrics = 76;      // GLFW_KEY_L
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
