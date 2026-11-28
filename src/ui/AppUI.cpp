#include "ui/AppUI.hpp"
#include "ui/AppLog.hpp"
#include "ui/ImageLoader.hpp"
#include "ui/Templates.hpp"
#include "vulkan/VulkanContext.hpp"

#include <GLFW/glfw3.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

static std::string browseFileWin32(const wchar_t* filter);
static std::string saveFileWin32(const wchar_t* filter, const wchar_t* defaultName);

static std::string formatTime(double sec) {
    if (sec < 0.0) sec = 0.0;
    int total = static_cast<int>(sec);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buf[24];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}

static bool containsIgnoreCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }
    );
    return it != hay.end();
}

static bool matchesFilter(const Track& t, const std::string& filter) {
    if (filter.empty()) return true;
    if (containsIgnoreCase(t.title, filter) || containsIgnoreCase(t.artist, filter)) return true;
    std::string q = (filter[0] == '#') ? filter.substr(1) : filter;
    for (const auto& tag : t.tags) {
        if (containsIgnoreCase(tag, q)) return true;
    }
    return false;
}

AppUI::AppUI(VulkanContext& vk, IconStore& icons, LibraryManager& library, PlatformSync& sync,
             MediaPlayer& player, StatsManager& stats,
             DiscordRPC& discord, MediaKeys& mediaKeys)
    : m_vk(vk), m_icons(icons), m_library(library), m_sync(sync),
      m_player(player), m_stats(stats),
      m_discord(discord), m_mediaKeys(mediaKeys) {}

AppUI::~AppUI() = default;

TemplatePalette AppUI::activePalette() const {
    if (m_previewCustomTheme && m_activeTab == MainTab::Settings) {
        return paletteFromCustom(m_newThemeDraft);
    }
    auto s = m_library.settings();
    if (s.activeCustomThemeIndex >= 0 && s.activeCustomThemeIndex < static_cast<int>(s.customThemes.size())) {
        return paletteFromCustom(s.customThemes[s.activeCustomThemeIndex]);
    }
    return getTemplatePalette(s.activeTemplate);
}

bool AppUI::isMobileLayout() const {
    auto s = m_library.settings();
    if (s.uiLayoutMode == 2) return true;
    if (s.uiLayoutMode == 1) return false;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return vp && vp->WorkSize.x < 720.0f;
}

void AppUI::init(GLFWwindow* window) {
    m_window = window;
    auto s = m_library.settings();
    applyPaletteStyle(activePalette(), m_vk);
    m_player.setVolume(s.volume);
    m_player.setMuted(s.muted);
    m_player.setEqGains(s.eqGainsDb);
    m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
    m_shuffle = s.shuffle;
    m_repeatMode = s.repeatMode;
    m_viewMode = std::clamp(s.viewMode, 0, 1);
    m_uiScale = std::max(0.20f, s.uiScale);
    m_osFullscreen = (glfwGetWindowMonitor(window) != nullptr);
    if (m_osFullscreen) m_fullscreenMode = true;
    m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
    m_discord.setEnabled(s.enableDiscordRPC, s.discordClientId);
    createDefaultCover();

    auto lists = m_library.playlistsCopy();
    if (!s.selectedPlaylistId.empty() && m_library.getPlaylist(s.selectedPlaylistId)) {
        m_selectedPlaylistId = s.selectedPlaylistId;
    } else if (!lists.empty()) {
        m_selectedPlaylistId = lists.front().id;
    }

    if (!s.offlineMode) {
        m_sync.syncAllAutoPlaylistsAsync();
        if (s.autoCheckUpdates) m_sync.checkUpdatesAsync();
    } else {
        auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        if (nowSec - s.lastCacheCleanupEpoch >= 86400) {
            s.lastCacheCleanupEpoch = nowSec;
            m_library.updateSettings(s);
            m_sync.cleanupOrphanCacheAsync();
        }
    }
}

void AppUI::shutdown() {
    m_videoTexture.destroy(m_vk);
    m_coverTexture.destroy(m_vk);
    m_defaultCoverTexture.destroy(m_vk);
    for (auto& kv : m_thumbCache) kv.second.destroy(m_vk);
    m_thumbCache.clear();
}

void AppUI::createDefaultCover() {
    const uint32_t W = 256, H = 256;
    std::vector<uint8_t> px(W * H * 4);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            float fx = static_cast<float>(x) / W;
            float fy = static_cast<float>(y) / H;
            size_t idx = (static_cast<size_t>(y) * W + x) * 4;
            px[idx + 0] = static_cast<uint8_t>(28 + fx * 65);
            px[idx + 1] = static_cast<uint8_t>(24 + fy * 45);
            px[idx + 2] = static_cast<uint8_t>(55 + (1.0f - fx) * 90);
            px[idx + 3] = 255;
        }
    }
    m_defaultCoverTexture.createFromRGBA(m_vk, px.data(), W, H);
}

void AppUI::loadCoverImage(const std::string& path) {
    if (path.empty() || !fs::exists(path)) {
        m_coverTexture.destroy(m_vk);
        m_loadedCoverPath.clear();
        return;
    }
    if (path == m_loadedCoverPath && m_coverTexture.isValid()) return;
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (loadImageRGBA(path, rgba, w, h)) {
        m_coverTexture.createFromRGBA(m_vk, rgba.data(), w, h);
        m_loadedCoverPath = path;
    }
}

bool AppUI::needsContinuousRender() const {
    if (m_player.hasVideo() && m_player.isPlaying()) return true;
    if (m_sync.activeTaskCount() > 0 || m_rebindingSlot >= 0) return true;
    auto animActive = [](float v, float target) { return std::abs(v - target) > 0.005f; };
    if (animActive(m_stageAnim, m_hasCurrentTrack ? 1.0f : 0.0f)) return true;
    if (animActive(m_focusAnim, m_fullscreenMode ? 1.0f : 0.0f)) return true;
    auto st = m_library.settings();
    bool idleHide = m_fullscreenMode && st.autoHideBarsFullscreen && (m_idleSeconds >= 3.0);
    bool hideTop = idleHide && st.autoHideTopBar;
    bool hideBot = (idleHide && st.autoHideBottomBar) || (isMobileLayout() && m_fullscreenMode);
    bool hideFocusQueue = idleHide && st.autoHideFocusQueue;
    if (animActive(m_focusQueueAnim, (m_focusQueueOpen && !hideFocusQueue) ? 1.0f : 0.0f)) return true;
    if (animActive(m_mobQueueAnim, m_mobileFocusQueue ? 1.0f : 0.0f)) return true;
    if (animActive(m_topBarAnim, hideTop ? 0.0f : 1.0f)) return true;
    if (animActive(m_bottomBarAnim, hideBot ? 0.0f : 1.0f)) return true;
    if (m_idleSeconds < 0.45) return true;
    return false;
}

ImTextureID AppUI::getTrackThumb(const Track& t) {
    if (t.localThumbnailPath.empty()) {
        return m_defaultCoverTexture.imguiId();
    }
    auto it = m_thumbCache.find(t.id);
    if (it != m_thumbCache.end() && it->second.isValid()) {
        return it->second.imguiId();
    }
    if (m_thumbsUploadedThisFrame >= 3) {
        return m_defaultCoverTexture.imguiId();
    }
    if (!fs::exists(t.localThumbnailPath)) {
        return m_defaultCoverTexture.imguiId();
    }
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (loadImageRGBA(t.localThumbnailPath, rgba, w, h, 220)) {
        VulkanTexture tex;
        if (tex.createFromRGBA(m_vk, rgba.data(), w, h)) {
            ++m_thumbsUploadedThisFrame;
            if (m_thumbCache.size() >= 180 && !m_thumbLru.empty()) {
                std::string oldId = m_thumbLru.front();
                m_thumbLru.erase(m_thumbLru.begin());
                auto oldIt = m_thumbCache.find(oldId);
                if (oldIt != m_thumbCache.end()) {
                    oldIt->second.destroy(m_vk);
                    m_thumbCache.erase(oldIt);
                }
            }
            m_thumbCache[t.id] = tex;
            m_thumbLru.push_back(t.id);
            return m_thumbCache[t.id].imguiId();
        }
    }
    return m_defaultCoverTexture.imguiId();
}

void AppUI::applyAudioForTrack(const std::string& playlistId, const Track& track) {
    auto s = m_library.settings();
    if (track.audioOverride.enabled) {
        m_player.setExtraGainDb(track.audioOverride.gainDb);
        m_player.setEqGains(track.audioOverride.eqGainsDb);
        return;
    }
    auto pl = m_library.getPlaylist(playlistId);
    if (pl && pl->audioOverride.enabled) {
        m_player.setExtraGainDb(pl->audioOverride.gainDb);
        m_player.setEqGains(pl->audioOverride.eqGainsDb);
        return;
    }
    m_player.setExtraGainDb(0.0f);
    m_player.setEqGains(s.eqGainsDb);
}

void AppUI::playTrack(const std::string& playlistId, const Track& track) {
    static std::string s_lastPlayKey;
    static std::chrono::steady_clock::time_point s_lastPlayTime{};
    std::string playKey = !track.id.empty() ? track.id : track.sourceUrl;
    auto now = std::chrono::steady_clock::now();
    if (!playKey.empty() && playKey == s_lastPlayKey &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastPlayTime).count() < 1000) {
        return;
    }
    s_lastPlayKey = playKey;
    s_lastPlayTime = now;

    bool offline = m_library.offlineMode();
    if (!track.localMediaPath.empty() && fs::exists(track.localMediaPath)) {
        m_currentPlaylistId = playlistId;
        m_currentTrack = track;
        m_hasCurrentTrack = true;
        m_preloadedForTrackId.clear();
        applyAudioForTrack(playlistId, track);
        loadCoverImage(track.localThumbnailPath);
        m_stats.recordPlayStart(track);
        m_discord.updatePresence(track, true, 0.0, track.durationSec);
        m_player.open(track.localMediaPath, !track.isVideo);
        m_playbackNotice = "Playing local file";
        return;
    }

    if (offline) {
        m_playbackNotice = "Offline Mode: track not downloaded";
        return;
    }

    if (!track.sourceUrl.empty()) {
        // Keep current music playing until the new stream is ready!
        m_playbackNotice = "Chargement: " + track.title + "...";
        m_sync.resolveDirectStreamAsync(track,
            [this, playlistId, track](bool ok, const std::string& streamUrl, const std::string& err) {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                if (ok) {
                    m_pendingOpenUri = streamUrl;
                    m_pendingPreferAudioOnly = !track.isVideo;
                    m_pendingTrack = track;
                    m_pendingPlaylistId = playlistId;
                    m_hasPendingOpen = true;
                } else {
                    m_playbackNotice = err.empty() ? "Stream error" : err;
                }
            });
    }
}

void AppUI::enqueueTrack(const std::string& playlistId, const Track& track) {
    m_queue.push_back({ playlistId, track });
}

void AppUI::playNext() {
    // 1. If Queue has tracks and we're either playing from Queue or Queue is non-empty
    if (!m_queue.empty() && (m_playingFromQueue || m_queueIndex < 0)) {
        if (m_shuffle && m_queue.size() > 1) {
            static std::mt19937 rng{std::random_device{}()};
            std::uniform_int_distribution<int> dist(0, static_cast<int>(m_queue.size()) - 1);
            m_queueIndex = dist(rng);
            m_playingFromQueue = true;
            playTrack(m_queue[m_queueIndex].playlistId, m_queue[m_queueIndex].track);
            return;
        }
        int nextQ = m_playingFromQueue ? (m_queueIndex + 1) : 0;
        if (nextQ >= static_cast<int>(m_queue.size())) {
            if (m_repeatMode == 2) nextQ = 0; // Repeat Queue
            else {
                m_playingFromQueue = false;
                m_player.stop();
                m_hasCurrentTrack = false;
                m_currentTrack = Track{};
                m_currentPlaylistId.clear();
                m_playbackNotice.clear();
                m_fullscreenMode = false;
                return;
            }
        }
        m_queueIndex = nextQ;
        m_playingFromQueue = true;
        playTrack(m_queue[m_queueIndex].playlistId, m_queue[m_queueIndex].track);
        return;
    }

    if (!m_hasCurrentTrack) return;
    auto pl = m_library.getPlaylist(m_currentPlaylistId);
    if (!pl || pl->tracks.empty()) return;

    if (m_shuffle && pl->tracks.size() > 1) {
        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist(0, pl->tracks.size() - 1);
        m_playingFromQueue = false;
        playTrack(pl->id, pl->tracks[dist(rng)]);
        return;
    }

    for (size_t i = 0; i < pl->tracks.size(); ++i) {
        if (pl->tracks[i].id == m_currentTrack.id) {
            size_t nextIdx = (i + 1) % pl->tracks.size();
            if (nextIdx == 0 && m_repeatMode != 2) {
                // Stop completely after the last track unless Repeat All
                m_player.stop();
                m_hasCurrentTrack = false;
                m_currentTrack = Track{};
                m_currentPlaylistId.clear();
                m_playbackNotice.clear();
                m_fullscreenMode = false;
                return;
            }
            m_playingFromQueue = false;
            playTrack(pl->id, pl->tracks[nextIdx]);
            return;
        }
    }
}

void AppUI::playPrev() {
    if (m_playingFromQueue && !m_queue.empty()) {
        int prevQ = m_queueIndex - 1;
        if (prevQ < 0) prevQ = (m_repeatMode == 2) ? static_cast<int>(m_queue.size()) - 1 : 0;
        m_queueIndex = prevQ;
        playTrack(m_queue[m_queueIndex].playlistId, m_queue[m_queueIndex].track);
        return;
    }
    if (!m_hasCurrentTrack) return;
    auto pl = m_library.getPlaylist(m_currentPlaylistId);
    if (!pl || pl->tracks.empty()) return;
    for (size_t i = 0; i < pl->tracks.size(); ++i) {
        if (pl->tracks[i].id == m_currentTrack.id) {
            size_t prevIdx = (i == 0) ? (pl->tracks.size() - 1) : (i - 1);
            playTrack(pl->id, pl->tracks[prevIdx]);
            return;
        }
    }
}

void AppUI::handleShortcuts() {
    // 1. Global Media Keys
    MediaAction mact = m_mediaKeys.pollGlobalMediaKey();
    if (mact == MediaAction::PlayPause && m_player.isOpen()) m_player.togglePlayPause();
    else if (mact == MediaAction::Next) playNext();
    else if (mact == MediaAction::Prev) playPrev();
    else if (mact == MediaAction::Stop) m_player.stop();

    // 2. Keybind rebinding in Settings
    ImGuiIO& io = ImGui::GetIO();
    if (!m_window) return;
    if (m_rebindingSlot >= 0) {
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            m_rebindingSlot = -1;
            return;
        }
        for (int k = 32; k <= 348; ++k) {
            if (k == GLFW_KEY_ESCAPE) continue;
            if (glfwGetKey(m_window, k) == GLFW_PRESS) {
                auto s = m_library.settings();
                switch (m_rebindingSlot) {
                    case 0: s.keyPlayPause = k; break;
                    case 1: s.keyMute = k; break;
                    case 2: s.keyFullscreen = k; break;
                    case 3: s.keyOsFullscreen = k; break;
                    case 4: s.keySeekBack = k; break;
                    case 5: s.keySeekFwd = k; break;
                    case 6: s.keyVolUp = k; break;
                    case 7: s.keyVolDown = k; break;
                }
                m_library.updateSettings(s);
                m_library.save();
                m_rebindingSlot = -1;
                break;
            }
        }
        return;
    }

    if (io.WantTextInput) return;

    auto s = m_library.settings();
    GLFWwindow* win = m_window;
    auto pressedOnce = [&](int key) {
        if (key <= 0) return false;
        static std::unordered_map<int, bool> prev;
        bool down = glfwGetKey(win, key) == GLFW_PRESS;
        bool edge = down && !prev[key];
        prev[key] = down;
        return edge;
    };

    if (pressedOnce(s.keyPlayPause) && m_player.isOpen()) m_player.togglePlayPause();
    if (pressedOnce(s.keyMute)) m_player.setMuted(!m_player.isMuted());
    if (pressedOnce(s.keyFullscreen)) m_fullscreenMode = !m_fullscreenMode;
    if (pressedOnce(s.keyOsFullscreen)) toggleOsFullscreen();
    if (pressedOnce(s.keySeekBack)) m_player.seek(m_player.positionSeconds() - 5.0);
    if (pressedOnce(s.keySeekFwd)) m_player.seek(m_player.positionSeconds() + 5.0);
    if (pressedOnce(s.keyVolUp)) {
        float v = std::min(1.5f, m_player.volume() + 0.05f);
        m_player.setVolume(v);
        m_library.setVolume(v);
    }
    if (pressedOnce(s.keyVolDown)) {
        float v = std::max(0.0f, m_player.volume() - 0.05f);
        m_player.setVolume(v);
        m_library.setVolume(v);
    }
    if (m_fullscreenMode && pressedOnce(GLFW_KEY_ESCAPE)) m_fullscreenMode = false;

    // Ctrl + / - / 0 to scale List / Cards size
    bool ctrl = (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    if (ctrl) {
        bool plus = pressedOnce(GLFW_KEY_EQUAL) || pressedOnce(GLFW_KEY_KP_ADD);
        bool minus = pressedOnce(GLFW_KEY_MINUS) || pressedOnce(GLFW_KEY_KP_SUBTRACT);
        bool reset = pressedOnce(GLFW_KEY_0) || pressedOnce(GLFW_KEY_KP_0);
        if (plus || minus || reset) {
            if (reset) m_uiScale = 1.0f;
            else m_uiScale = std::max(0.20f, m_uiScale + (plus ? 0.12f : -0.12f));
            auto st = m_library.settings();
            st.uiScale = m_uiScale;
            m_library.updateSettings(st);
            m_library.save();
        }
    }
}

void AppUI::update(double dtSeconds) {
    m_thumbsUploadedThisFrame = 0;
    if (m_sync.updateReadyToRestart() && m_window) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }
    if (!m_autoUpdateTriggered && m_sync.hasUpdateAvailable() && !m_sync.isInstallingUpdate() &&
        !m_sync.updateAssetUrl().empty() && m_library.settings().autoInstallUpdates) {
        m_autoUpdateTriggered = true;
        m_sync.installUpdateAsync();
    }
    handleShortcuts();

    // Mouse idle detection for 3s auto-hide in fullscreen (only when app is focused & hovered)
    bool appFocused = m_window && (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0);
    bool appHovered = m_window && (glfwGetWindowAttrib(m_window, GLFW_HOVERED) != 0);
    ImVec2 mp = ImGui::GetMousePos();
    if (appFocused && appHovered && (mp.x != m_lastMousePos.x || mp.y != m_lastMousePos.y || ImGui::IsAnyMouseDown())) {
        m_idleSeconds = 0.0;
        m_lastMousePos = mp;
    } else {
        m_idleSeconds += dtSeconds;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (m_hasPendingOpen) {
            m_hasPendingOpen = false;
            m_currentPlaylistId = m_pendingPlaylistId;
            m_currentTrack = m_pendingTrack;
            m_hasCurrentTrack = true;
            m_preloadedForTrackId.clear();
            applyAudioForTrack(m_currentPlaylistId, m_currentTrack);
            loadCoverImage(m_currentTrack.localThumbnailPath);
            m_stats.recordPlayStart(m_currentTrack);
            m_discord.updatePresence(m_currentTrack, true, 0.0, m_currentTrack.durationSec);
            m_player.open(m_pendingOpenUri, m_pendingPreferAudioOnly);
            m_playbackNotice = "Direct stream (Ad-Free)";
        }
    }

    // Preload next track 12 seconds before current track ends
    if (m_hasCurrentTrack && m_player.isPlaying() && !m_library.offlineMode() &&
        m_preloadedForTrackId != m_currentTrack.id) {
        double dur = m_player.durationSeconds();
        double pos = m_player.positionSeconds();
        if (dur > 15.0 && (dur - pos) <= 12.0) {
            m_preloadedForTrackId = m_currentTrack.id;
            Track nextTr;
            bool foundNext = false;
            if (!m_queue.empty()) {
                nextTr = m_queue.front().track;
                foundNext = true;
            } else {
                auto pl = m_library.getPlaylist(m_currentPlaylistId);
                if (pl && !pl->tracks.empty()) {
                    for (size_t i = 0; i < pl->tracks.size(); ++i) {
                        if (pl->tracks[i].id == m_currentTrack.id && i + 1 < pl->tracks.size()) {
                            nextTr = pl->tracks[i + 1];
                            foundNext = true;
                            break;
                        }
                    }
                }
            }
            if (foundNext && nextTr.localMediaPath.empty() && !nextTr.sourceUrl.empty()) {
                m_sync.resolveDirectStreamAsync(nextTr, nullptr);
            }
        }
    }

    uint32_t vw = 0, vh = 0;
    if (m_player.consumeVideoFrame(m_videoUploadBuf, vw, vh)) {
        m_videoTexture.updateRGBA(m_vk, m_videoUploadBuf.data(), vw, vh);
    }

    if (m_hasCurrentTrack && m_player.isPlaying()) {
        if (m_player.positionSeconds() >= 2.0) m_consecutiveErrors = 0;
        m_stats.accumulateTime(m_currentTrack, dtSeconds);
        m_statsSaveAccum += dtSeconds;
        m_discordAccum += dtSeconds;
        if (m_statsSaveAccum >= 10.0) {
            m_stats.save();
            m_statsSaveAccum = 0.0;
        }
        if (m_discordAccum >= 5.0) {
            m_discord.updatePresence(m_currentTrack, true, m_player.positionSeconds(), m_player.durationSeconds());
            m_discordAccum = 0.0;
        }
    }

    // Auto-sync playlists every 30 minutes (1800s) when online
    if (!m_library.offlineMode()) {
        m_autoSyncAccum += dtSeconds;
        if (m_autoSyncAccum >= 1800.0) {
            m_autoSyncAccum = 0.0;
            if (m_sync.activeTaskCount() == 0) {
                m_sync.syncAllAutoPlaylistsAsync();
            }
        }
    }

    if (m_player.consumeEnded()) {
        if (m_player.positionSeconds() >= 1.5 || m_player.durationSeconds() > 0.0) {
            if (m_repeatMode == 1) playTrack(m_currentPlaylistId, m_currentTrack); // Repeat Same
            else playNext();
        } else {
            ++m_consecutiveErrors;
            if (m_consecutiveErrors < 2) playNext();
            else m_playbackNotice = "Playback stopped (stream unavailable)";
        }
    }
}

void AppUI::render() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##MainRoot", nullptr, rootFlags);
    ImGui::PopStyleVar();

    auto settings = m_library.settings();
    bool mobile = isMobileLayout();
    bool idleHide = m_fullscreenMode && settings.autoHideBarsFullscreen && (m_idleSeconds >= 3.0);
    bool hideTopBar = idleHide && settings.autoHideTopBar;
    bool hideBottomBar = (idleHide && settings.autoHideBottomBar) || (mobile && m_fullscreenMode);

    float barStep = ImGui::GetIO().DeltaTime * 5.0f;
    auto stepAnim = [&](float& val, bool hidden) {
        float tgt = hidden ? 0.0f : 1.0f;
        if (val < tgt) val = std::min(1.0f, val + barStep);
        else if (val > tgt) val = std::max(0.0f, val - barStep);
        return 1.0f - std::pow(1.0f - val, 3.0f);
    };
    float easedTop = stepAnim(m_topBarAnim, hideTopBar);
    float easedBottom = stepAnim(m_bottomBarAnim, hideBottomBar);

    if (easedTop > 0.02f) {
        float topH = 46.0f * easedTop;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
        ImGui::BeginChild("##TopBarWrap", ImVec2(0.0f, topH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPosY(topH - 46.0f);
        renderTopBar();
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
    }

    float playerBarHeight = 76.0f * easedBottom;
    float bodyHeight = ImGui::GetContentRegionAvail().y - (easedBottom > 0.02f ? (playerBarHeight + 4.0f) : 0.0f);
    if (bodyHeight < 180.0f) bodyHeight = 180.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    ImGui::BeginChild("##MiddleBody", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    float contentHeight = ImGui::GetContentRegionAvail().y;

    float targetAnim = m_hasCurrentTrack ? 1.0f : 0.0f;
    float animStep = ImGui::GetIO().DeltaTime * 4.0f;
    if (m_stageAnim < targetAnim) m_stageAnim = std::min(1.0f, m_stageAnim + animStep);
    else if (m_stageAnim > targetAnim) m_stageAnim = std::max(0.0f, m_stageAnim - animStep);
    float easedStage = 1.0f - std::pow(1.0f - m_stageAnim, 3.0f);

    float targetFocus = m_fullscreenMode ? 1.0f : 0.0f;
    float focusStep = ImGui::GetIO().DeltaTime * 4.5f;
    if (m_focusAnim < targetFocus) m_focusAnim = std::min(1.0f, m_focusAnim + focusStep);
    else if (m_focusAnim > targetFocus) m_focusAnim = std::max(0.0f, m_focusAnim - focusStep);
    float easedFocus = 1.0f - std::pow(1.0f - m_focusAnim, 3.0f);

    if (m_focusAnim > 0.01f) {
        float offsetY = contentHeight * (1.0f - easedFocus);
        if (offsetY > 0.5f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
        ImGui::BeginChild("##FullscreenStage", ImVec2(0.0f, std::max(1.0f, contentHeight - offsetY)), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        renderStagePanel(true);
        ImGui::EndChild();
    } else if (m_activeTab == MainTab::Playlists) {
        float winW = ImGui::GetContentRegionAvail().x;
        if (mobile) {
            ImGui::BeginChild("##CenterMain", ImVec2(winW, contentHeight), true);
            renderPlaylistsTab();
            ImGui::EndChild();
        } else {
            float sidebarW = std::clamp(winW * 0.17f, 175.0f, 250.0f);
            float fullStageW = std::clamp(winW * 0.24f, 240.0f, 380.0f);
            float stageW = fullStageW * easedStage;
            float gap = (stageW >= 4.0f) ? 18.0f : 10.0f;
            float centerW = winW - sidebarW - stageW - gap;
            if (centerW < 280.0f) centerW = 280.0f;

            ImGui::BeginChild("##Sidebar", ImVec2(sidebarW, contentHeight), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            renderSidebar();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("##CenterMain", ImVec2(centerW, contentHeight), true);
            renderPlaylistsTab();
            ImGui::EndChild();

            if (stageW >= 4.0f) {
                ImGui::SameLine();
                ImGui::BeginChild("##StagePanel", ImVec2(0.0f, contentHeight), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                renderStagePanel(false);
                ImGui::EndChild();
            }
        }
    } else {
        float winW = ImGui::GetContentRegionAvail().x;
        float fullStageW = mobile ? 0.0f : std::clamp(winW * 0.23f, 230.0f, 360.0f);
        float stageW = fullStageW * easedStage;
        float gap = (stageW >= 4.0f) ? 10.0f : 0.0f;
        float centerW = winW - stageW - gap;
        if (centerW < 220.0f) centerW = 220.0f;

        ImGui::BeginChild("##TabMain", ImVec2(centerW, contentHeight), true);
        switch (m_activeTab) {
            case MainTab::YouTubeMusic: renderSearchTab(Platform::YouTubeMusic); break;
            case MainTab::TikTok:       renderSearchTab(Platform::TikTok); break;
            case MainTab::Queue:        renderQueueTab(); break;
            case MainTab::Downloads:    renderDownloadsTab(); break;
            case MainTab::Stats:        renderStatsTab(); break;
            case MainTab::Settings:     renderSettingsTab(); break;
            default: break;
        }
        ImGui::EndChild();

        if (!mobile && stageW >= 4.0f) {
            ImGui::SameLine();
            ImGui::BeginChild("##StagePanel", ImVec2(0.0f, contentHeight), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            renderStagePanel(false);
            ImGui::EndChild();
        }
    }
    ImGui::EndChild(); // ##MiddleBody

    if (easedBottom > 0.02f) {
        TemplatePalette barPal = activePalette();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(barPal.bgSidebar.x + 0.04f, barPal.bgSidebar.y + 0.04f, barPal.bgSidebar.z + 0.06f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 0.0f));
        ImGui::BeginChild("##PlayerBar", ImVec2(0.0f, playerBarHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        renderPlayerBar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    renderImportModal();
    renderAddToPlaylistModal();
    renderRenameModal();
    renderAudioOverrideModal();
    ImGui::End();
}

void AppUI::toggleOsFullscreen() {
    if (!m_window) return;
    if (!m_osFullscreen) {
        glfwGetWindowPos(m_window, &m_savedWinX, &m_savedWinY);
        glfwGetWindowSize(m_window, &m_savedWinW, &m_savedWinH);

        // Pick whichever monitor contains the center of the window (works on Monitor 1, 2, etc.)
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        GLFWmonitor* best = glfwGetPrimaryMonitor();
        int cx = m_savedWinX + m_savedWinW / 2;
        int cy = m_savedWinY + m_savedWinH / 2;
        for (int i = 0; i < count; ++i) {
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode && cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
                best = monitors[i];
                break;
            }
        }
        const GLFWvidmode* mode = glfwGetVideoMode(best);
        if (mode) {
            glfwSetWindowMonitor(m_window, best, 0, 0, mode->width, mode->height, mode->refreshRate);
            m_osFullscreen = true;
        }
    } else {
        glfwSetWindowMonitor(m_window, nullptr, m_savedWinX, m_savedWinY, m_savedWinW, m_savedWinH, 0);
        m_osFullscreen = false;
    }
}

void AppUI::renderTopBar() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    ImGui::BeginChild("##TopBar", ImVec2(0.0f, 46.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float winW = ImGui::GetWindowWidth();
    bool compactBar = (winW < 1080.0f);

    auto drawHoverRect = [&](ImVec4 col) {
        if (ImGui::IsItemHovered()) {
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                ImGui::GetColorU32(col), 4.0f, 0, 1.5f
            );
        }
    };

    auto drawActiveBar = [&](bool active) {
        if (active) {
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(mn.x + 2.0f, mx.y - 2.0f),
                ImVec2(mx.x - 2.0f, mx.y),
                ImGui::GetColorU32(pal.accent)
            );
        }
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.16f));

    auto textTabBtn = [&](MainTab tab, const char* label) {
        bool active = (m_activeTab == tab);
        ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.accent : pal.textPrimary);
        if (ImGui::Button(label)) {
            m_activeTab = tab;
            m_fullscreenMode = false;
        }
        ImGui::PopStyleColor();
        drawHoverRect(pal.accent);
        drawActiveBar(active);
        ImGui::SameLine(0.0f, compactBar ? 4.0f : 8.0f);
    };

    auto iconTabBtn = [&](MainTab tab, const char* id, const char* icon, const char* tip) {
        bool active = (m_activeTab == tab);
        if (m_icons.button(id, icon, 20.0f, active ? pal.accent : pal.textMuted)) {
            m_activeTab = tab;
            m_fullscreenMode = false;
        }
        drawHoverRect(pal.accent);
        drawActiveBar(active);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine(0.0f, compactBar ? 4.0f : 8.0f);
    };

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
    textTabBtn(MainTab::Playlists, "Accueil");
    iconTabBtn(MainTab::YouTubeMusic, "##tab_ytm", "youtubemusic", "YouTube Music");
    iconTabBtn(MainTab::Downloads, "##tab_dl", "download", "Downloads");
    textTabBtn(MainTab::Queue, "File attente");

    // Right: Responsive aligned utility controls
    float rightW = compactBar ? 245.0f : 285.0f;
    float rightX = winW - rightW;
    if (rightX > ImGui::GetCursorPosX()) {
        ImGui::SameLine(rightX);
    }
    ImGui::SetNextItemWidth(compactBar ? 95.0f : 120.0f);
    auto stTop = m_library.settings();
    std::vector<std::string> themeLabels = {
        "YT Music", "Spotify", "TikTok", "Minimal",
        "CH: Neon", "CH: Mint", "CH: Sunset", "CH: Emerald"
    };
    for (const auto& ct : stTop.customThemes) themeLabels.push_back(ct.name);
    int curTpl = (stTop.activeCustomThemeIndex >= 0 && stTop.activeCustomThemeIndex < static_cast<int>(stTop.customThemes.size()))
        ? (8 + stTop.activeCustomThemeIndex)
        : static_cast<int>(stTop.activeTemplate);
    if (ImGui::BeginCombo("##TemplateCombo", themeLabels[curTpl].c_str())) {
        for (int ti = 0; ti < static_cast<int>(themeLabels.size()); ++ti) {
            bool sel = (curTpl == ti);
            if (ImGui::Selectable((themeLabels[ti] + "##tpl_" + std::to_string(ti)).c_str(), sel)) {
                if (ti < 8) {
                    stTop.activeCustomThemeIndex = -1;
                    stTop.activeTemplate = static_cast<GuiTemplate>(ti);
                } else {
                    stTop.activeCustomThemeIndex = ti - 8;
                }
                m_library.updateSettings(stTop);
                m_library.save();
                applyPaletteStyle(activePalette(), m_vk);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (m_icons.button("##top_settings", "settings", 18.0f, m_activeTab == MainTab::Settings ? pal.accent : pal.textPrimary)) {
        m_activeTab = MainTab::Settings;
        m_fullscreenMode = false;
    }
    drawHoverRect(pal.accent);
    ImGui::SameLine(0.0f, 6.0f);

    bool offline = m_library.offlineMode();
    bool netOk = m_sync.isNetworkAvailable();
    ImVec4 statusCol = offline
        ? ImVec4(0.92f, 0.45f, 0.20f, 1.0f)
        : (netOk ? ImVec4(0.25f, 0.85f, 0.48f, 1.0f) : ImVec4(0.85f, 0.72f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, statusCol);
    if (ImGui::Button(offline ? (compactBar ? "Off" : "Hors Ligne") : (compactBar ? "On" : "En Ligne"))) {
        m_library.setOfflineMode(!offline);
        m_library.save();
        if (offline) {
            m_sync.refreshNetworkStatusAsync();
            m_sync.syncAllAutoPlaylistsAsync();
            m_autoSyncAccum = 0.0;
        }
    }
    ImGui::PopStyleColor();
    drawHoverRect(statusCol);

    if (m_sync.hasUpdateAvailable()) {
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.accent);
        if (m_sync.isInstallingUpdate()) {
            ImGui::Button("MAJ...");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", m_sync.updateStatus().c_str());
            }
        } else {
            if (ImGui::Button("MAJ")) {
                if (!m_sync.updateAssetUrl().empty()) {
                    m_sync.installUpdateAsync();
                } else {
                    ShellExecuteA(nullptr, "open", m_sync.updateUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            drawHoverRect(pal.accent);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(!m_sync.updateAssetUrl().empty()
                    ? "Installer la mise à jour v%s"
                    : "Nouvelle version v%s disponible (ouvrir GitHub)",
                    m_sync.latestVersion().c_str());
            }
        }
        ImGui::PopStyleColor();
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    ImGui::EndChild();
}

void AppUI::renderSidebar() {
    TemplatePalette pal = activePalette();
    auto playlists = m_library.playlistSummaries();

    float scrollH = std::max(40.0f, ImGui::GetContentRegionAvail().y - 48.0f);
    ImGui::BeginChild("##SidebarScroll", ImVec2(0.0f, scrollH), false);

    m_icons.draw("heart_filled", 18.0f, pal.accent);
    ImGui::SameLine();
    if (ImGui::Selectable("Titres Favoris", m_showLikedOnlyView)) {
        m_showLikedOnlyView = true;
        m_showOfflineOnlyView = false;
    }

    m_icons.draw("downloaded", 18.0f, pal.accentSecondary);
    ImGui::SameLine();
    if (ImGui::Selectable("Offline Downloads", m_showOfflineOnlyView)) {
        m_showOfflineOnlyView = true;
        m_showLikedOnlyView = false;
    }

    if (m_library.canUndoDeletePlaylist()) {
        ImGui::Spacing();
        if (ImGui::Button("Annuler suppression (Undo)", ImVec2(-1.0f, 0.0f))) {
            std::string restored = m_library.undoDeletePlaylist();
            if (!restored.empty()) {
                m_selectedPlaylistId = restored;
                m_showOfflineOnlyView = false;
                m_showLikedOnlyView = false;
                m_library.save();
            }
        }
    }

    // Favorite Playlists section at top
    auto drawPlaylistCtx = [&](const PlaylistSummary& pl, const char* popupId) {
        if (ImGui::BeginPopupContextItem(popupId)) {
            if (ImGui::MenuItem(pl.favorite ? "Retirer des favoris" : "Ajouter aux favoris")) {
                m_library.togglePlaylistFavorite(pl.id);
                m_library.save();
            }
            if (pl.platform != Platform::Local) {
                auto fullPl = m_library.getPlaylist(pl.id);
                ImGui::Separator();
                bool offline = m_library.offlineMode();
                if (ImGui::MenuItem("Synchroniser", nullptr, false, !offline && fullPl && !fullPl->sourceUrl.empty())) {
                    m_sync.syncPlaylistAsync(pl.id);
                }
                if (ImGui::MenuItem("Tout telecharger", nullptr, false, !offline && pl.trackCount > 0)) {
                    m_sync.queuePlaylistDownload(pl.id, fullPl ? fullPl->preferVideo : false);
                }
                if (fullPl) {
                    bool autoSync = fullPl->autoSync, autoDl = fullPl->autoDownload, prefVid = fullPl->preferVideo;
                    bool changed = false;
                    if (ImGui::MenuItem("Auto-Sync", nullptr, &autoSync)) changed = true;
                    if (ImGui::MenuItem("Auto-Download", nullptr, &autoDl)) changed = true;
                    if (ImGui::MenuItem("Preferer Video", nullptr, &prefVid)) changed = true;
                    if (changed) {
                        m_library.setPlaylistOptions(pl.id, autoSync, autoDl, prefVid);
                        m_library.save();
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Audio / dB & EQ")) {
                auto fullPl = m_library.getPlaylist(pl.id);
                m_showAudioModal = true;
                m_audioModalIsPlaylist = true;
                m_audioModalPlaylistId = pl.id;
                m_audioModalLabel = "Playlist: " + pl.name;
                if (fullPl) m_audioModalData = fullPl->audioOverride;
            }
            if (ImGui::MenuItem("Renommer")) {
                m_showRenameModal = true;
                m_renameIsPlaylist = true;
                m_renamePlaylistId = pl.id;
                std::snprintf(m_renameTitleBuf, sizeof(m_renameTitleBuf), "%s", pl.name.c_str());
            }
            if (ImGui::BeginMenu("Catégorie")) {
                const Platform plats[] = { Platform::YouTubeMusic, Platform::YouTube, Platform::TikTok, Platform::Local };
                for (Platform tp : plats) {
                    if (ImGui::MenuItem(platformName(tp), nullptr, pl.platform == tp)) {
                        m_library.movePlaylistToPlatform(pl.id, tp);
                        m_library.save();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Supprimer fichiers locaux")) {
                m_library.clearPlaylistLocalFiles(pl.id);
                m_library.save();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Supprimer la playlist")) {
                m_library.deletePlaylist(pl.id);
                m_library.save();
                if (m_selectedPlaylistId == pl.id) {
                    auto rem = m_library.playlistsCopy();
                    m_selectedPlaylistId = rem.empty() ? "" : rem.front().id;
                }
            }
            ImGui::EndPopup();
        }
    };

    auto attachPlaylistDnd = [&](const PlaylistSummary& pl, bool allowPlatformDrop) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("DND_PLAYLIST", pl.id.c_str(), pl.id.size() + 1);
            ImGui::Text("%s", pl.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PLAYLIST")) {
                std::string draggedId(static_cast<const char*>(payload->Data));
                if (allowPlatformDrop) m_library.movePlaylistToPlatform(draggedId, pl.platform);
                m_library.reorderPlaylist(draggedId, pl.id);
                m_library.save();
            }
            ImGui::EndDragDropTarget();
        }
    };

    std::vector<PlaylistSummary> favPlaylists;
    for (const auto& p : playlists) {
        if (p.favorite) favPlaylists.push_back(p);
    }
    if (!favPlaylists.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("PLAYLISTS FAVORITES");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PLAYLIST")) {
                std::string draggedId(static_cast<const char*>(payload->Data));
                auto pOpt = m_library.getPlaylist(draggedId);
                if (pOpt && !pOpt->favorite) {
                    m_library.togglePlaylistFavorite(draggedId);
                    m_library.save();
                }
            }
            ImGui::EndDragDropTarget();
        }
        for (const auto& pl : favPlaylists) {
            bool selected = (!m_showOfflineOnlyView && !m_showLikedOnlyView && m_selectedPlaylistId == pl.id);
            m_icons.draw("heart_filled", 14.0f, pal.accent);
            ImGui::SameLine();
            std::string label = pl.name + " (" + std::to_string(pl.trackCount) + ")##fav_" + pl.id;
            if (ImGui::Selectable(label.c_str(), selected)) {
                m_showOfflineOnlyView = false;
                m_showLikedOnlyView = false;
                m_selectedPlaylistId = pl.id;
                auto st = m_library.settings();
                st.selectedPlaylistId = pl.id;
                m_library.updateSettings(st);
                m_library.save();
            }
            attachPlaylistDnd(pl, false);
            drawPlaylistCtx(pl, ("##sb_fav_ctx_" + pl.id).c_str());
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("PLATEFORMES & PLAYLISTS");
    ImGui::Spacing();

    const Platform order[] = { Platform::YouTubeMusic, Platform::YouTube, Platform::TikTok, Platform::Local };

    for (Platform plat : order) {
        std::vector<PlaylistSummary> group;
        for (const auto& p : playlists) {
            if (p.platform == plat) group.push_back(p);
        }
        if (group.empty()) continue;

        m_icons.draw(platformIconKey(plat), 18.0f, pal.accent);
        ImGui::SameLine();
        bool nodeOpen = ImGui::TreeNodeEx(platformName(plat), ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PLAYLIST")) {
                std::string draggedId(static_cast<const char*>(payload->Data));
                m_library.movePlaylistToPlatform(draggedId, plat);
                m_library.save();
            }
            ImGui::EndDragDropTarget();
        }
        if (nodeOpen) {
            for (const auto& pl : group) {
                bool selected = (!m_showOfflineOnlyView && !m_showLikedOnlyView && m_selectedPlaylistId == pl.id);
                m_icons.draw("playlist", 15.0f, selected ? pal.accent : pal.textMuted);
                ImGui::SameLine();
                std::string label = pl.name + " (" + std::to_string(pl.trackCount) + ")##" + pl.id;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    m_showOfflineOnlyView = false;
                    m_showLikedOnlyView = false;
                    m_selectedPlaylistId = pl.id;
                    auto st = m_library.settings();
                    st.selectedPlaylistId = pl.id;
                    m_library.updateSettings(st);
                    m_library.save();
                }
                attachPlaylistDnd(pl, true);
                drawPlaylistCtx(pl, ("##sb_ctx_" + pl.id).c_str());
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::BeginPopupContextWindow("##SidebarEmptyCtx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Nouvelle playlist")) {
            std::snprintf(m_newPlNameBuf, sizeof(m_newPlNameBuf), "Nouvelle playlist");
            m_newPlPlatIdx = 0;
            m_showCreatePlaylistModal = true;
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("+ Importer", ImVec2(-1.0f, 30.0f))) {
        m_showImportModal = true;
    }
}

void AppUI::renderPlaylistsTab() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());

    m_icons.draw("search", 18.0f, pal.textMuted);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##PlFilter", "Filter playlist tracks...", m_searchBuf, sizeof(m_searchBuf));
    std::string filter(m_searchBuf);

    if (m_showLikedOnlyView) {
        ImGui::Separator();
        m_icons.draw("heart_filled", 20.0f, pal.accent);
        ImGui::SameLine();
        ImGui::Text("Titres Favoris");
        auto lists = m_library.playlistsCopy();
        if (ImGui::BeginTable("##LikedTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Titre & Auteur", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Plateforme", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableHeadersRow();
            for (const auto& pl : lists) {
                for (const auto& t : pl.tracks) {
                    if (!t.liked) continue;
                    if (!matchesFilter(t, filter)) continue;
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 52.0f);
                    ImGui::TableNextColumn();
                    ImGui::Image(getTrackThumb(t), ImVec2(44.0f, 44.0f));
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", t.title.c_str());
                    ImGui::TextDisabled("%s", t.artist.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", platformName(t.platform));
                    ImGui::TableNextColumn();
                    if (m_icons.button(("##play_lk_" + t.id).c_str(), "play", 18.0f, pal.accent)) playTrack(pl.id, t);
                    ImGui::SameLine();
                    if (m_icons.button(("##q_lk_" + t.id).c_str(), "queue", 18.0f, pal.textPrimary)) enqueueTrack(pl.id, t);
                    ImGui::SameLine();
                    if (m_icons.button(("##unlk_" + t.id).c_str(), "heart_filled", 18.0f, pal.accent)) {
                        m_library.toggleTrackLike(pl.id, t.id);
                        m_library.save();
                    }
                }
            }
            ImGui::EndTable();
        }
        return;
    }

    if (m_showOfflineOnlyView) {
        ImGui::Separator();
        ImGui::Text("Offline Library (Downloaded Music & Video)");
        auto lists = m_library.playlistsCopy();
        if (ImGui::BeginTable("##OfflineTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Title / Artist", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Platform", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableHeadersRow();
            for (const auto& pl : lists) {
                for (const auto& t : pl.tracks) {
                    if (t.localMediaPath.empty() || !fs::exists(t.localMediaPath)) continue;
                    if (!matchesFilter(t, filter)) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Image(getTrackThumb(t), ImVec2(40.0f, 40.0f));
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", t.title.c_str());
                    ImGui::TextDisabled("%s", t.artist.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", platformName(t.platform));
                    ImGui::TableNextColumn();
                    if (m_icons.button(("##play_off_" + t.id).c_str(), "play", 16.0f, pal.accent)) playTrack(pl.id, t);
                    ImGui::SameLine();
                    if (m_icons.button(("##q_off_" + t.id).c_str(), "queue", 16.0f, pal.textPrimary)) enqueueTrack(pl.id, t);
                }
            }
            ImGui::EndTable();
        }
        return;
    }

    auto plOpt = m_library.getPlaylist(m_selectedPlaylistId);
    if (!plOpt) {
        ImGui::TextDisabled("Select a playlist on the left or import a URL.");
        return;
    }
    Playlist pl = *plOpt;

    ImGui::SameLine();
    m_icons.draw(platformIconKey(pl.platform), 20.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("%s (%zu)", pl.name.c_str(), pl.tracks.size());
    if (ImGui::BeginPopupContextItem("##pl_hdr_ctx")) {
        if (ImGui::MenuItem(pl.favorite ? "Retirer des favoris" : "Ajouter aux favoris")) {
            m_library.togglePlaylistFavorite(pl.id);
            m_library.save();
        }
        if (pl.platform != Platform::Local) {
            ImGui::Separator();
            bool offlineCtx = m_library.offlineMode();
            if (ImGui::MenuItem("Synchroniser", nullptr, false, !offlineCtx && !pl.sourceUrl.empty())) {
                m_sync.syncPlaylistAsync(pl.id);
            }
            if (ImGui::MenuItem("Tout telecharger", nullptr, false, !offlineCtx && !pl.tracks.empty())) {
                m_sync.queuePlaylistDownload(pl.id, pl.preferVideo);
            }
            bool autoSync = pl.autoSync, autoDl = pl.autoDownload, prefVid = pl.preferVideo;
            bool changed = false;
            if (ImGui::MenuItem("Auto-Sync", nullptr, &autoSync)) changed = true;
            if (ImGui::MenuItem("Auto-Download", nullptr, &autoDl)) changed = true;
            if (ImGui::MenuItem("Preferer Video", nullptr, &prefVid)) changed = true;
            if (changed) {
                m_library.setPlaylistOptions(pl.id, autoSync, autoDl, prefVid);
                m_library.save();
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Audio / dB & EQ")) {
            m_showAudioModal = true;
            m_audioModalIsPlaylist = true;
            m_audioModalPlaylistId = pl.id;
            m_audioModalLabel = "Playlist: " + pl.name;
            m_audioModalData = pl.audioOverride;
        }
        if (ImGui::MenuItem("Renommer")) {
            m_showRenameModal = true;
            m_renameIsPlaylist = true;
            m_renamePlaylistId = pl.id;
            std::snprintf(m_renameTitleBuf, sizeof(m_renameTitleBuf), "%s", pl.name.c_str());
        }
        if (ImGui::BeginMenu("Catégorie##pl_hdr_cat")) {
            const Platform plats[] = { Platform::YouTubeMusic, Platform::YouTube, Platform::TikTok, Platform::Local };
            for (Platform tp : plats) {
                if (ImGui::MenuItem(platformName(tp), nullptr, pl.platform == tp)) {
                    m_library.movePlaylistToPlatform(pl.id, tp);
                    m_library.save();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Supprimer fichiers locaux")) {
            m_library.clearPlaylistLocalFiles(pl.id);
            m_library.save();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Supprimer la playlist")) {
            m_library.deletePlaylist(pl.id);
            m_library.save();
            auto lists = m_library.playlistsCopy();
            m_selectedPlaylistId = lists.empty() ? "" : lists.front().id;
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (m_icons.button("##pl_fav", pl.favorite ? "heart_filled" : "heart", 18.0f, pl.favorite ? pal.accent : pal.textMuted)) {
        m_library.togglePlaylistFavorite(pl.id);
        m_library.save();
    }

    ImGui::SameLine();
    const char* viewModes[] = { "Liste", "Grandes Cartes" };
    ImGui::SetNextItemWidth(125.0f);
    if (ImGui::Combo("##ViewMode", &m_viewMode, viewModes, 2)) {
        auto st = m_library.settings();
        st.viewMode = m_viewMode;
        m_library.updateSettings(st);
        m_library.save();
    }

    ImGui::Separator();

    bool offline = m_library.offlineMode();

    if (m_viewMode == 1) {
        // Grandes Cartes (YouTube Music Home/Album Grid) - responsive, never overflows right
        ImGui::BeginChild("##GridScroll", ImVec2(0.0f, 0.0f), false);
        float avail = ImGui::GetContentRegionAvail().x - 18.0f; // reserve scrollbar width
        float targetCardW = 165.0f * m_uiScale;
        float spacing = 12.0f;
        int cols = std::max(1, static_cast<int>((avail + spacing) / (targetCardW + spacing)));
        float cardW = std::floor((avail - spacing * (cols - 1)) / static_cast<float>(cols));
        float imgSide = std::max(64.0f, cardW - 6.0f);
        int colIdx = 0;
        for (size_t i = 0; i < pl.tracks.size(); ++i) {
            const Track& t = pl.tracks[i];
            if (!matchesFilter(t, filter)) continue;
            bool isDownloaded = (t.downloadState == DownloadState::Downloaded && !t.localMediaPath.empty());
            bool unavailableOffline = offline && !isDownloaded;

            if (colIdx > 0) ImGui::SameLine(0.0f, spacing);
            if (!ImGui::IsRectVisible(ImVec2(cardW, imgSide + 54.0f))) {
                ImGui::Dummy(ImVec2(cardW, imgSide + 54.0f));
                colIdx = (colIdx + 1) % cols;
                continue;
            }
            ImGui::BeginGroup();
            ImGui::BeginDisabled(unavailableOffline);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            if (ImGui::ImageButton(("##gbtn_" + t.id).c_str(), getTrackThumb(t), ImVec2(imgSide, imgSide))) {
                playTrack(pl.id, t);
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            if (ImGui::BeginPopupContextItem(("##gctx_" + t.id).c_str())) {
                if (ImGui::MenuItem("Écouter maintenant")) playTrack(pl.id, t);
                if (ImGui::MenuItem("Ajouter à la file (+ Queue)")) enqueueTrack(pl.id, t);
                if (!offline && !t.sourceUrl.empty()) {
                    if (ImGui::MenuItem("Télécharger Audio (M4A)")) m_sync.queueDirectDownload(t, pl.id, false);
                    if (ImGui::MenuItem("Télécharger Vidéo (MP4)")) m_sync.queueDirectDownload(t, pl.id, true);
                    if (ImGui::MenuItem("Réinstaller / Fix")) m_sync.reinstallTrack(pl.id, t, pl.preferVideo);
                }
                ImGui::EndPopup();
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - 4.0f);
            ImGui::Text("%s", t.title.c_str());
            ImGui::TextDisabled("%s", t.artist.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndDisabled();
            ImGui::EndGroup();

            colIdx = (colIdx + 1) % cols;
        }
        ImGui::EndChild();
        return;
    }

    float rowH = 56.0f * m_uiScale;
    float thumbSz = 48.0f * m_uiScale;

    if (ImGui::BeginTable("##TrackTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_WidthFixed, thumbSz + 12.0f);
        ImGui::TableSetupColumn("Titre & Auteur", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Durée", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 165.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < pl.tracks.size(); ++i) {
            const Track& t = pl.tracks[i];
            if (!matchesFilter(t, filter)) continue;

            bool isDownloaded = (t.downloadState == DownloadState::Downloaded && !t.localMediaPath.empty());
            bool unavailableOffline = offline && !isDownloaded;

            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::BeginDisabled(unavailableOffline);

            ImGui::TableNextColumn();
            ImGui::Image(getTrackThumb(t), ImVec2(thumbSz, thumbSz));

            ImGui::TableNextColumn();
            bool isCurrent = (m_hasCurrentTrack && m_currentTrack.id == t.id);
            if (ImGui::Selectable(("##sel_" + t.id).c_str(), isCurrent,
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, thumbSz))) {
                if (ImGui::IsMouseDoubleClicked(0)) playTrack(pl.id, t);
            }
            if (ImGui::BeginPopupContextItem(("##ctx_" + t.id).c_str())) {
                if (ImGui::MenuItem("Écouter maintenant")) playTrack(pl.id, t);
                if (ImGui::MenuItem("Ajouter à la file d'attente (+ Queue)")) enqueueTrack(pl.id, t);
                if (ImGui::MenuItem("Renommer titre / auteur")) {
                    m_showRenameModal = true;
                    m_renameIsPlaylist = false;
                    m_renamePlaylistId = pl.id;
                    m_renameTrackId = t.id;
                    std::snprintf(m_renameTitleBuf, sizeof(m_renameTitleBuf), "%s", t.title.c_str());
                    std::snprintf(m_renameArtistBuf, sizeof(m_renameArtistBuf), "%s", t.artist.c_str());
                }
                if (ImGui::MenuItem("Audio / dB & EQ (ce titre)")) {
                    m_showAudioModal = true;
                    m_audioModalIsPlaylist = false;
                    m_audioModalPlaylistId = pl.id;
                    m_audioModalTrackId = t.id;
                    m_audioModalLabel = "Titre: " + t.title;
                    m_audioModalData = t.audioOverride;
                }
                ImGui::Separator();
                if (!offline && !t.sourceUrl.empty()) {
                    if (ImGui::MenuItem("Télécharger Audio (M4A)")) m_sync.queueDirectDownload(t, pl.id, false);
                    if (ImGui::MenuItem("Télécharger Vidéo (MP4)")) m_sync.queueDirectDownload(t, pl.id, true);
                    if (ImGui::MenuItem("Réinstaller / Fix (Re-télécharger)")) {
                        m_sync.reinstallTrack(pl.id, t, pl.preferVideo);
                    }
                }
                if (isDownloaded && ImGui::MenuItem("Supprimer le fichier local")) {
                    m_library.clearTrackLocalMedia(pl.id, t.id);
                    m_library.save();
                }
                ImGui::Separator();
                if (i > 0 && ImGui::MenuItem("Monter dans la playlist")) {
                    m_library.moveTrackInPlaylist(pl.id, i, i - 1);
                    m_library.save();
                }
                if (i + 1 < pl.tracks.size() && ImGui::MenuItem("Descendre dans la playlist")) {
                    m_library.moveTrackInPlaylist(pl.id, i, i + 1);
                    m_library.save();
                }
                if (ImGui::MenuItem("Retirer de la playlist")) {
                    m_library.removeTrackFromPlaylist(pl.id, t.id);
                    m_library.save();
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::BeginGroup();
            if (isCurrent) ImGui::TextColored(pal.accent, "%s", t.title.c_str());
            else ImGui::Text("%s", t.title.c_str());
            ImGui::TextDisabled("%s", t.artist.c_str());
            ImGui::EndGroup();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", t.durationSec > 0 ? formatTime(t.durationSec).c_str() : "--:--");

            ImGui::TableNextColumn();
            if (m_icons.button(("##play_" + t.id).c_str(), "play", 20.0f, pal.accent)) playTrack(pl.id, t);
            ImGui::SameLine(0.0f, 6.0f);
            if (m_icons.button(("##q_" + t.id).c_str(), "queue", 20.0f, pal.textPrimary)) enqueueTrack(pl.id, t);
            ImGui::SameLine(0.0f, 6.0f);
            if (isDownloaded) {
                if (m_icons.button(("##fix_" + t.id).c_str(), "downloaded", 20.0f, pal.accentSecondary)) {
                    if (!offline && !t.sourceUrl.empty()) m_sync.reinstallTrack(pl.id, t, pl.preferVideo);
                }
            } else if (t.downloadState == DownloadState::Downloading || t.downloadState == DownloadState::Queued) {
                ImGui::TextDisabled("...");
            } else if (!offline && !t.sourceUrl.empty()) {
                if (m_icons.button(("##dl_" + t.id).c_str(), "download", 20.0f, pal.textPrimary)) {
                    m_sync.queueDirectDownload(t, pl.id, pl.preferVideo);
                }
            } else {
                ImGui::TextDisabled("N/A");
            }

            ImGui::EndDisabled();
        }
        ImGui::EndTable();
    }
}

void AppUI::renderSearchTab(Platform plat) {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    bool isTT = (plat == Platform::TikTok);
    char* buf = isTT ? m_ttSearchBuf : m_ytmSearchBuf;
    size_t bufSz = isTT ? sizeof(m_ttSearchBuf) : sizeof(m_ytmSearchBuf);
    bool& searching = isTT ? m_ttSearching : m_ytmSearching;
    int& limit = isTT ? m_ttSearchLimit : m_ytmSearchLimit;
    std::vector<Track>& results = isTT ? m_ttResults : m_ytmResults;

    m_icons.draw(platformIconKey(plat), 22.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("%s Direct Search & Listen (Ad-Free)", platformName(plat));
    ImGui::Separator();

    ImGui::SetNextItemWidth(380.0f);
    bool enter = ImGui::InputTextWithHint(
        isTT ? "##TTSearch" : "##YTMSearch",
        isTT ? "Rechercher sur TikTok ou coller un lien vidéo/photo..." : "Rechercher un titre, artiste ou coller un lien YT Music...",
        buf, bufSz, ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::SameLine();
    ImGui::BeginDisabled(m_library.offlineMode() || searching);
    auto triggerSearch = [&](int lim) {
        searching = true;
        std::string query(buf);
        m_sync.searchPlatformAsync(plat, query, lim, [this, isTT](bool /*ok*/, const std::vector<Track>& res, const std::string&) {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            if (isTT) { m_ttResults = res; m_ttSearching = false; }
            else { m_ytmResults = res; m_ytmSearching = false; }
        });
    };
    if ((ImGui::Button("Rechercher") || enter) && buf[0] != '\0') {
        limit = 12;
        triggerSearch(limit);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const char* searchViewModes[] = { "Liste", "Grandes Cartes" };
    ImGui::SetNextItemWidth(125.0f);
    if (ImGui::Combo("##SearchViewMode", &m_viewMode, searchViewModes, 2)) {
        auto st = m_library.settings();
        st.viewMode = m_viewMode;
        m_library.updateSettings(st);
        m_library.save();
    }
    if (searching) {
        ImGui::SameLine();
        ImGui::TextDisabled("Recherche en cours...");
    }

    ImGui::Separator();

    std::lock_guard<std::mutex> lock(m_searchMutex);
    float tableH = ImGui::GetContentRegionAvail().y - 38.0f;
    if (tableH < 120.0f) tableH = 120.0f;
    if (m_viewMode == 1) {
        ImGui::BeginChild("##SearchGridScroll", ImVec2(0.0f, tableH), false);
        float avail = ImGui::GetContentRegionAvail().x - 18.0f;
        float targetCardW = 165.0f * m_uiScale;
        float spacing = 12.0f;
        int cols = std::max(1, static_cast<int>((avail + spacing) / (targetCardW + spacing)));
        float cardW = std::floor((avail - spacing * (cols - 1)) / static_cast<float>(cols));
        float imgSide = std::max(64.0f, cardW - 6.0f);
        int colIdx = 0;
        for (size_t i = 0; i < results.size(); ++i) {
            const Track& t = results[i];
            std::string sid = std::to_string(i);
            if (colIdx > 0) ImGui::SameLine(0.0f, spacing);
            if (!ImGui::IsRectVisible(ImVec2(cardW, imgSide + 54.0f))) {
                ImGui::Dummy(ImVec2(cardW, imgSide + 54.0f));
                colIdx = (colIdx + 1) % cols;
                continue;
            }
            ImGui::BeginGroup();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            if (ImGui::ImageButton(("##sgbtn_" + sid).c_str(), getTrackThumb(t), ImVec2(imgSide, imgSide))) {
                playTrack("", t);
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            if (ImGui::BeginPopupContextItem(("##sgctx_" + sid).c_str())) {
                if (ImGui::MenuItem("Écouter maintenant")) playTrack("", t);
                if (ImGui::MenuItem("Ajouter à la file d'attente (+ Queue)")) enqueueTrack("", t);
                if (ImGui::MenuItem("Ajouter à une playlist...")) {
                    m_addToPlTrack = t;
                    m_addToPlNewNameBuf[0] = '\0';
                    m_addToPlNewPlatIdx = (t.platform == Platform::TikTok) ? 2 : 0;
                    m_showAddToPlaylistModal = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Télécharger Audio (M4A)")) m_sync.queueDirectDownload(t, m_selectedPlaylistId, false);
                if (ImGui::MenuItem("Télécharger Vidéo (MP4)")) m_sync.queueDirectDownload(t, m_selectedPlaylistId, true);
                ImGui::EndPopup();
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - 4.0f);
            ImGui::Text("%s", t.title.c_str());
            ImGui::TextDisabled("%s", t.artist.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            colIdx = (colIdx + 1) % cols;
        }
        ImGui::EndChild();
    } else if (ImGui::BeginTable(isTT ? "##TTTable" : "##YTMTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, tableH))) {
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Titre & Auteur", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Durée", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 175.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < results.size(); ++i) {
            const Track& t = results[i];
            std::string sid = std::to_string(i);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 56.0f);
            ImGui::TableNextColumn();
            ImGui::Image(getTrackThumb(t), ImVec2(48.0f, 48.0f));
            ImGui::TableNextColumn();
            if (ImGui::Selectable(("##ssel_" + sid).c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, 48.0f))) {
                if (ImGui::IsMouseDoubleClicked(0)) playTrack("", t);
            }
            if (ImGui::BeginPopupContextItem(("##sctx_" + sid).c_str())) {
                if (ImGui::MenuItem("Écouter maintenant")) playTrack("", t);
                if (ImGui::MenuItem("Ajouter à la file d'attente (+ Queue)")) enqueueTrack("", t);
                if (ImGui::MenuItem("Ajouter à une playlist...")) {
                    m_addToPlTrack = t;
                    m_addToPlNewNameBuf[0] = '\0';
                    m_addToPlNewPlatIdx = (t.platform == Platform::TikTok) ? 2 : 0;
                    m_showAddToPlaylistModal = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Télécharger Audio (M4A)")) m_sync.queueDirectDownload(t, m_selectedPlaylistId, false);
                if (ImGui::MenuItem("Télécharger Vidéo (MP4)")) m_sync.queueDirectDownload(t, m_selectedPlaylistId, true);
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::BeginGroup();
            ImGui::Text("%s", t.title.c_str());
            ImGui::TextDisabled("%s", t.artist.c_str());
            ImGui::EndGroup();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", t.durationSec > 0 ? formatTime(t.durationSec).c_str() : "--:--");
            ImGui::TableNextColumn();

            if (m_icons.button(("##splay_" + sid).c_str(), "play", 20.0f, pal.accent)) playTrack("", t);
            ImGui::SameLine(0.0f, 5.0f);
            if (m_icons.button(("##squeue_" + sid).c_str(), "queue", 20.0f, pal.textPrimary)) enqueueTrack("", t);
            ImGui::SameLine(0.0f, 5.0f);
            if (m_icons.button(("##sadd_" + sid).c_str(), "add", 20.0f, pal.accentSecondary)) {
                m_addToPlTrack = t;
                m_addToPlNewNameBuf[0] = '\0';
                m_addToPlNewPlatIdx = (t.platform == Platform::TikTok) ? 2 : 0;
                m_showAddToPlaylistModal = true;
            }
            ImGui::SameLine(0.0f, 5.0f);
            if (m_icons.button(("##sdl_" + sid).c_str(), "download", 20.0f, pal.textMuted)) {
                m_sync.queueDirectDownload(t, m_selectedPlaylistId, isTT);
            }
        }
        ImGui::EndTable();
    }

    if (!results.empty() && buf[0] != '\0') {
        ImGui::BeginDisabled(m_library.offlineMode() || searching);
        if (ImGui::Button("Voir Plus (+12)", ImVec2(-1.0f, 30.0f))) {
            limit += 12;
            triggerSearch(limit);
        }
        ImGui::EndDisabled();
    }
}

void AppUI::renderQueueTab() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    m_icons.draw("queue", 22.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("File d'attente en RAM (%zu)", m_queue.size());
    ImGui::SameLine();
    if (ImGui::Button("Lire File")) {
        if (!m_queue.empty()) {
            m_queueIndex = 0;
            m_playingFromQueue = true;
            playTrack(m_queue[0].playlistId, m_queue[0].track);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Queue")) {
        m_queue.clear();
        m_queueIndex = -1;
        m_playingFromQueue = false;
    }
    ImGui::Separator();

    if (m_queue.empty()) {
        ImGui::TextDisabled("Queue is empty. Click the Queue icon on any track to add it.");
        return;
    }

    if (ImGui::BeginTable("##QueueTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Thumb", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Title / Artist", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < m_queue.size(); ++i) {
            auto& q = m_queue[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Image(getTrackThumb(q.track), ImVec2(40.0f, 40.0f));
            ImGui::TableNextColumn();
            ImGui::Text("%s", q.track.title.c_str());
            ImGui::TextDisabled("%s", q.track.artist.c_str());
            ImGui::TableNextColumn();
            if (i > 0 && m_icons.button(("##qup_" + std::to_string(i)).c_str(), "arrow_up", 14.0f, pal.textMuted)) {
                std::swap(m_queue[i], m_queue[i - 1]);
            }
            ImGui::SameLine();
            if (i + 1 < m_queue.size() && m_icons.button(("##qdn_" + std::to_string(i)).c_str(), "arrow_down", 14.0f, pal.textMuted)) {
                std::swap(m_queue[i], m_queue[i + 1]);
            }
            ImGui::TableNextColumn();
            if (m_icons.button(("##qgo_" + std::to_string(i)).c_str(), "play", 16.0f, pal.accent)) {
                m_queueIndex = static_cast<int>(i);
                m_playingFromQueue = true;
                playTrack(q.playlistId, q.track);
                break;
            }
            ImGui::SameLine();
            if (m_icons.button(("##qrm_" + std::to_string(i)).c_str(), "close", 16.0f, pal.textMuted)) {
                m_queue.erase(m_queue.begin() + static_cast<ptrdiff_t>(i));
                if (m_queueIndex >= static_cast<int>(m_queue.size())) m_queueIndex = static_cast<int>(m_queue.size()) - 1;
                break;
            }
        }
        ImGui::EndTable();
    }
}

void AppUI::renderDownloadsTab() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    m_icons.draw("download", 22.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("Download Manager");
    ImGui::SameLine();
    if (ImGui::Button("Relancer échoués")) m_sync.retryFailedDownloads();
    ImGui::SameLine();
    if (ImGui::Button("Annuler file")) m_sync.cancelAllDownloads();
    ImGui::SameLine();
    if (ImGui::Button("Clear Completed")) m_sync.clearCompletedDownloads();
    ImGui::Separator();

    auto items = m_sync.downloadsSnapshot();
    if (items.empty()) {
        ImGui::TextDisabled("No active or recent downloads.");
        return;
    }

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& d = items[i];
        ImGui::PushID(static_cast<int>(i));
        m_icons.draw(platformIconKey(d.platform), 18.0f, pal.accent);
        ImGui::SameLine();
        ImGui::Text("%s - %s (%s)", d.title.c_str(), d.artist.c_str(), d.asVideo ? "MP4" : "M4A");

        const char* st = "Queued";
        if (d.state == DownloadState::Downloading) st = "Downloading";
        else if (d.state == DownloadState::Downloaded) st = "Completed";
        else if (d.state == DownloadState::Failed) st = "Failed";

        ImGui::ProgressBar(d.progressPct / 100.0f, ImVec2(-1.0f, 18.0f));
        ImGui::TextDisabled("%s | %.1f%% | %s | ETA %s", st, d.progressPct,
                            d.speed.empty() ? "-" : d.speed.c_str(),
                            d.eta.empty() ? "-" : d.eta.c_str());
        if (!d.errorMsg.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", d.errorMsg.c_str());
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}

void AppUI::renderStatsTab() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    m_icons.draw("stats", 22.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("Local Listening Statistics");
    ImGui::Separator();

    double totalSec = m_stats.totalSeconds();
    double hours = totalSec / 3600.0;
    ImGui::TextColored(pal.accent, "Total Listening Time: %.2f hours (%s)", hours, formatTime(totalSec).c_str());
    ImGui::Text("Total Plays: %d", m_stats.totalPlays());

    auto pSec = m_stats.platformSeconds();
    ImGui::TextDisabled("By Platform -> YT Music: %s | YouTube: %s | TikTok: %s | Local: %s",
                        formatTime(pSec[0]).c_str(), formatTime(pSec[1]).c_str(),
                        formatTime(pSec[2]).c_str(), formatTime(pSec[3]).c_str());

    ImGui::Separator();
    ImGui::Text("Top Most Listened Tracks");
    auto top = m_stats.topTracks(12);
    if (ImGui::BeginTable("##TopStats", 4, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Platform", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Plays", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (const auto& t : top) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s - %s", t.title.c_str(), t.artist.c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", platformName(t.platform));
            ImGui::TableNextColumn(); ImGui::Text("%d", t.playCount);
            ImGui::TableNextColumn(); ImGui::Text("%s", formatTime(t.secondsListened).c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Recent History");
    auto hist = m_stats.recentHistory(12);
    for (const auto& h : hist) {
        ImGui::TextDisabled("[%s] %s - %s (%s)", h.timestamp.c_str(), h.title.c_str(), h.artist.c_str(), platformName(h.platform));
    }
}

void AppUI::renderSettingsTab() {
    TemplatePalette pal = activePalette();
    AppSettings s = m_library.settings();
    bool changed = false;

    if (ImGui::BeginTabBar("##SettingsSubTabs")) {
        if (ImGui::BeginTabItem("Stats")) {
            renderStatsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Général")) {
            ImGui::Spacing();
            ImGui::TextColored(pal.accent, "Interface & Affichage");
            ImGui::Separator();

            const char* layoutModes[] = { "Auto (largeur < 720px)", "Desktop", "Téléphone (Mobile)" };
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::Combo("Mode d'interface", &s.uiLayoutMode, layoutModes, 3)) changed = true;

            if (ImGui::Checkbox("Auto-hide bars after 3s in Focus Mode", &s.autoHideBarsFullscreen)) changed = true;
            if (s.autoHideBarsFullscreen) {
                ImGui::Indent(22.0f);
                if (ImGui::Checkbox("Top bar##ah_top", &s.autoHideTopBar)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Bottom bar##ah_bot", &s.autoHideBottomBar)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("File d'attente##ah_fq", &s.autoHideFocusQueue)) changed = true;
                ImGui::Unindent(22.0f);
            }
            if (ImGui::Checkbox("Vérifier les mises à jour au lancement", &s.autoCheckUpdates)) changed = true;
            ImGui::SameLine();
            if (ImGui::Button("Vérifier##chk_upd")) {
                m_sync.checkUpdatesAsync();
            }
            if (ImGui::Checkbox("Installer automatiquement les mises à jour", &s.autoInstallUpdates)) changed = true;
            std::string updSt = m_sync.updateStatus();
            if (!updSt.empty()) {
                ImGui::TextColored(m_sync.hasUpdateAvailable() ? pal.accent : pal.textMuted, "%s", updSt.c_str());
                if (m_sync.hasUpdateAvailable() && !m_sync.isInstallingUpdate()) {
                    if (!m_sync.updateAssetUrl().empty()) {
                        ImGui::SameLine();
                        std::string instLbl = "Installer v" + m_sync.latestVersion() + "##inst_upd";
                        if (ImGui::Button(instLbl.c_str())) {
                            m_sync.installUpdateAsync();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Ouvrir GitHub##open_upd")) {
                        ShellExecuteA(nullptr, "open", m_sync.updateUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(pal.accent, "Touches Média & Discord");
            ImGui::Separator();

            if (ImGui::Checkbox("Enable Global Media Keys (Play/Pause, Next, Prev)", &s.enableMediaKeys)) {
                m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
                changed = true;
            }
            if (ImGui::Checkbox("Exclusive Media Keys (Only control StreamPulse)", &s.mediaKeysExclusive)) {
                m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
                changed = true;
            }
            if (ImGui::Checkbox("Force Focus app response in background", &s.forceFocusOnMediaKey)) {
                m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
                changed = true;
            }
            if (ImGui::Checkbox("Enable Discord Rich Presence", &s.enableDiscordRPC)) {
                m_discord.setEnabled(s.enableDiscordRPC, s.discordClientId);
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextColored(pal.accent, "Raccourcis Clavier");
            ImGui::SameLine();
            ImGui::TextDisabled("(clic gauche: assigner, clic droit: debind)");
            ImGui::Separator();

            auto keybindCell = [&](const char* label, int slot, int& keyCode) {
                ImGui::Text("%-14s", label);
                ImGui::SameLine(130.0f);
                std::string btnTxt = (m_rebindingSlot == slot)
                    ? "Appuyez..."
                    : (keyCode <= 0 ? "Aucun" : MediaKeys::keyName(keyCode));
                if (ImGui::Button((btnTxt + "##kb_" + std::to_string(slot)).c_str(), ImVec2(120.0f, 0.0f))) {
                    m_rebindingSlot = slot;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    keyCode = 0;
                    if (m_rebindingSlot == slot) m_rebindingSlot = -1;
                    changed = true;
                }
            };

            if (ImGui::BeginTable("##KeybindsGrid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); keybindCell("Play / Pause", 0, s.keyPlayPause);
                ImGui::TableNextColumn(); keybindCell("Seek -5s", 4, s.keySeekBack);

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); keybindCell("Mute", 1, s.keyMute);
                ImGui::TableNextColumn(); keybindCell("Seek +5s", 5, s.keySeekFwd);

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); keybindCell("Mode Focus", 2, s.keyFullscreen);
                ImGui::TableNextColumn(); keybindCell("Volume +5%", 6, s.keyVolUp);

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); keybindCell("Fullscreen", 3, s.keyOsFullscreen);
                ImGui::TableNextColumn(); keybindCell("Volume -5%", 7, s.keyVolDown);
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextColored(pal.accent, "Sauvegarde des paramètres (toute l'app)");
            ImGui::Separator();
            if (ImGui::Button("Exporter paramètres (.json)")) {
                std::string p = saveFileWin32(L"JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0", L"streampulse_settings.json");
                if (!p.empty()) {
                    m_settingsIoNotice = m_library.exportSettingsToFile(p)
                        ? "Paramètres exportés !"
                        : "Erreur lors de l'export.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Importer paramètres (.json)")) {
                std::string p = browseFileWin32(L"JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0");
                if (!p.empty()) {
                    if (m_library.importSettingsFromFile(p)) {
                        s = m_library.settings();
                        m_player.setVolume(s.volume);
                        m_player.setMuted(s.muted);
                        m_player.setEqGains(s.eqGainsDb);
                        m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
                        m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
                        m_discord.setEnabled(s.enableDiscordRPC, s.discordClientId);
                        m_library.save();
                        applyPaletteStyle(activePalette(), m_vk);
                        m_settingsIoNotice = "Paramètres importés et appliqués !";
                    } else {
                        m_settingsIoNotice = "Fichier paramètres invalide.";
                    }
                }
            }
            if (!m_settingsIoNotice.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(pal.accentSecondary, "%s", m_settingsIoNotice.c_str());
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mélangeur Audio")) {
            m_icons.draw("equalizer", 20.0f, pal.accent);
            ImGui::SameLine();
            ImGui::Text("Préréglages Audio intégrés");
            ImGui::Separator();

            struct AudioQuickPreset {
                const char* name;
                AudioPreset preset;
                std::array<float, 5> eq;
                float preamp;
                float width;
            };
            const AudioQuickPreset builtIns[] = {
                { "Flat",          AudioPreset::Flat,         {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f },  0.0f, 1.0f },
                { "YouTube Music", AudioPreset::YouTubeMusic, {  2.0f,  1.0f,  0.0f,  1.5f,  1.0f },  0.0f, 1.0f },
                { "Spotify",       AudioPreset::Spotify,      {  3.5f,  1.5f, -0.5f,  2.0f,  2.5f },  0.0f, 1.05f },
                { "TikTok",        AudioPreset::TikTok,       {  4.5f,  2.0f,  1.0f,  3.0f,  2.0f },  0.5f, 1.15f },
                { "Bass Boost",    AudioPreset::Custom,       {  7.5f,  4.5f,  0.0f,  0.5f,  0.0f }, -1.0f, 1.0f },
                { "Vocal / Clair", AudioPreset::Custom,       { -2.0f,  0.0f,  3.5f,  4.5f,  2.0f },  0.5f, 0.95f },
                { "Nuit / Doux",   AudioPreset::Custom,       { -3.0f, -1.0f,  1.0f,  0.0f, -2.5f }, -2.0f, 0.85f }
            };
            for (int i = 0; i < 7; ++i) {
                if (i % 4 != 0) ImGui::SameLine();
                bool active = (s.activeCustomAudioIndex < 0 && i < 4 && static_cast<int>(s.audioPreset) == static_cast<int>(builtIns[i].preset));
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
                if (ImGui::Button((std::string(builtIns[i].name) + "##ap_" + std::to_string(i)).c_str())) {
                    s.activeCustomAudioIndex = -1;
                    s.audioPreset = builtIns[i].preset;
                    s.eqGainsDb = builtIns[i].eq;
                    s.preampDb = builtIns[i].preamp;
                    s.stereoWidth = builtIns[i].width;
                    s.balance = 0.0f;
                    s.monoMix = false;
                    m_player.setEqGains(s.eqGainsDb);
                    m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
                    changed = true;
                }
                if (active) ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Text("Mes Profils Audio personnalisés (%zu)", s.customAudioProfiles.size());
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(s.customAudioProfiles.size()); ++i) {
                ImGui::PushID(1000 + i);
                bool active = (s.activeCustomAudioIndex == i);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
                if (ImGui::Button(s.customAudioProfiles[i].name.c_str())) {
                    const auto& p = s.customAudioProfiles[i];
                    s.activeCustomAudioIndex = i;
                    s.audioPreset = AudioPreset::Custom;
                    s.eqGainsDb = p.eqGainsDb;
                    s.preampDb = p.preampDb;
                    s.balance = p.balance;
                    s.stereoWidth = p.stereoWidth;
                    s.monoMix = p.monoMix;
                    s.softLimiter = p.softLimiter;
                    m_player.setEqGains(s.eqGainsDb);
                    m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
                    changed = true;
                }
                if (active) ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Supprimer")) {
                    s.customAudioProfiles.erase(s.customAudioProfiles.begin() + i);
                    if (s.activeCustomAudioIndex == i) s.activeCustomAudioIndex = -1;
                    else if (s.activeCustomAudioIndex > i) --s.activeCustomAudioIndex;
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::Text("Mixage & Spatialisation (en direct)");
            ImGui::Separator();
            bool mixerChanged = false;
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::SliderFloat("Préampli (Preamp)", &s.preampDb, -12.0f, 12.0f, "%.1f dB")) mixerChanged = true;
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::SliderFloat("Balance (Gauche / Droite)", &s.balance, -1.0f, 1.0f, "%.2f")) mixerChanged = true;
            ImGui::SetNextItemWidth(280.0f);
            float widthPct = s.stereoWidth * 100.0f;
            if (ImGui::SliderFloat("Largeur Stéréo", &widthPct, 0.0f, 200.0f, "%.0f %%")) {
                s.stereoWidth = widthPct / 100.0f;
                mixerChanged = true;
            }
            if (ImGui::Checkbox("Mode Mono", &s.monoMix)) mixerChanged = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Limiteur doux (anti-saturation)", &s.softLimiter)) mixerChanged = true;

            if (mixerChanged) {
                s.activeCustomAudioIndex = -1;
                m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
                changed = true;
            }

            ImGui::Spacing();
            ImGui::Text("Égaliseur 5 Bandes");
            ImGui::Separator();
            const char* bandLabels[5] = { "60 Hz (Sub)", "230 Hz (Bass)", "910 Hz (Mid)", "3.6 kHz (Presence)", "14 kHz (Treble)" };
            for (int i = 0; i < 5; ++i) {
                ImGui::SetNextItemWidth(280.0f);
                if (ImGui::SliderFloat(bandLabels[i], &s.eqGainsDb[i], -12.0f, 12.0f, "%.1f dB")) {
                    s.audioPreset = AudioPreset::Custom;
                    s.activeCustomAudioIndex = -1;
                    m_player.setEqGains(s.eqGainsDb);
                    changed = true;
                }
            }
            if (ImGui::Button("Réinitialiser tout (0 dB / Normal)")) {
                s.audioPreset = AudioPreset::Flat;
                s.activeCustomAudioIndex = -1;
                s.eqGainsDb = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                s.preampDb = 0.0f;
                s.balance = 0.0f;
                s.stereoWidth = 1.0f;
                s.monoMix = false;
                s.softLimiter = true;
                m_player.setEqGains(s.eqGainsDb);
                m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
                changed = true;
            }

            ImGui::Spacing();
            ImGui::Text("Créer un nouveau profil audio");
            ImGui::Separator();
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("Nom du profil", m_newAudioProfileName, sizeof(m_newAudioProfileName));
            ImGui::SameLine();
            if (ImGui::Button("Créer & Sauvegarder le profil")) {
                CustomAudioProfile ap;
                ap.name = m_newAudioProfileName[0] ? std::string(m_newAudioProfileName) : "Profil Audio";
                ap.eqGainsDb = s.eqGainsDb;
                ap.preampDb = s.preampDb;
                ap.balance = s.balance;
                ap.stereoWidth = s.stereoWidth;
                ap.monoMix = s.monoMix;
                ap.softLimiter = s.softLimiter;
                s.customAudioProfiles.push_back(std::move(ap));
                s.activeCustomAudioIndex = static_cast<int>(s.customAudioProfiles.size()) - 1;
                changed = true;
            }

            ImGui::Spacing();
            ImGui::Text("Exporter / Importer (texte)");
            ImGui::Separator();
            if (ImGui::Button("Exporter réglage actuel (copier texte)##exp_aud")) {
                CustomAudioProfile cur;
                cur.name = (s.activeCustomAudioIndex >= 0 && s.activeCustomAudioIndex < static_cast<int>(s.customAudioProfiles.size()))
                    ? s.customAudioProfiles[s.activeCustomAudioIndex].name
                    : std::string(m_newAudioProfileName[0] ? m_newAudioProfileName : "Profil Audio");
                cur.eqGainsDb = s.eqGainsDb;
                cur.preampDb = s.preampDb;
                cur.balance = s.balance;
                cur.stereoWidth = s.stereoWidth;
                cur.monoMix = s.monoMix;
                cur.softLimiter = s.softLimiter;
                std::string txt = LibraryManager::exportAudioProfileText(cur);
                std::snprintf(m_audioTextBuf, sizeof(m_audioTextBuf), "%s", txt.c_str());
                ImGui::SetClipboardText(txt.c_str());
            }
            ImGui::SetNextItemWidth(340.0f);
            ImGui::InputTextWithHint("##AudioTxtIO", "Coller le texte d'un profil audio...", m_audioTextBuf, sizeof(m_audioTextBuf));
            ImGui::SameLine();
            if (ImGui::Button("Importer (texte)##imp_aud")) {
                if (auto apOpt = LibraryManager::importAudioProfileText(m_audioTextBuf)) {
                    CustomAudioProfile ap = *apOpt;
                    s.customAudioProfiles.push_back(ap);
                    s.activeCustomAudioIndex = static_cast<int>(s.customAudioProfiles.size()) - 1;
                    s.audioPreset = AudioPreset::Custom;
                    s.eqGainsDb = ap.eqGainsDb;
                    s.preampDb = ap.preampDb;
                    s.balance = ap.balance;
                    s.stereoWidth = ap.stereoWidth;
                    s.monoMix = ap.monoMix;
                    s.softLimiter = ap.softLimiter;
                    m_player.setEqGains(s.eqGainsDb);
                    m_player.setAudioMixer(s.preampDb, s.balance, s.stereoWidth, s.monoMix, s.softLimiter);
                    changed = true;
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Thèmes")) {
            ImGui::Text("Thèmes intégrés (YouTube Music, Spotify, TikTok, ColorHunt)");
            ImGui::Separator();
            const char* builtInNames[] = {
                "YouTube Music", "Spotify", "TikTok", "Minimal",
                "ColorHunt: Neon Night", "ColorHunt: Cyber Mint", "ColorHunt: Sunset Synth", "ColorHunt: Emerald Dark"
            };
            for (int i = 0; i < 8; ++i) {
                if (i % 4 != 0) ImGui::SameLine();
                bool active = (s.activeCustomThemeIndex < 0 && static_cast<int>(s.activeTemplate) == i);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
                if (ImGui::Button(builtInNames[i])) {
                    m_previewCustomTheme = false;
                    s.activeCustomThemeIndex = -1;
                    s.activeTemplate = static_cast<GuiTemplate>(i);
                    changed = true;
                }
                if (active) ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Text("Mes Thèmes personnalisés (%zu)", s.customThemes.size());
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(s.customThemes.size()); ++i) {
                ImGui::PushID(i);
                bool active = (s.activeCustomThemeIndex == i);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
                if (ImGui::Button(s.customThemes[i].name.c_str())) {
                    m_previewCustomTheme = false;
                    s.activeCustomThemeIndex = i;
                    changed = true;
                }
                if (active) ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Supprimer")) {
                    s.customThemes.erase(s.customThemes.begin() + i);
                    if (s.activeCustomThemeIndex == i) s.activeCustomThemeIndex = -1;
                    else if (s.activeCustomThemeIndex > i) --s.activeCustomThemeIndex;
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::Text("Créer un nouveau thème (aperçu en temps réel)");
            ImGui::Separator();
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("Nom du thème", m_newThemeName, sizeof(m_newThemeName));
            bool draftChanged = false;
            if (ImGui::ColorEdit4("Fond principal", m_newThemeDraft.bgMain.data())) draftChanged = true;
            if (ImGui::ColorEdit4("Barre latérale", m_newThemeDraft.bgSidebar.data())) draftChanged = true;
            if (ImGui::ColorEdit4("Cartes / Boutons", m_newThemeDraft.bgCard.data())) draftChanged = true;
            if (ImGui::ColorEdit4("Couleur Accent", m_newThemeDraft.accent.data())) draftChanged = true;
            if (ImGui::ColorEdit4("Accent secondaire", m_newThemeDraft.accentSecondary.data())) draftChanged = true;
            if (ImGui::ColorEdit4("Texte principal", m_newThemeDraft.textPrimary.data())) draftChanged = true;
            if (ImGui::SliderFloat("Arrondi (px)", &m_newThemeDraft.rounding, 0.0f, 20.0f, "%.0f px")) draftChanged = true;
            if (draftChanged) {
                m_previewCustomTheme = true;
                applyPaletteStyle(paletteFromCustom(m_newThemeDraft), m_vk);
            }
            if (ImGui::Button("Créer & Appliquer le thème")) {
                CustomTheme ct = m_newThemeDraft;
                ct.name = m_newThemeName[0] ? std::string(m_newThemeName) : "Custom Theme";
                s.customThemes.push_back(std::move(ct));
                s.activeCustomThemeIndex = static_cast<int>(s.customThemes.size()) - 1;
                m_previewCustomTheme = false;
                changed = true;
            }
            if (m_previewCustomTheme) {
                ImGui::SameLine();
                if (ImGui::Button("Annuler aperçu")) {
                    m_previewCustomTheme = false;
                    applyPaletteStyle(activePalette(), m_vk);
                }
            }

            ImGui::Spacing();
            ImGui::Text("Exporter / Importer thème (texte)");
            ImGui::Separator();
            if (ImGui::Button("Exporter thème actuel (copier texte)##exp_thm")) {
                CustomTheme cur = m_newThemeDraft;
                if (!m_previewCustomTheme && s.activeCustomThemeIndex >= 0 &&
                    s.activeCustomThemeIndex < static_cast<int>(s.customThemes.size())) {
                    cur = s.customThemes[s.activeCustomThemeIndex];
                } else {
                    cur.name = m_newThemeName[0] ? std::string(m_newThemeName) : "Custom Theme";
                }
                std::string txt = LibraryManager::exportThemeText(cur);
                std::snprintf(m_themeTextBuf, sizeof(m_themeTextBuf), "%s", txt.c_str());
                ImGui::SetClipboardText(txt.c_str());
            }
            ImGui::SetNextItemWidth(340.0f);
            ImGui::InputTextWithHint("##ThemeTxtIO", "Coller le texte d'un thème...", m_themeTextBuf, sizeof(m_themeTextBuf));
            ImGui::SameLine();
            if (ImGui::Button("Importer (texte)##imp_thm")) {
                if (auto thOpt = LibraryManager::importThemeText(m_themeTextBuf)) {
                    m_previewCustomTheme = false;
                    s.customThemes.push_back(*thOpt);
                    s.activeCustomThemeIndex = static_cast<int>(s.customThemes.size()) - 1;
                    changed = true;
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Console")) {
            if (ImGui::Button("Effacer les logs")) {
                AppLog::instance().clear();
            }
            ImGui::Separator();
            ImGui::BeginChild("##AppConsoleScroll", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            auto lines = AppLog::instance().snapshot();
            for (const auto& l : lines) {
                ImGui::TextUnformatted(l.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (changed) {
        m_library.updateSettings(s);
        m_library.save();
        applyPaletteStyle(activePalette(), m_vk);
    }
}

void AppUI::renderStagePanel(bool isFullscreen) {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());

    float availW = ImGui::GetContentRegionAvail().x;
    float totalH = ImGui::GetContentRegionAvail().y;

    if (isFullscreen && isMobileLayout()) {
        // Mobile Phone Focus Player UI
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.22f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 18.0f);

        if (ImGui::Button("v##mob_close", ImVec2(32.0f, 32.0f))) {
            m_fullscreenMode = false;
        }
        ImGui::SameLine(std::max(60.0f, availW - 36.0f));
        if (m_icons.button("##mob_qbtn", "queue", 20.0f, m_mobileFocusQueue ? pal.accent : pal.textPrimary)) {
            m_mobileFocusQueue = !m_mobileFocusQueue;
        }

        float tgtMQ = m_mobileFocusQueue ? 1.0f : 0.0f;
        float mqStep = ImGui::GetIO().DeltaTime * 5.0f;
        if (m_mobQueueAnim < tgtMQ) m_mobQueueAnim = std::min(1.0f, m_mobQueueAnim + mqStep);
        else if (m_mobQueueAnim > tgtMQ) m_mobQueueAnim = std::max(0.0f, m_mobQueueAnim - mqStep);
        float easedMQ = 1.0f - std::pow(1.0f - m_mobQueueAnim, 3.0f);

        float bottomY = std::max(180.0f, totalH - 168.0f);
        float topAreaH = 38.0f;
        float mediaMaxH = std::max(140.0f, bottomY - topAreaH - 10.0f);
        float mediaMaxW = std::max(140.0f, availW - 8.0f);
        auto drawMobMedia = [&](ImTextureID texId, uint32_t texW, uint32_t texH) {
            float aspect = (texW > 0 && texH > 0) ? (static_cast<float>(texW) / static_cast<float>(texH)) : 1.0f;
            float drawW = mediaMaxW;
            float drawH = drawW / aspect;
            if (drawH > mediaMaxH) { drawH = mediaMaxH; drawW = drawH * aspect; }
            ImGui::SetCursorPosX((availW - drawW) * 0.5f);
            ImGui::SetCursorPosY(topAreaH + (mediaMaxH - drawH) * 0.5f);
            ImGui::Image(texId, ImVec2(drawW, drawH));
        };

        if (m_player.hasVideo() && m_videoTexture.isValid()) {
            drawMobMedia(m_videoTexture.imguiId(), m_videoTexture.width(), m_videoTexture.height());
        } else if (m_coverTexture.isValid()) {
            drawMobMedia(m_coverTexture.imguiId(), m_coverTexture.width(), m_coverTexture.height());
        } else {
            drawMobMedia(m_defaultCoverTexture.imguiId(), m_defaultCoverTexture.width(), m_defaultCoverTexture.height());
        }
        if (ImGui::BeginPopupContextItem("##stage_media_ctx")) {
            if (ImGui::MenuItem("Arrêter la lecture")) {
                m_player.stop();
                m_hasCurrentTrack = false;
                m_currentTrack = Track{};
                m_currentPlaylistId.clear();
                m_playbackNotice.clear();
                m_fullscreenMode = false;
            }
            ImGui::EndPopup();
        }

        float padX = 12.0f;
        float innerW = std::max(120.0f, availW - padX * 2.0f);

        ImGui::SetCursorPos(ImVec2(padX, bottomY));
        ImGui::SetWindowFontScale(1.22f);
        ImGui::Text("%s", m_hasCurrentTrack ? m_currentTrack.title.c_str() : "Aucune lecture");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorPosX(padX);
        ImGui::TextDisabled("%s", m_hasCurrentTrack ? m_currentTrack.artist.c_str() : "");

        // Seek bar
        double pos = m_player.positionSeconds();
        double dur = m_player.durationSeconds();
        float frac = (dur > 0.1) ? std::clamp(static_cast<float>(pos / dur), 0.0f, 1.0f) : 0.0f;
        ImGui::SetCursorPos(ImVec2(padX, bottomY + 58.0f));
        ImVec2 seekMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##MobSeek", ImVec2(innerW, 16.0f));
        bool sAct = ImGui::IsItemActive();
        if ((sAct || ImGui::IsItemClicked()) && dur > 0.1) {
            float nf = std::clamp((ImGui::GetIO().MousePos.x - seekMin.x) / std::max(1.0f, innerW), 0.0f, 1.0f);
            m_player.seek(static_cast<double>(nf) * dur);
            frac = nf;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float sy = seekMin.y + 8.0f;
        dl->AddRectFilled(ImVec2(seekMin.x, sy - 1.5f), ImVec2(seekMin.x + innerW, sy + 1.5f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.22f)));
        dl->AddRectFilled(ImVec2(seekMin.x, sy - 1.5f), ImVec2(seekMin.x + innerW * frac, sy + 1.5f), ImGui::GetColorU32(pal.accent));
        dl->AddCircleFilled(ImVec2(seekMin.x + innerW * frac, sy), sAct ? 7.0f : 5.5f, ImGui::GetColorU32(pal.accent));

        ImGui::SetCursorPos(ImVec2(padX, bottomY + 76.0f));
        ImGui::TextDisabled("%s", formatTime(pos).c_str());
        std::string durStr = dur > 0.1 ? formatTime(dur) : "--:--";
        float durW = ImGui::CalcTextSize(durStr.c_str()).x;
        ImGui::SetCursorPos(ImVec2(padX + innerW - durW, bottomY + 76.0f));
        ImGui::TextDisabled("%s", durStr.c_str());

        // 5 transport buttons with equal padding between screen edges and between every button
        float ctrlCenterY = bottomY + 130.0f;
        float g = std::max(6.0f, (availW - 194.0f) / 6.0f);
        float shufCX = g + 14.0f;
        float prevCX = g + 28.0f + g + 18.0f;
        float playCX = g + 28.0f + g + 36.0f + g + 33.0f;
        float nextCX = playCX + 33.0f + g + 18.0f;
        float repCX  = availW - g - 14.0f;

        auto mobIconBtn = [&](const char* id, float cxBtn, const char* icon, float iconSz, ImVec4 tint) -> bool {
            float hitSz = 48.0f;
            ImGui::SetCursorPos(ImVec2(cxBtn - hitSz * 0.5f, ctrlCenterY - hitSz * 0.5f));
            ImVec2 pMin = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton(id, ImVec2(hitSz, hitSz));
            if (ImGui::IsItemHovered()) {
                dl->AddCircleFilled(ImVec2(pMin.x + hitSz * 0.5f, pMin.y + hitSz * 0.5f),
                                    hitSz * 0.5f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.12f)));
            }
            ImGui::SetCursorPos(ImVec2(cxBtn - iconSz * 0.5f, ctrlCenterY - iconSz * 0.5f));
            m_icons.draw(icon, iconSz, tint);
            return clicked;
        };

        if (mobIconBtn("##mob_shuf", shufCX, "shuffle", 28.0f, m_shuffle ? pal.accent : pal.textMuted)) {
            m_shuffle = !m_shuffle;
            auto st = m_library.settings();
            st.shuffle = m_shuffle;
            m_library.updateSettings(st);
            m_library.save();
        }

        if (mobIconBtn("##mob_prev", prevCX, "prev", 36.0f, pal.textPrimary)) playPrev();

        // Center Play/Pause circle (66x66 exact hitbox, no hover animation)
        ImGui::SetCursorPos(ImVec2(playCX - 33.0f, ctrlCenterY - 33.0f));
        ImVec2 playMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##mob_play", ImVec2(66.0f, 66.0f))) {
            if (m_player.isOpen()) m_player.togglePlayPause();
        }
        dl->AddCircleFilled(ImVec2(playMin.x + 33.0f, playMin.y + 33.0f), 33.0f, ImGui::GetColorU32(pal.accent));
        ImGui::SetCursorPos(ImVec2(playCX - 18.0f, ctrlCenterY - 18.0f));
        m_icons.draw(m_player.isPlaying() ? "pause" : "play", 36.0f, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));

        if (mobIconBtn("##mob_next", nextCX, "next", 36.0f, pal.textPrimary)) playNext();

        const char* repIcon = (m_repeatMode == 0) ? "repeat" : (m_repeatMode == 1 ? "repeat_one" : "repeat_all");
        ImVec4 repCol = (m_repeatMode == 0) ? pal.textMuted : (m_repeatMode == 1 ? pal.accentSecondary : pal.accent);
        if (mobIconBtn("##mob_rep", repCX, repIcon, 28.0f, repCol)) {
            m_repeatMode = (m_repeatMode == 0) ? 2 : (m_repeatMode == 2 ? 1 : 0);
            auto st = m_library.settings();
            st.repeatMode = m_repeatMode;
            m_library.updateSettings(st);
            m_library.save();
        }

        if (easedMQ > 0.01f) {
            float areaTop = 40.0f;
            float areaH = std::max(1.0f, totalH - areaTop);
            float offsetY = areaH * (1.0f - easedMQ);
            ImGui::SetCursorPos(ImVec2(0.0f, areaTop + offsetY));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.bgMain);
            ImGui::BeginChild("##MobFocusQ", ImVec2(availW, std::max(1.0f, areaH - offsetY)), true);
            if (m_queue.empty()) {
                ImGui::TextDisabled("File d'attente vide.");
            } else {
                for (size_t i = 0; i < m_queue.size(); ++i) {
                    const auto& q = m_queue[i];
                    bool isQCur = (m_playingFromQueue && m_queueIndex == static_cast<int>(i));
                    ImVec2 rowStart = ImGui::GetCursorPos();
                    if (ImGui::Selectable(("##mfq_" + std::to_string(i)).c_str(), isQCur, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, 52.0f))) {
                        m_queueIndex = static_cast<int>(i);
                        m_playingFromQueue = true;
                        playTrack(q.playlistId, q.track);
                        break;
                    }
                    ImVec2 rowEnd = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(rowStart.x + 2.0f, rowStart.y + 2.0f));
                    ImGui::Image(getTrackThumb(q.track), ImVec2(48.0f, 48.0f));
                    ImGui::SetCursorPos(ImVec2(rowStart.x + 58.0f, rowStart.y + 6.0f));
                    ImGui::BeginGroup();
                    ImGui::Text("%s", q.track.title.c_str());
                    ImGui::TextDisabled("%s", q.track.artist.c_str());
                    ImGui::EndGroup();
                    ImGui::SetCursorPos(rowEnd);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        return;
    }

    bool hideFocusQueue = isFullscreen && m_library.settings().autoHideBarsFullscreen &&
                          m_library.settings().autoHideFocusQueue && (m_idleSeconds >= 3.0);
    float easedFQ = 0.0f;
    if (isFullscreen) {
        float tgtFQ = (m_focusQueueOpen && !hideFocusQueue) ? 1.0f : 0.0f;
        float fqStep = ImGui::GetIO().DeltaTime * 5.0f;
        if (m_focusQueueAnim < tgtFQ) m_focusQueueAnim = std::min(1.0f, m_focusQueueAnim + fqStep);
        else if (m_focusQueueAnim > tgtFQ) m_focusQueueAnim = std::max(0.0f, m_focusQueueAnim - fqStep);
        easedFQ = 1.0f - std::pow(1.0f - m_focusQueueAnim, 3.0f);
    }

    float fullQueueW = isFullscreen ? std::clamp(availW * 0.23f, 230.0f, 320.0f) : 0.0f;
    float queueW = isFullscreen ? (fullQueueW * easedFQ) : 0.0f;
    float reopenBtnW = (isFullscreen && queueW < 4.0f && !hideFocusQueue) ? 30.0f : 0.0f;
    float mediaAreaW = isFullscreen
        ? std::max(160.0f, availW - queueW - (queueW >= 4.0f ? 10.0f : 0.0f) - reopenBtnW)
        : availW;
    float mediaH = isFullscreen ? (totalH - 4.0f) : std::min(mediaAreaW * 0.92f, totalH * 0.54f);
    if (mediaH < 140.0f) mediaH = 140.0f;

    if (isFullscreen) {
        ImGui::BeginChild("##FocusMedia", ImVec2(mediaAreaW, totalH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }

    auto drawFitMedia = [&](ImTextureID texId, uint32_t texW, uint32_t texH) {
        float aspect = (texW > 0 && texH > 0) ? (static_cast<float>(texW) / static_cast<float>(texH)) : 1.0f;
        float drawW = mediaAreaW;
        float drawH = drawW / aspect;
        if (drawH > mediaH) { drawH = mediaH; drawW = drawH * aspect; }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (mediaAreaW - drawW) * 0.5f);
        if (isFullscreen) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (totalH - drawH) * 0.5f);
        ImGui::Image(texId, ImVec2(drawW, drawH));
    };

    if (m_player.hasVideo() && m_videoTexture.isValid()) {
        drawFitMedia(m_videoTexture.imguiId(), m_videoTexture.width(), m_videoTexture.height());
    } else if (m_coverTexture.isValid()) {
        drawFitMedia(m_coverTexture.imguiId(), m_coverTexture.width(), m_coverTexture.height());
    } else {
        drawFitMedia(m_defaultCoverTexture.imguiId(), m_defaultCoverTexture.width(), m_defaultCoverTexture.height());
    }
    if (ImGui::BeginPopupContextItem("##stage_media_ctx")) {
        if (ImGui::MenuItem("Arrêter la lecture")) {
            m_player.stop();
            m_hasCurrentTrack = false;
            m_currentTrack = Track{};
            m_currentPlaylistId.clear();
            m_playbackNotice.clear();
            m_fullscreenMode = false;
        }
        ImGui::EndPopup();
    }

    if (isFullscreen) {
        ImGui::EndChild();

        if (queueW < 4.0f) {
            if (!hideFocusQueue) {
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
                if (ImGui::Button("<##fq_open", ImVec2(26.0f, 36.0f))) {
                    m_focusQueueOpen = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Afficher la file d'attente");
            }
        } else {
            ImGui::SameLine();
            ImGui::BeginChild("##FocusQueue", ImVec2(0.0f, totalH), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextColored(pal.accent, "À suivre");
            ImGui::SameLine(std::max(80.0f, ImGui::GetWindowWidth() - 36.0f));
            if (ImGui::SmallButton(">##fq_close")) {
                m_focusQueueOpen = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rétracter");
            ImGui::Separator();
            ImGui::BeginChild("##FocusQueueList", ImVec2(0.0f, 0.0f), false);
            if (m_queue.empty()) {
                ImGui::TextDisabled("File d'attente vide.");
            } else {
                for (size_t i = 0; i < m_queue.size(); ++i) {
                    const auto& q = m_queue[i];
                    bool isQCur = (m_playingFromQueue && m_queueIndex == static_cast<int>(i));
                    ImVec2 rowStart = ImGui::GetCursorPos();
                    if (ImGui::Selectable(("##fq_" + std::to_string(i)).c_str(), isQCur, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, 52.0f))) {
                        m_queueIndex = static_cast<int>(i);
                        m_playingFromQueue = true;
                        playTrack(q.playlistId, q.track);
                        break;
                    }
                    ImVec2 rowEnd = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(rowStart.x + 2.0f, rowStart.y + 2.0f));
                    ImGui::Image(getTrackThumb(q.track), ImVec2(48.0f, 48.0f));
                    ImGui::SetCursorPos(ImVec2(rowStart.x + 58.0f, rowStart.y + 6.0f));
                    ImGui::BeginGroup();
                    ImGui::Text("%s", q.track.title.c_str());
                    ImGui::TextDisabled("%s", q.track.artist.c_str());
                    ImGui::EndGroup();
                    ImGui::SetCursorPos(rowEnd);
                }
            }
            ImGui::EndChild();
            ImGui::EndChild();
        }
    }

    if (!isFullscreen) {
        ImGui::Spacing();
        if (m_hasCurrentTrack) {
            m_icons.draw(platformIconKey(m_currentTrack.platform), 16.0f, pal.accent);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", m_currentTrack.title.c_str());
            ImGui::TextDisabled("%s", m_currentTrack.artist.c_str());
        } else {
            ImGui::TextDisabled("Aucune lecture en cours");
        }
        if (!m_playbackNotice.empty()) ImGui::TextColored(pal.accentSecondary, "%s", m_playbackNotice.c_str());
    }
    std::string err = m_player.lastError();
    if (!err.empty()) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", err.c_str());

    if (!isFullscreen) {
        ImGui::Spacing();
        if (ImGui::BeginTabBar("##YTMWatchTabs")) {
            if (ImGui::BeginTabItem("À suivre")) {
                float qH = ImGui::GetContentRegionAvail().y;
                ImGui::BeginChild("##StageQueue", ImVec2(0.0f, qH), false);
                if (m_queue.empty()) {
                    ImGui::TextDisabled("File d'attente vide.");
                } else {
                    for (size_t i = 0; i < m_queue.size(); ++i) {
                        const auto& q = m_queue[i];
                        bool isQCur = (m_playingFromQueue && m_queueIndex == static_cast<int>(i));
                        ImGui::Image(getTrackThumb(q.track), ImVec2(34.0f, 34.0f));
                        ImGui::SameLine();
                        if (ImGui::Selectable((q.track.title + "##sq_" + std::to_string(i)).c_str(), isQCur)) {
                            m_queueIndex = static_cast<int>(i);
                            m_playingFromQueue = true;
                            playTrack(q.playlistId, q.track);
                            break;
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
}

void AppUI::renderPlayerBar() {
    TemplatePalette pal = activePalette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    float winW = ImGui::GetWindowWidth();
    float winH = ImGui::GetWindowHeight();

    // 1. Sleek full-width 3px YouTube Music Seek Bar flush at top edge
    double pos = m_player.positionSeconds();
    double dur = m_player.durationSeconds();
    float frac = (dur > 0.1) ? std::clamp(static_cast<float>(pos / dur), 0.0f, 1.0f) : 0.0f;

    ImGui::SetCursorScreenPos(winPos);
    ImGui::InvisibleButton("##SeekBarHit", ImVec2(winW, 12.0f));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    if ((active || ImGui::IsItemClicked()) && dur > 0.1) {
        float mx = ImGui::GetIO().MousePos.x;
        float nf = std::clamp((mx - winPos.x) / std::max(1.0f, winW), 0.0f, 1.0f);
        m_player.seek(static_cast<double>(nf) * dur);
        frac = nf;
    }
    float barH = (hovered || active) ? 4.5f : 2.5f;
    ImU32 trackCol = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
    ImU32 fillCol = ImGui::GetColorU32(pal.accent);
    dl->AddRectFilled(winPos, ImVec2(winPos.x + winW, winPos.y + barH), trackCol);
    dl->AddRectFilled(winPos, ImVec2(winPos.x + winW * frac, winPos.y + barH), fillCol);
    if (hovered || active || dur > 0.1) {
        float knobR = (hovered || active) ? 6.0f : 4.5f;
        dl->AddCircleFilled(ImVec2(winPos.x + winW * frac, winPos.y + barH * 0.5f), knobR, fillCol);
    }

    // Transparent borderless icon button style
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.24f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.0f);

    float centerY = (winH + barH) * 0.5f;

    // Left: Prev (26) / Play-Pause (34) / Next (26) + Time
    ImGui::SetCursorPos(ImVec2(16.0f, centerY - 17.0f));
    if (m_icons.button("##prev", "prev", 26.0f, pal.textPrimary)) playPrev();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetCursorPosY(centerY - 21.0f);
    if (m_icons.button("##playpause", m_player.isPlaying() ? "pause" : "play", 34.0f, pal.textPrimary)) {
        if (m_player.isOpen()) m_player.togglePlayPause();
    }
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetCursorPosY(centerY - 17.0f);
    if (m_icons.button("##next", "next", 26.0f, pal.textPrimary)) playNext();
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetCursorPosY(centerY - 9.0f);
    ImGui::TextDisabled("%s / %s", formatTime(pos).c_str(), dur > 0.1 ? formatTime(dur).c_str() : "--:--");

    // Right: Fixed-anchor layout from right edge so volume % width never shifts buttons
    float rightZoneW = 360.0f;
    float rightStartX = std::max(270.0f, winW - rightZoneW - 14.0f);

    // Center: Cover + Title + Artist • Platform + Like (tightly sized so Heart sits right next to text)
    if (m_hasCurrentTrack && winW >= 720.0f) {
        float leftEnd = 250.0f;
        float availCenter = rightStartX - leftEnd - 20.0f;
        if (availCenter > 150.0f) {
            std::string sub = m_currentTrack.artist + " • " + platformName(m_currentTrack.platform);
            float tW = ImGui::CalcTextSize(m_currentTrack.title.c_str()).x;
            float sW = ImGui::CalcTextSize(sub.c_str()).x;
            float maxTextW = std::max(80.0f, availCenter - 96.0f);
            float textW = std::clamp(std::max(tW, sW) + 8.0f, 80.0f, maxTextW);
            float totalBlockW = 46.0f + 12.0f + textW + 10.0f + 32.0f;
            float startX = leftEnd + std::max(0.0f, (availCenter - totalBlockW) * 0.5f);

            ImGui::SetCursorPos(ImVec2(startX, centerY - 23.0f));
            ImTextureID barCov = m_coverTexture.isValid() ? m_coverTexture.imguiId() : m_defaultCoverTexture.imguiId();
            ImGui::Image(barCov, ImVec2(46.0f, 46.0f));
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::SetCursorPosY(centerY - 20.0f);
            ImGui::BeginChild("##BarMeta", ImVec2(textW, 42.0f), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(m_currentTrack.title.c_str());
            ImGui::TextDisabled("%s", sub.c_str());
            ImGui::EndChild();
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::SetCursorPosY(centerY - 16.0f);
            if (m_icons.button("##bar_like", m_currentTrack.liked ? "heart_filled" : "heart", 22.0f,
                               m_currentTrack.liked ? pal.accent : pal.textMuted)) {
                m_currentTrack.liked = !m_currentTrack.liked;
                m_library.toggleTrackLike(m_currentPlaylistId, m_currentTrack.id);
                m_library.save();
            }
        }
    }

    // Fixed right-aligned controls
    float rx = rightStartX + 44.0f;
    ImGui::SetCursorPos(ImVec2(rx, centerY - 7.0f));
    float volPct = m_player.volume() * 100.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.0f);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::SliderFloat("##Vol", &volPct, 0.0f, 150.0f, "")) {
        m_player.setVolume(volPct / 100.0f);
        m_library.setVolume(volPct / 100.0f);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) m_library.save();
    ImGui::PopStyleVar(2);

    rx += 108.0f;
    ImGui::SetCursorPos(ImVec2(rx, centerY - 9.0f));
    ImGui::TextDisabled("%3.0f%%", volPct);

    rx += 46.0f;
    ImGui::SetCursorPos(ImVec2(rx, centerY - 16.0f));
    bool muted = m_player.isMuted();
    if (m_icons.button("##mute", muted ? "volume_mute" : "volume_high", 24.0f, muted ? pal.accent : pal.textMuted)) {
        m_player.setMuted(!muted);
        auto st = m_library.settings();
        st.muted = !muted;
        m_library.updateSettings(st);
        m_library.save();
    }

    rx += 44.0f;
    ImGui::SetCursorPos(ImVec2(rx, centerY - 16.0f));
    const char* repIcon = (m_repeatMode == 0) ? "repeat" : (m_repeatMode == 1 ? "repeat_one" : "repeat_all");
    ImVec4 repCol = (m_repeatMode == 0) ? pal.textMuted : (m_repeatMode == 1 ? pal.accentSecondary : pal.accent);
    if (m_icons.button("##repeat", repIcon, 24.0f, repCol)) {
        m_repeatMode = (m_repeatMode == 0) ? 2 : (m_repeatMode == 2 ? 1 : 0);
        auto st = m_library.settings();
        st.repeatMode = m_repeatMode;
        m_library.updateSettings(st);
        m_library.save();
    }

    rx += 44.0f;
    ImGui::SetCursorPos(ImVec2(rx, centerY - 16.0f));
    if (m_icons.button("##shuffle", "shuffle", 24.0f, m_shuffle ? pal.accent : pal.textMuted)) {
        m_shuffle = !m_shuffle;
        auto st = m_library.settings();
        st.shuffle = m_shuffle;
        m_library.updateSettings(st);
        m_library.save();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_fullscreenMode = !m_fullscreenMode;
    }
}

static std::string browseFileWin32(const wchar_t* filter) {
    wchar_t fileBuf[MAX_PATH] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            std::string out(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, out.data(), len, nullptr, nullptr);
            return out;
        }
    }
    return "";
}

static std::string saveFileWin32(const wchar_t* filter, const wchar_t* defaultName) {
    wchar_t fileBuf[MAX_PATH] = {0};
    if (defaultName) {
        wcsncpy_s(fileBuf, MAX_PATH, defaultName, _TRUNCATE);
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            std::string out(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, out.data(), len, nullptr, nullptr);
            return out;
        }
    }
    return "";
}

void AppUI::renderAddToPlaylistModal() {
    if (m_showAddToPlaylistModal) {
        auto pls = m_library.playlistsCopy();
        m_addToPlIdx = 0;
        for (int i = 0; i < static_cast<int>(pls.size()); ++i) {
            if (pls[i].id == m_selectedPlaylistId) { m_addToPlIdx = i; break; }
        }
        ImGui::OpenPopup("###AddToPlPopup");
        m_showAddToPlaylistModal = false;
    }

    if (ImGui::BeginPopupModal("###AddToPlPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("%s", m_addToPlTrack.title.c_str());
        ImGui::TextDisabled("%s", m_addToPlTrack.artist.c_str());
        ImGui::Separator();

        auto pls = m_library.playlistsCopy();
        if (!pls.empty()) {
            if (m_addToPlIdx >= static_cast<int>(pls.size())) m_addToPlIdx = 0;
            ImGui::Text("Playlist existante :");
            ImGui::SetNextItemWidth(300.0f);
            if (ImGui::BeginCombo("##AddToPlCombo", pls[m_addToPlIdx].name.c_str())) {
                for (int i = 0; i < static_cast<int>(pls.size()); ++i) {
                    bool sel = (m_addToPlIdx == i);
                    if (ImGui::Selectable((pls[i].name + "##atp_" + std::to_string(i)).c_str(), sel)) {
                        m_addToPlIdx = i;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Ajouter à cette playlist")) {
                m_library.addTrackToPlaylist(pls[m_addToPlIdx].id, m_addToPlTrack);
                m_library.save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        ImGui::Text("Ou créer une nouvelle playlist :");
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputTextWithHint("##AddToPlNewName", "Nom de la nouvelle playlist", m_addToPlNewNameBuf, sizeof(m_addToPlNewNameBuf));
        const char* platItems[] = { "YouTube Music", "YouTube", "TikTok", "Local" };
        ImGui::SetNextItemWidth(300.0f);
        ImGui::Combo("Plateforme##AddToPlPlat", &m_addToPlNewPlatIdx, platItems, 4);
        if (ImGui::Button("Créer & Ajouter")) {
            std::string name(m_addToPlNewNameBuf);
            if (name.empty()) name = "Nouvelle playlist";
            Platform plat = static_cast<Platform>(std::clamp(m_addToPlNewPlatIdx, 0, 3));
            std::string newId = m_library.createPlaylist(name, plat, "", false, false, plat == Platform::TikTok);
            m_library.addTrackToPlaylist(newId, m_addToPlTrack);
            m_library.save();
            m_selectedPlaylistId = newId;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler##atp_cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AppUI::renderImportModal() {
    if (m_showCreatePlaylistModal) {
        ImGui::OpenPopup("###CreatePlPopup");
        m_showCreatePlaylistModal = false;
    }
    if (ImGui::BeginPopupModal("###CreatePlPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("Nouvelle playlist");
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputTextWithHint("##NewPlName", "Nom de la playlist", m_newPlNameBuf, sizeof(m_newPlNameBuf));
        const char* platItems[] = { "YouTube Music", "YouTube", "TikTok", "Local" };
        ImGui::SetNextItemWidth(300.0f);
        ImGui::Combo("Plateforme##NewPlPlat", &m_newPlPlatIdx, platItems, 4);
        if (ImGui::Button("Creer##newpl_ok")) {
            std::string name(m_newPlNameBuf);
            if (name.empty()) name = "Nouvelle playlist";
            Platform plat = static_cast<Platform>(std::clamp(m_newPlPlatIdx, 0, 3));
            std::string id = m_library.createPlaylist(name, plat, "", false, false, plat == Platform::TikTok);
            m_library.save();
            m_selectedPlaylistId = id;
            m_showOfflineOnlyView = false;
            m_showLikedOnlyView = false;
            m_activeTab = MainTab::Playlists;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler##newpl_cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (m_showImportModal) {
        ImGui::OpenPopup("###ImportPopup");
        m_showImportModal = false;
    }

    if (ImGui::BeginPopupModal("###ImportPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (ImGui::BeginTabBar("##ImportTabs")) {
            if (ImGui::BeginTabItem("Playlist")) {
                ImGui::SetNextItemWidth(380.0f);
                ImGui::InputTextWithHint("##UrlInput", "URL de la playlist (YT / YT Music / TikTok)", m_urlBuf, sizeof(m_urlBuf));
                ImGui::SetNextItemWidth(380.0f);
                ImGui::InputTextWithHint("##PlName", "Nom (optionnel)", m_plNameBuf, sizeof(m_plNameBuf));

                ImGui::Checkbox("Auto-Sync", &m_newAutoSync);
                ImGui::SameLine();
                ImGui::Checkbox("Auto-DL", &m_newAutoDownload);
                ImGui::SameLine();
                ImGui::Checkbox("Video", &m_newPreferVideo);

                if (ImGui::Button("Importer")) {
                    std::string url(m_urlBuf);
                    if (!url.empty()) {
                        Platform plat = LibraryManager::detectPlatform(url);
                        std::string name = std::string(m_plNameBuf).empty() ? "Nouvelle Playlist" : std::string(m_plNameBuf);
                        std::string id = m_library.createPlaylist(name, plat, url, m_newAutoSync, m_newAutoDownload, m_newPreferVideo);
                        m_library.save();
                        m_selectedPlaylistId = id;
                        m_showOfflineOnlyView = false;
                        m_showLikedOnlyView = false;
                        m_activeTab = MainTab::Playlists;
                        m_sync.syncPlaylistAsync(id);
                        m_urlBuf[0] = '\0';
                        m_plNameBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Link")) {
                ImGui::SetNextItemWidth(380.0f);
                ImGui::InputTextWithHint("##LinkUrl", "Lien direct (YouTube / YT Music / TikTok)", m_linkUrlBuf, sizeof(m_linkUrlBuf));

                auto pls = m_library.playlistsCopy();
                if (!pls.empty()) {
                    if (m_linkTargetPlIdx >= static_cast<int>(pls.size())) m_linkTargetPlIdx = 0;
                    ImGui::SetNextItemWidth(380.0f);
                    if (ImGui::BeginCombo("##LinkPlCombo", pls[m_linkTargetPlIdx].name.c_str())) {
                        for (int i = 0; i < static_cast<int>(pls.size()); ++i) {
                            bool sel = (m_linkTargetPlIdx == i);
                            if (ImGui::Selectable((pls[i].name + "##lkpl_" + std::to_string(i)).c_str(), sel)) {
                                m_linkTargetPlIdx = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::Button("Ajouter##link_add")) {
                    std::string url(m_linkUrlBuf);
                    if (!url.empty()) {
                        Platform plat = LibraryManager::detectPlatform(url);
                        std::string targetPlId;
                        if (!pls.empty() && m_linkTargetPlIdx >= 0 && m_linkTargetPlIdx < static_cast<int>(pls.size())) {
                            targetPlId = pls[m_linkTargetPlIdx].id;
                        } else {
                            targetPlId = m_library.createPlaylist(platformName(plat), plat, "", false, false, plat == Platform::TikTok);
                            m_library.save();
                        }
                        m_sync.searchPlatformAsync(plat, url, 1, [this, targetPlId](bool ok, const std::vector<Track>& res, const std::string&) {
                            if (ok && !res.empty()) {
                                m_library.addTrackToPlaylist(targetPlId, res.front());
                                m_library.save();
                            }
                        });
                        m_selectedPlaylistId = targetPlId;
                        m_showOfflineOnlyView = false;
                        m_showLikedOnlyView = false;
                        m_activeTab = MainTab::Playlists;
                        m_linkUrlBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Annuler##link_cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Local")) {
                ImGui::SetNextItemWidth(336.0f);
                ImGui::InputTextWithHint("##LocalMedia", "Fichier (.mp3 / .m4a / .wav / .mp4)", m_localMediaBuf, sizeof(m_localMediaBuf));
                ImGui::SameLine();
                if (ImGui::Button("...##loc_m")) {
                    std::string p = browseFileWin32(L"Media (*.mp3;*.m4a;*.wav;*.mp4;*.mkv;*.webm)\0*.mp3;*.m4a;*.wav;*.mp4;*.mkv;*.webm\0All Files (*.*)\0*.*\0");
                    if (!p.empty()) std::snprintf(m_localMediaBuf, sizeof(m_localMediaBuf), "%s", p.c_str());
                }

                ImGui::SetNextItemWidth(336.0f);
                ImGui::InputTextWithHint("##LocalCover", "Cover (.jpg / .png) - optionnel", m_localCoverBuf, sizeof(m_localCoverBuf));
                ImGui::SameLine();
                if (ImGui::Button("...##loc_c")) {
                    std::string p = browseFileWin32(L"Images (*.jpg;*.jpeg;*.png;*.webp)\0*.jpg;*.jpeg;*.png;*.webp\0All Files (*.*)\0*.*\0");
                    if (!p.empty()) std::snprintf(m_localCoverBuf, sizeof(m_localCoverBuf), "%s", p.c_str());
                }

                ImGui::SetNextItemWidth(186.0f);
                ImGui::InputTextWithHint("##LocalTitle", "Titre", m_localTitleBuf, sizeof(m_localTitleBuf));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(186.0f);
                ImGui::InputTextWithHint("##LocalArtist", "Artiste", m_localArtistBuf, sizeof(m_localArtistBuf));
                ImGui::Checkbox("Video##loc_vid", &m_localIsVideo);

                if (ImGui::Button("Ajouter##loc_add")) {
                    std::string media(m_localMediaBuf);
                    if (!media.empty() && fs::exists(media)) {
                        std::string targetPl = m_selectedPlaylistId;
                        auto pl = m_library.getPlaylist(targetPl);
                        if (!pl || pl->platform != Platform::Local) targetPl = "local_music";
                        m_library.addLocalTrack(targetPl, media, m_localCoverBuf, m_localTitleBuf, m_localArtistBuf, m_localIsVideo);
                        m_library.save();
                        m_selectedPlaylistId = targetPl;
                        m_showOfflineOnlyView = false;
                        m_showLikedOnlyView = false;
                        m_activeTab = MainTab::Playlists;
                        m_localMediaBuf[0] = '\0';
                        m_localCoverBuf[0] = '\0';
                        m_localTitleBuf[0] = '\0';
                        m_localArtistBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Annuler##loc_cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndPopup();
    }
}

void AppUI::renderRenameModal() {
    if (m_showRenameModal) {
        ImGui::OpenPopup("Renommer");
        m_showRenameModal = false;
    }

    if (ImGui::BeginPopupModal("Renommer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(380.0f);
        ImGui::InputTextWithHint("##RenTitle", m_renameIsPlaylist ? "Nom de la playlist" : "Titre", m_renameTitleBuf, sizeof(m_renameTitleBuf));
        if (!m_renameIsPlaylist) {
            ImGui::SetNextItemWidth(380.0f);
            ImGui::InputTextWithHint("##RenArtist", "Auteur / Artiste", m_renameArtistBuf, sizeof(m_renameArtistBuf));
        }
        if (ImGui::Button("Enregistrer")) {
            if (m_renameIsPlaylist) {
                m_library.renamePlaylist(m_renamePlaylistId, m_renameTitleBuf);
            } else {
                m_library.renameTrack(m_renamePlaylistId, m_renameTrackId, m_renameTitleBuf, m_renameArtistBuf);
                if (m_hasCurrentTrack && m_currentTrack.id == m_renameTrackId) {
                    if (m_renameTitleBuf[0]) m_currentTrack.title = m_renameTitleBuf;
                    if (m_renameArtistBuf[0]) m_currentTrack.artist = m_renameArtistBuf;
                }
            }
            m_library.save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AppUI::renderAudioOverrideModal() {
    if (m_showAudioModal) {
        ImGui::OpenPopup("Audio / dB & EQ");
        m_showAudioModal = false;
    }

    if (ImGui::BeginPopupModal("Audio / dB & EQ", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", m_audioModalLabel.c_str());
        ImGui::Separator();

        ImGui::Checkbox("Activer réglage audio personnalisé", &m_audioModalData.enabled);
        ImGui::BeginDisabled(!m_audioModalData.enabled);
        ImGui::SliderFloat("Gain (dB)", &m_audioModalData.gainDb, -18.0f, 18.0f, "%.1f dB");

        const char* presetNames[] = { "Flat", "YouTube Music", "Spotify", "TikTok", "Custom" };
        int pIdx = static_cast<int>(m_audioModalData.preset);
        if (ImGui::Combo("Preset EQ", &pIdx, presetNames, 5)) {
            m_audioModalData.preset = static_cast<AudioPreset>(pIdx);
            if (m_audioModalData.preset != AudioPreset::Custom) {
                m_audioModalData.eqGainsDb = presetEqGains(m_audioModalData.preset);
            }
        }
        const char* bandLabels[5] = { "60 Hz", "230 Hz", "910 Hz", "3.6 kHz", "14 kHz" };
        for (int i = 0; i < 5; ++i) {
            if (ImGui::SliderFloat(bandLabels[i], &m_audioModalData.eqGainsDb[i], -12.0f, 12.0f, "%.1f dB")) {
                m_audioModalData.preset = AudioPreset::Custom;
            }
        }
        ImGui::EndDisabled();

        if (ImGui::Button("Appliquer & Sauvegarder")) {
            if (m_audioModalIsPlaylist) {
                m_library.setPlaylistAudioOverride(m_audioModalPlaylistId, m_audioModalData);
            } else {
                m_library.setTrackAudioOverride(m_audioModalPlaylistId, m_audioModalTrackId, m_audioModalData);
                if (m_hasCurrentTrack && m_currentTrack.id == m_audioModalTrackId) {
                    m_currentTrack.audioOverride = m_audioModalData;
                }
            }
            if (m_hasCurrentTrack) applyAudioForTrack(m_currentPlaylistId, m_currentTrack);
            m_library.save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Fermer")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
