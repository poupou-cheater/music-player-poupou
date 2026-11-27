#pragma once

#include "library/DiscordRPC.hpp"
#include "library/LibraryManager.hpp"
#include "library/PlatformSync.hpp"
#include "library/StatsManager.hpp"
#include "media/MediaPlayer.hpp"
#include "ui/IconStore.hpp"
#include "ui/MediaKeys.hpp"
#include "ui/Templates.hpp"
#include "vulkan/VulkanTexture.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;
class VulkanContext;

enum class MainTab {
    Playlists = 0,
    YouTubeMusic,
    TikTok,
    Queue,
    Downloads,
    Stats,
    Settings
};

struct QueueEntry {
    std::string playlistId;
    Track track;
};

class AppUI {
public:
    AppUI(VulkanContext& vk, IconStore& icons, LibraryManager& library, PlatformSync& sync,
          MediaPlayer& player, StatsManager& stats,
          DiscordRPC& discord, MediaKeys& mediaKeys);
    ~AppUI();

    void init(GLFWwindow* window);
    void shutdown();
    void update(double dtSeconds);
    void render();

private:
    void renderTopBar();
    void renderSidebar();
    void renderPlaylistsTab();
    void renderSearchTab(Platform plat);
    void renderQueueTab();
    void renderDownloadsTab();
    void renderStatsTab();
    void renderSettingsTab();
    void renderStagePanel(bool isFullscreen);
    void renderPlayerBar();
    void renderImportModal();
    void renderRenameModal();
    void renderAudioOverrideModal();
    void applyAudioForTrack(const std::string& playlistId, const Track& track);
    TemplatePalette activePalette() const;
    bool isMobileLayout() const;

    void handleShortcuts();
    void playTrack(const std::string& playlistId, const Track& track);
    void enqueueTrack(const std::string& playlistId, const Track& track);
    void playNext();
    void playPrev();
    void loadCoverImage(const std::string& path);
    void createDefaultCover();
    ImTextureID getTrackThumb(const Track& t);
    void toggleOsFullscreen();

    bool m_osFullscreen = false;
    int m_savedWinX = 100, m_savedWinY = 100, m_savedWinW = 1400, m_savedWinH = 860;

    VulkanContext& m_vk;
    GLFWwindow* m_window = nullptr;
    IconStore& m_icons;
    LibraryManager& m_library;
    PlatformSync& m_sync;
    MediaPlayer& m_player;
    StatsManager& m_stats;
    DiscordRPC& m_discord;
    MediaKeys& m_mediaKeys;

    MainTab m_activeTab = MainTab::Playlists;
    std::string m_selectedPlaylistId;
    bool m_showOfflineOnlyView = false;
    bool m_showLikedOnlyView = false;

    // Active playback & queue
    std::string m_currentPlaylistId;
    Track m_currentTrack;
    bool m_hasCurrentTrack = false;
    bool m_shuffle = false;
    int m_repeatMode = 0; // 0 = None, 1 = Repeat Same, 2 = Repeat Playlist/Queue
    std::vector<QueueEntry> m_queue;
    int m_queueIndex = -1;
    bool m_playingFromQueue = false;
    std::string m_preloadedForTrackId;

    // Stage modes & 3s auto-hide
    bool m_fullscreenMode = false;
    float m_stageAnim = 0.0f;
    float m_focusAnim = 0.0f;
    bool m_focusQueueOpen = true;
    bool m_mobileFocusQueue = false;
    float m_mobQueueAnim = 0.0f;
    float m_focusQueueAnim = 1.0f;
    float m_topBarAnim = 1.0f;
    float m_bottomBarAnim = 1.0f;
    int m_viewMode = 0; // 0 = Liste YT Music, 1 = Grandes Cartes, 2 = Compact
    float m_uiScale = 1.0f;
    int m_consecutiveErrors = 0;
    double m_idleSeconds = 0.0;
    ImVec2 m_lastMousePos = ImVec2(-1, -1);

    // Stats & Discord timers
    double m_statsSaveAccum = 0.0;
    double m_discordAccum = 0.0;
    double m_autoSyncAccum = 0.0;

    // Pending stream open
    std::mutex m_pendingMutex;
    bool m_hasPendingOpen = false;
    std::string m_pendingOpenUri;
    bool m_pendingPreferAudioOnly = false;
    Track m_pendingTrack;
    std::string m_pendingPlaylistId;
    std::string m_playbackNotice;

    // Platform search state (YouTube Music & TikTok tabs)
    std::mutex m_searchMutex;
    char m_ytmSearchBuf[256]{};
    bool m_ytmSearching = false;
    int m_ytmSearchLimit = 12;
    std::vector<Track> m_ytmResults;
    char m_ttSearchBuf[256]{};
    bool m_ttSearching = false;
    int m_ttSearchLimit = 12;
    std::vector<Track> m_ttResults;

    // Vulkan textures
    VulkanTexture m_videoTexture;
    VulkanTexture m_coverTexture;
    VulkanTexture m_defaultCoverTexture;
    std::unordered_map<std::string, VulkanTexture> m_thumbCache;
    int m_thumbsUploadedThisFrame = 0;
    std::vector<uint8_t> m_videoUploadBuf;
    std::string m_loadedCoverPath;

    // Keybind rebinding state (-1 = none, 0..7 = slot)
    int m_rebindingSlot = -1;

    // UI inputs
    char m_searchBuf[128]{};
    bool m_showImportModal = false;
    bool m_showCreatePlaylistModal = false;
    char m_newPlNameBuf[128]{};
    int m_newPlPlatIdx = 0;
    char m_urlBuf[512]{};
    char m_plNameBuf[128]{};
    char m_linkUrlBuf[512]{};
    int m_linkTargetPlIdx = 0;
    bool m_newAutoSync = true;
    bool m_newAutoDownload = false;
    bool m_newPreferVideo = false;

    char m_localMediaBuf[512]{};
    char m_localCoverBuf[512]{};
    char m_localTitleBuf[128]{};
    char m_localArtistBuf[128]{};
    bool m_localIsVideo = false;

    bool m_showRenameModal = false;
    bool m_renameIsPlaylist = false;
    std::string m_renamePlaylistId;
    std::string m_renameTrackId;
    char m_renameTitleBuf[256]{};
    char m_renameArtistBuf[128]{};

    bool m_showAudioModal = false;
    bool m_audioModalIsPlaylist = false;
    std::string m_audioModalPlaylistId;
    std::string m_audioModalTrackId;
    std::string m_audioModalLabel;
    AudioOverride m_audioModalData;

    char m_newThemeName[64]{ "Mon Thème" };
    CustomTheme m_newThemeDraft;
    bool m_previewCustomTheme = false;
    char m_newAudioProfileName[64]{ "Mon Profil Audio" };
};
