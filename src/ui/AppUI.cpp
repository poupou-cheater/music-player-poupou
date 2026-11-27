#include "ui/AppUI.hpp"
#include "ui/ImageLoader.hpp"
#include "ui/Templates.hpp"
#include "vulkan/VulkanContext.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

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
             MediaPlayer& player, LyricsManager& lyrics, StatsManager& stats,
             DiscordRPC& discord, MediaKeys& mediaKeys)
    : m_vk(vk), m_icons(icons), m_library(library), m_sync(sync),
      m_player(player), m_lyrics(lyrics), m_stats(stats),
      m_discord(discord), m_mediaKeys(mediaKeys) {}

AppUI::~AppUI() = default;

void AppUI::init(GLFWwindow* window) {
    m_window = window;
    auto s = m_library.settings();
    applyTemplateStyle(s.activeTemplate, m_vk);
    m_player.setVolume(s.volume);
    m_player.setMuted(s.muted);
    m_player.setEqGains(s.eqGainsDb);
    m_shuffle = s.shuffle;
    m_repeatMode = s.repeatMode;
    m_viewMode = s.viewMode;
    m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
    m_discord.setEnabled(s.enableDiscordRPC, s.discordClientId);
    createDefaultCover();

    auto lists = m_library.playlistsCopy();
    if (!s.selectedPlaylistId.empty() && m_library.getPlaylist(s.selectedPlaylistId)) {
        m_selectedPlaylistId = s.selectedPlaylistId;
    } else if (!lists.empty()) {
        m_selectedPlaylistId = lists.front().id;
    }

    if (!s.offlineMode) m_sync.syncAllAutoPlaylistsAsync();
    else m_sync.cleanupOrphanCacheAsync();
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

ImTextureID AppUI::getTrackThumb(const Track& t) {
    if (t.localThumbnailPath.empty() || !fs::exists(t.localThumbnailPath)) {
        return m_defaultCoverTexture.imguiId();
    }
    auto it = m_thumbCache.find(t.id);
    if (it != m_thumbCache.end() && it->second.isValid()) {
        return it->second.imguiId();
    }
    if (m_thumbCache.size() > 1200 || m_thumbsUploadedThisFrame >= 4) {
        return m_defaultCoverTexture.imguiId();
    }
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (loadImageRGBA(t.localThumbnailPath, rgba, w, h)) {
        VulkanTexture tex;
        if (tex.createFromRGBA(m_vk, rgba.data(), w, h)) {
            ++m_thumbsUploadedThisFrame;
            m_thumbCache[t.id] = tex;
            return m_thumbCache[t.id].imguiId();
        }
    }
    return m_defaultCoverTexture.imguiId();
}

void AppUI::playTrack(const std::string& playlistId, const Track& track) {
    bool offline = m_library.offlineMode();
    if (!track.localMediaPath.empty() && fs::exists(track.localMediaPath)) {
        m_currentPlaylistId = playlistId;
        m_currentTrack = track;
        m_hasCurrentTrack = true;
        m_preloadedForTrackId.clear();
        loadCoverImage(track.localThumbnailPath);
        m_stats.recordPlayStart(track);
        m_lyrics.fetchForTrackAsync(track.id, track.title, track.artist, track.durationSec, offline);
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
            if (nextIdx == 0 && m_repeatMode != 2) return; // Stop at end unless Repeat Playlist
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
        for (int k = 32; k <= 348; ++k) {
            if (glfwGetKey(m_window, k) == GLFW_PRESS) {
                auto s = m_library.settings();
                switch (m_rebindingSlot) {
                    case 0: s.keyPlayPause = k; break;
                    case 1: s.keyMute = k; break;
                    case 2: s.keyFullscreen = k; break;
                    case 3: s.keyLyrics = k; break;
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
        static std::unordered_map<int, bool> prev;
        bool down = glfwGetKey(win, key) == GLFW_PRESS;
        bool edge = down && !prev[key];
        prev[key] = down;
        return edge;
    };

    if (pressedOnce(s.keyPlayPause) && m_player.isOpen()) m_player.togglePlayPause();
    if (pressedOnce(s.keyMute)) m_player.setMuted(!m_player.isMuted());
    if (pressedOnce(s.keyFullscreen)) m_fullscreenMode = !m_fullscreenMode;
    if (pressedOnce(s.keyLyrics)) m_lyricsMode = !m_lyricsMode;
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
}

void AppUI::update(double dtSeconds) {
    m_thumbsUploadedThisFrame = 0;
    handleShortcuts();

    // Mouse idle detection for 3s auto-hide in fullscreen
    ImVec2 mp = ImGui::GetMousePos();
    if (mp.x != m_lastMousePos.x || mp.y != m_lastMousePos.y || ImGui::IsAnyMouseDown()) {
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
            loadCoverImage(m_currentTrack.localThumbnailPath);
            m_stats.recordPlayStart(m_currentTrack);
            m_lyrics.fetchForTrackAsync(m_currentTrack.id, m_currentTrack.title, m_currentTrack.artist,
                                        m_currentTrack.durationSec, m_library.offlineMode());
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
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::Begin("##MainRoot", nullptr, rootFlags);
    ImGui::PopStyleVar();

    auto settings = m_library.settings();
    bool hideBars = m_fullscreenMode && settings.autoHideBarsFullscreen && (m_idleSeconds >= 3.0);

    if (!hideBars) {
        renderTopBar();
        ImGui::Spacing();
    }

    float playerBarHeight = hideBars ? 0.0f : 90.0f;
    float contentHeight = ImGui::GetContentRegionAvail().y - (hideBars ? 0.0f : (playerBarHeight + 8.0f));
    if (contentHeight < 180.0f) contentHeight = 180.0f;

    if (m_fullscreenMode) {
        ImGui::BeginChild("##FullscreenStage", ImVec2(0.0f, contentHeight), false);
        renderStagePanel(true);
        ImGui::EndChild();
    } else if (m_activeTab == MainTab::Playlists) {
        ImGui::BeginChild("##Sidebar", ImVec2(240.0f, contentHeight), true);
        renderSidebar();
        ImGui::EndChild();

        ImGui::SameLine();

        float availW = ImGui::GetContentRegionAvail().x;
        float stageW = 340.0f;
        float centerW = availW - stageW - 10.0f;
        if (centerW < 300.0f) centerW = 300.0f;

        ImGui::BeginChild("##CenterMain", ImVec2(centerW, contentHeight), true);
        renderPlaylistsTab();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##StagePanel", ImVec2(0.0f, contentHeight), true);
        renderStagePanel(false);
        ImGui::EndChild();
    } else {
        float availW = ImGui::GetContentRegionAvail().x;
        float stageW = 320.0f;
        float centerW = availW - stageW - 10.0f;
        if (centerW < 360.0f) centerW = 360.0f;

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

        ImGui::SameLine();

        ImGui::BeginChild("##StagePanel", ImVec2(0.0f, contentHeight), true);
        renderStagePanel(false);
        ImGui::EndChild();
    }

    if (!hideBars) {
        ImGui::Spacing();
        ImGui::BeginChild("##PlayerBar", ImVec2(0.0f, playerBarHeight), true);
        renderPlayerBar();
        ImGui::EndChild();
    }

    renderImportModal();
    renderRenameModal();
    ImGui::End();
}

void AppUI::renderTopBar() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
    ImGui::BeginChild("##TopBar", ImVec2(0.0f, 52.0f), true, ImGuiWindowFlags_NoScrollbar);

    // Left: YouTube Music style red circle + brand
    m_icons.draw("youtubemusic", 24.0f, pal.accent);
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetWindowFontScale(1.12f);
    ImGui::TextUnformatted("StreamPulse");
    ImGui::SetWindowFontScale(1.0f);

    // Center-Left: Clean YouTube Music navigation pills
    ImGui::SameLine(170.0f);
    auto tabBtn = [&](MainTab tab, const char* label, const char* icon) {
        bool active = (m_activeTab == tab);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? pal.accent : ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
        m_icons.draw(icon, 16.0f, active ? ImVec4(1, 1, 1, 1) : pal.textMuted);
        ImGui::SameLine(0.0f, 5.0f);
        if (ImGui::Button(label)) {
            m_activeTab = tab;
            m_fullscreenMode = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
    };

    tabBtn(MainTab::Playlists, "Accueil", "playlist");
    tabBtn(MainTab::YouTubeMusic, "YT Music", "youtubemusic");
    tabBtn(MainTab::TikTok, "TikTok", "tiktok");
    tabBtn(MainTab::Queue, ("File (" + std::to_string(m_queue.size()) + ")").c_str(), "queue");
    tabBtn(MainTab::Downloads, "Downloads", "download");
    tabBtn(MainTab::Stats, "Stats", "stats");

    // Right: Aligned utility controls
    float rightStart = std::max(720.0f, ImGui::GetWindowWidth() - 405.0f);
    ImGui::SameLine(rightStart);
    ImGui::SetNextItemWidth(105.0f);
    const char* tplNames[] = { "YT Music", "Spotify", "TikTok", "Minimal" };
    int curTpl = static_cast<int>(m_library.activeTemplate());
    if (ImGui::Combo("##TemplateCombo", &curTpl, tplNames, 4)) {
        auto nextTpl = static_cast<GuiTemplate>(curTpl);
        m_library.setActiveTemplate(nextTpl);
        m_library.save();
        applyTemplateStyle(nextTpl, m_vk);
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (ImGui::Button("+ Importer")) m_showImportModal = true;
    ImGui::SameLine(0.0f, 8.0f);
    if (m_icons.button("##top_settings", "settings", 18.0f, m_activeTab == MainTab::Settings ? pal.accent : pal.textPrimary)) {
        m_activeTab = MainTab::Settings;
        m_fullscreenMode = false;
    }
    ImGui::SameLine(0.0f, 8.0f);

    bool offline = m_library.offlineMode();
    bool netOk = m_sync.isNetworkAvailable();
    ImVec4 btnCol = offline
        ? ImVec4(0.85f, 0.35f, 0.15f, 1.0f)
        : (netOk ? ImVec4(0.14f, 0.55f, 0.30f, 1.0f) : ImVec4(0.55f, 0.45f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
    if (ImGui::Button(offline ? "Hors Ligne" : "En Ligne")) {
        m_library.setOfflineMode(!offline);
        m_library.save();
        if (offline) m_sync.refreshNetworkStatusAsync();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AppUI::renderSidebar() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());

    m_icons.draw("downloaded", 18.0f, pal.accentSecondary);
    ImGui::SameLine();
    if (ImGui::Selectable("Offline Downloads", m_showOfflineOnlyView)) m_showOfflineOnlyView = true;

    if (m_library.canUndoDeletePlaylist()) {
        ImGui::Spacing();
        if (ImGui::Button("Annuler suppression (Undo)", ImVec2(-1.0f, 0.0f))) {
            std::string restored = m_library.undoDeletePlaylist();
            if (!restored.empty()) {
                m_selectedPlaylistId = restored;
                m_showOfflineOnlyView = false;
                m_library.save();
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("PLATFORMS & PLAYLISTS");
    ImGui::Spacing();

    auto playlists = m_library.playlistsCopy();
    const Platform order[] = { Platform::YouTubeMusic, Platform::YouTube, Platform::TikTok, Platform::Local };

    for (Platform plat : order) {
        std::vector<Playlist> group;
        for (const auto& p : playlists) {
            if (p.platform == plat) group.push_back(p);
        }

        m_icons.draw(platformIconKey(plat), 18.0f, pal.accent);
        ImGui::SameLine();
        if (ImGui::TreeNodeEx(platformName(plat), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (group.empty()) ImGui::TextDisabled("  (No playlists)");
            for (const auto& pl : group) {
                bool selected = (!m_showOfflineOnlyView && m_selectedPlaylistId == pl.id);
                m_icons.draw("playlist", 15.0f, selected ? pal.accent : pal.textMuted);
                ImGui::SameLine();
                std::string label = pl.name + " (" + std::to_string(pl.tracks.size()) + ")##" + pl.id;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    m_showOfflineOnlyView = false;
                    m_selectedPlaylistId = pl.id;
                    auto st = m_library.settings();
                    st.selectedPlaylistId = pl.id;
                    m_library.updateSettings(st);
                    m_library.save();
                }
            }
            ImGui::TreePop();
        }
    }
}

void AppUI::renderPlaylistsTab() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());

    m_icons.draw("search", 18.0f, pal.textMuted);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##PlFilter", "Filter playlist tracks...", m_searchBuf, sizeof(m_searchBuf));
    std::string filter(m_searchBuf);

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
    ImGui::Text("%s", pl.name.c_str());

    bool autoSync = pl.autoSync, autoDl = pl.autoDownload, prefVid = pl.preferVideo;
    bool optsChanged = false;
    if (pl.platform != Platform::Local) {
        if (ImGui::Checkbox("Auto-Sync", &autoSync)) optsChanged = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto-Download", &autoDl)) optsChanged = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Prefer Video", &prefVid)) optsChanged = true;
        if (optsChanged) {
            m_library.setPlaylistOptions(pl.id, autoSync, autoDl, prefVid);
            m_library.save();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_library.offlineMode() || pl.sourceUrl.empty());
        if (ImGui::Button("Sync")) m_sync.syncPlaylistAsync(pl.id);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_library.offlineMode() || pl.tracks.empty());
        if (ImGui::Button("Download All")) m_sync.queuePlaylistDownload(pl.id, prefVid);
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    const char* viewModes[] = { "Liste", "Grandes Cartes", "Compact" };
    ImGui::SetNextItemWidth(125.0f);
    if (ImGui::Combo("##ViewMode", &m_viewMode, viewModes, 3)) {
        auto st = m_library.settings();
        st.viewMode = m_viewMode;
        m_library.updateSettings(st);
        m_library.save();
    }

    ImGui::SameLine();
    if (ImGui::Button("Renommer")) {
        m_showRenameModal = true;
        m_renameIsPlaylist = true;
        m_renamePlaylistId = pl.id;
        std::snprintf(m_renameTitleBuf, sizeof(m_renameTitleBuf), "%s", pl.name.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Suppr. locaux")) {
        m_library.clearPlaylistLocalFiles(pl.id);
        m_library.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        m_library.deletePlaylist(pl.id);
        m_library.save();
        auto lists = m_library.playlistsCopy();
        m_selectedPlaylistId = lists.empty() ? "" : lists.front().id;
        return;
    }

    ImGui::Separator();

    bool offline = m_library.offlineMode();

    if (m_viewMode == 1) {
        // Grandes Cartes (YouTube Music Home/Album Grid)
        ImGui::BeginChild("##GridScroll", ImVec2(0.0f, 0.0f), false);
        float cardW = 168.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        int cols = std::max(1, static_cast<int>(avail / (cardW + 14.0f)));
        int colIdx = 0;
        for (size_t i = 0; i < pl.tracks.size(); ++i) {
            const Track& t = pl.tracks[i];
            if (!matchesFilter(t, filter)) continue;
            bool isDownloaded = (t.downloadState == DownloadState::Downloaded && !t.localMediaPath.empty());
            bool unavailableOffline = offline && !isDownloaded;

            if (colIdx > 0) ImGui::SameLine(0.0f, 14.0f);
            ImGui::BeginGroup();
            ImGui::BeginDisabled(unavailableOffline);
            if (ImGui::ImageButton(("##gbtn_" + t.id).c_str(), getTrackThumb(t), ImVec2(cardW - 8.0f, cardW - 8.0f))) {
                playTrack(pl.id, t);
            }
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
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - 8.0f);
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

    float rowH = (m_viewMode == 2) ? 34.0f : 56.0f;
    float thumbSz = (m_viewMode == 2) ? 28.0f : 48.0f;

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
    if (searching) {
        ImGui::SameLine();
        ImGui::TextDisabled("Recherche en cours...");
    }

    ImGui::Separator();

    std::lock_guard<std::mutex> lock(m_searchMutex);
    float tableH = ImGui::GetContentRegionAvail().y - 38.0f;
    if (tableH < 120.0f) tableH = 120.0f;
    if (ImGui::BeginTable(isTT ? "##TTTable" : "##YTMTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, tableH))) {
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
                if (ImGui::MenuItem("Ajouter à la playlist sélectionnée")) {
                    m_library.addTrackToPlaylist(m_selectedPlaylistId.empty() ? "ytm_favorites" : m_selectedPlaylistId, t);
                    m_library.save();
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
                m_library.addTrackToPlaylist(m_selectedPlaylistId.empty() ? "ytm_favorites" : m_selectedPlaylistId, t);
                m_library.save();
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
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    AppSettings s = m_library.settings();
    bool changed = false;

    m_icons.draw("equalizer", 20.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("Audio Mixer & Equalizer Presets");
    ImGui::Separator();

    const char* presetNames[] = { "Flat", "YouTube Music", "Spotify", "TikTok", "Custom" };
    int pIdx = static_cast<int>(s.audioPreset);
    if (ImGui::Combo("Audio Preset", &pIdx, presetNames, 5)) {
        s.audioPreset = static_cast<AudioPreset>(pIdx);
        if (s.audioPreset != AudioPreset::Custom) {
            s.eqGainsDb = presetEqGains(s.audioPreset);
        }
        m_player.setEqGains(s.eqGainsDb);
        changed = true;
    }

    const char* bandLabels[5] = { "60 Hz (Sub)", "230 Hz (Bass)", "910 Hz (Mid)", "3.6 kHz (Presence)", "14 kHz (Treble)" };
    for (int i = 0; i < 5; ++i) {
        if (ImGui::SliderFloat(bandLabels[i], &s.eqGainsDb[i], -12.0f, 12.0f, "%.1f dB")) {
            s.audioPreset = AudioPreset::Custom;
            m_player.setEqGains(s.eqGainsDb);
            changed = true;
        }
    }

    ImGui::Spacing();
    m_icons.draw("settings", 20.0f, pal.accent);
    ImGui::SameLine();
    ImGui::Text("Behavior, Discord & Keybinds");
    ImGui::Separator();

    if (ImGui::Checkbox("Auto-hide top & bottom bars after 3s in Fullscreen", &s.autoHideBarsFullscreen)) changed = true;
    if (ImGui::Checkbox("Enable Global Media Keys (Play/Pause, Next, Prev)", &s.enableMediaKeys)) {
        m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
        changed = true;
    }
    if (ImGui::Checkbox("Exclusive Media Keys (Only control StreamPulse)", &s.mediaKeysExclusive)) {
        m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
        changed = true;
    }
    if (ImGui::Checkbox("Force Focus app when Media Key pressed", &s.forceFocusOnMediaKey)) {
        m_mediaKeys.setConfig(s.enableMediaKeys, s.mediaKeysExclusive, s.forceFocusOnMediaKey);
        changed = true;
    }
    if (ImGui::Button("Force Focus Now")) {
        m_mediaKeys.focusAppWindow();
    }
    ImGui::SameLine();
    if (ImGui::Button("Nettoyer cache orphelin")) {
        m_sync.cleanupOrphanCacheAsync();
    }
    if (ImGui::Checkbox("Enable Discord Rich Presence", &s.enableDiscordRPC)) {
        m_discord.setEnabled(s.enableDiscordRPC, s.discordClientId);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Text("Keybinds (click a button then press any key):");
    auto keybindRow = [&](const char* label, int slot, int keyCode) {
        ImGui::Text("%-18s", label);
        ImGui::SameLine(180.0f);
        std::string btnTxt = (m_rebindingSlot == slot) ? "Press a key..." : MediaKeys::keyName(keyCode);
        if (ImGui::Button((btnTxt + "##kb_" + std::to_string(slot)).c_str(), ImVec2(140.0f, 0.0f))) {
            m_rebindingSlot = slot;
        }
    };
    keybindRow("Play / Pause", 0, s.keyPlayPause);
    keybindRow("Mute", 1, s.keyMute);
    keybindRow("Fullscreen", 2, s.keyFullscreen);
    keybindRow("Lyrics Mode", 3, s.keyLyrics);
    keybindRow("Seek -5s", 4, s.keySeekBack);
    keybindRow("Seek +5s", 5, s.keySeekFwd);
    keybindRow("Volume +5%", 6, s.keyVolUp);
    keybindRow("Volume -5%", 7, s.keyVolDown);

    if (changed) {
        m_library.updateSettings(s);
        m_library.save();
    }
}

void AppUI::renderLyricsView(float height) {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());
    ImVec4 cardBg = ImVec4(pal.accent.x * 0.22f + 0.07f, pal.accent.y * 0.22f + 0.07f, pal.accent.z * 0.22f + 0.10f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::BeginChild("##LyricsScroll", ImVec2(0.0f, height), true);

    if (m_lyrics.isLoading()) {
        ImGui::TextDisabled("Chargement des paroles...");
    } else if (!m_lyrics.hasLyrics()) {
        ImGui::TextDisabled("%s", m_lyrics.statusText().c_str());
    } else {
        auto lines = m_lyrics.linesCopy();
        double pos = m_player.positionSeconds();
        int active = m_lyrics.activeIndex(pos);

        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            if (i == active) {
                // Spotify-style word-by-word highlight on the active line
                ImGui::SetWindowFontScale(1.38f);
                std::vector<std::string> words;
                std::string cur;
                for (char c : lines[i].text) {
                    if (c == ' ') {
                        if (!cur.empty()) { words.push_back(cur); cur.clear(); }
                    } else {
                        cur.push_back(c);
                    }
                }
                if (!cur.empty()) words.push_back(cur);

                double lineStart = lines[i].timeSec;
                double lineEnd = (i + 1 < static_cast<int>(lines.size()) && lines[i + 1].timeSec > lineStart)
                    ? lines[i + 1].timeSec
                    : (lineStart + std::max(1.5, words.size() * 0.35));
                double lineDur = std::max(0.4, lineEnd - lineStart);
                double frac = std::clamp((pos - lineStart) / lineDur, 0.0, 1.0);
                int activeWord = words.empty() ? 0 : static_cast<int>(frac * static_cast<double>(words.size()));

                float maxX = ImGui::GetWindowContentRegionMax().x;
                for (int w = 0; w < static_cast<int>(words.size()); ++w) {
                    ImVec4 wCol = (w <= activeWord)
                        ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                        : ImVec4(1.0f, 1.0f, 1.0f, 0.38f);
                    ImGui::PushStyleColor(ImGuiCol_Text, wCol);
                    ImGui::TextUnformatted(words[w].c_str());
                    ImGui::PopStyleColor();
                    if (w + 1 < static_cast<int>(words.size())) {
                        float nextW = ImGui::CalcTextSize(words[w + 1].c_str()).x;
                        if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + nextW + 14.0f < maxX) {
                            ImGui::SameLine(0.0f, 6.0f);
                        }
                    }
                }
                ImGui::SetWindowFontScale(1.0f);
                if (m_player.isPlaying()) ImGui::SetScrollHereY(0.42f);
                ImGui::Spacing();
            } else {
                float alpha = 0.42f;
                if (active >= 0) {
                    int d = std::abs(i - active);
                    if (d == 1) alpha = 0.62f;
                    else if (d == 2) alpha = 0.42f;
                    else alpha = 0.26f;
                }
                ImGui::SetWindowFontScale(1.10f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
                if (ImGui::Selectable((lines[i].text + "##lyr_" + std::to_string(i)).c_str(), false)) {
                    if (lines[i].timeSec >= 0.0) m_player.seek(lines[i].timeSec);
                }
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(1.0f);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void AppUI::renderStagePanel(bool isFullscreen) {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());

    m_icons.draw(m_player.hasVideo() ? "video" : "music", 18.0f, pal.accent);
    ImGui::SameLine();
    ImGui::TextColored(pal.accent, isFullscreen ? "PLEIN ECRAN" : "LECTURE");
    ImGui::SameLine(ImGui::GetWindowWidth() - 56.0f);
    if (m_icons.button("##stage_fs", isFullscreen ? "fullscreen_exit" : "fullscreen", 18.0f, pal.textPrimary)) {
        m_fullscreenMode = !m_fullscreenMode;
    }
    ImGui::Separator();

    float availW = ImGui::GetContentRegionAvail().x;
    float totalH = ImGui::GetContentRegionAvail().y;
    float mediaH = isFullscreen ? (totalH - 70.0f) : std::min(availW * 0.72f, totalH * 0.46f);
    if (mediaH < 120.0f) mediaH = 120.0f;

    if (isFullscreen && m_lyricsMode) {
        renderLyricsView(mediaH);
    } else if (m_player.hasVideo() && m_videoTexture.isValid()) {
        float aspect = static_cast<float>(m_videoTexture.width()) / static_cast<float>(m_videoTexture.height());
        float drawW = availW;
        float drawH = drawW / aspect;
        if (drawH > mediaH) { drawH = mediaH; drawW = drawH * aspect; }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - drawW) * 0.5f);
        ImGui::Image(m_videoTexture.imguiId(), ImVec2(drawW, drawH));
    } else {
        ImTextureID coverId = m_coverTexture.isValid() ? m_coverTexture.imguiId() : m_defaultCoverTexture.imguiId();
        float side = std::min(availW, mediaH);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - side) * 0.5f);
        ImGui::Image(coverId, ImVec2(side, side));
    }

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
            if (ImGui::BeginTabItem("Paroles")) {
                float lH = ImGui::GetContentRegionAvail().y;
                renderLyricsView(lH);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
}

void AppUI::renderPlayerBar() {
    TemplatePalette pal = getTemplatePalette(m_library.activeTemplate());

    // Top edge: Full-width YouTube Music Red Seek Bar
    double pos = m_player.positionSeconds();
    double dur = m_player.durationSeconds();
    float posF = static_cast<float>(pos);
    float maxF = (dur > 0.1) ? static_cast<float>(dur) : 100.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##Seek", &posF, 0.0f, maxF, "")) {
        if (dur > 0.1) m_player.seek(static_cast<double>(posF));
    }
    ImGui::PopStyleVar(2);

    // Left: Prev / Play-Pause / Next + Time
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
    if (m_icons.button("##prev", "prev", 22.0f, pal.textPrimary)) playPrev();
    ImGui::SameLine(0.0f, 8.0f);
    if (m_icons.button("##playpause", m_player.isPlaying() ? "pause" : "play", 28.0f, pal.accent)) {
        if (m_player.isOpen()) m_player.togglePlayPause();
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (m_icons.button("##next", "next", 22.0f, pal.textPrimary)) playNext();
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("%s / %s", formatTime(pos).c_str(), dur > 0.1 ? formatTime(dur).c_str() : "--:--");

    // Center: Cover + Title + Artist (clamped so it never overlaps right controls)
    float winW = ImGui::GetWindowWidth();
    if (m_hasCurrentTrack) {
        ImGui::SameLine(winW * 0.30f);
        ImTextureID barCov = m_coverTexture.isValid() ? m_coverTexture.imguiId() : m_defaultCoverTexture.imguiId();
        ImGui::Image(barCov, ImVec2(42.0f, 42.0f));
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::BeginChild("##BarMeta", ImVec2(winW * 0.34f, 44.0f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted(m_currentTrack.title.c_str());
        ImGui::TextDisabled("%s", m_currentTrack.artist.c_str());
        ImGui::EndChild();
    }

    // Right: Volume % + Shuffle + Repeat (Off/1/All) + Fullscreen
    ImGui::SameLine(std::max(560.0f, winW - 395.0f));
    bool muted = m_player.isMuted();
    if (m_icons.button("##mute", muted ? "volume_mute" : "volume_high", 18.0f, pal.textPrimary)) {
        m_player.setMuted(!muted);
        auto st = m_library.settings();
        st.muted = !muted;
        m_library.updateSettings(st);
        m_library.save();
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::SmallButton("-")) {
        float v = std::max(0.0f, m_player.volume() - 0.05f);
        m_player.setVolume(v);
        m_library.setVolume(v);
        m_library.save();
    }
    ImGui::SameLine(0.0f, 4.0f);
    float volPct = m_player.volume() * 100.0f;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::SliderFloat("##Vol", &volPct, 0.0f, 150.0f, "%.0f%%")) {
        m_player.setVolume(volPct / 100.0f);
        m_library.setVolume(volPct / 100.0f);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) m_library.save();
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::SmallButton("+")) {
        float v = std::min(1.5f, m_player.volume() + 0.05f);
        m_player.setVolume(v);
        m_library.setVolume(v);
        m_library.save();
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (m_icons.button("##shuffle", "shuffle", 18.0f, m_shuffle ? pal.accent : pal.textMuted)) {
        m_shuffle = !m_shuffle;
        auto st = m_library.settings();
        st.shuffle = m_shuffle;
        m_library.updateSettings(st);
        m_library.save();
    }
    ImGui::SameLine(0.0f, 6.0f);
    ImVec4 repCol = (m_repeatMode == 0) ? pal.textMuted : (m_repeatMode == 1 ? pal.accentSecondary : pal.accent);
    if (m_icons.button("##repeat", "repeat", 18.0f, repCol)) {
        m_repeatMode = (m_repeatMode + 1) % 3;
        auto st = m_library.settings();
        st.repeatMode = m_repeatMode;
        m_library.updateSettings(st);
        m_library.save();
    }
    if (ImGui::IsItemHovered()) {
        const char* repTip = (m_repeatMode == 0) ? "Repeat: Rien" : (m_repeatMode == 1 ? "Repeat: Même titre (1)" : "Repeat: Playlist / File (All)");
        ImGui::SetTooltip("%s", repTip);
    }
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextColored(repCol, m_repeatMode == 0 ? "" : (m_repeatMode == 1 ? "1" : "All"));
    ImGui::SameLine(0.0f, 6.0f);
    if (m_icons.button("##bar_fs", m_fullscreenMode ? "fullscreen_exit" : "fullscreen", 18.0f, pal.textPrimary)) {
        m_fullscreenMode = !m_fullscreenMode;
    }
}

void AppUI::renderImportModal() {
    if (m_showImportModal) {
        ImGui::OpenPopup("Import Playlist / Media");
        m_showImportModal = false;
    }

    if (ImGui::BeginPopupModal("Import Playlist / Media", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTabBar("##ImportTabs")) {
            if (ImGui::BeginTabItem("Platform URL (YT / YT Music / TikTok)")) {
                ImGui::SetNextItemWidth(440.0f);
                ImGui::InputTextWithHint("##UrlInput", "https://music.youtube.com/playlist?list=...", m_urlBuf, sizeof(m_urlBuf));
                ImGui::SetNextItemWidth(440.0f);
                ImGui::InputTextWithHint("##PlName", "Playlist name (optional)", m_plNameBuf, sizeof(m_plNameBuf));

                ImGui::Checkbox("Auto-Update Playlist", &m_newAutoSync);
                ImGui::SameLine();
                ImGui::Checkbox("Auto-Download New Tracks", &m_newAutoDownload);
                ImGui::SameLine();
                ImGui::Checkbox("Prefer Video (MP4)", &m_newPreferVideo);

                if (ImGui::Button("Import & Sync")) {
                    std::string url(m_urlBuf);
                    if (!url.empty()) {
                        Platform plat = LibraryManager::detectPlatform(url);
                        std::string name = std::string(m_plNameBuf).empty() ? "New Playlist" : std::string(m_plNameBuf);
                        std::string id = m_library.createPlaylist(name, plat, url, m_newAutoSync, m_newAutoDownload, m_newPreferVideo);
                        m_library.save();
                        m_selectedPlaylistId = id;
                        m_showOfflineOnlyView = false;
                        m_activeTab = MainTab::Playlists;
                        m_sync.syncPlaylistAsync(id);
                        m_urlBuf[0] = '\0';
                        m_plNameBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Local Music (+ 1 Image) / Video")) {
                ImGui::SetNextItemWidth(440.0f);
                ImGui::InputTextWithHint("##LocalMedia", "Path to .mp3 / .m4a / .wav / .mp4", m_localMediaBuf, sizeof(m_localMediaBuf));
                ImGui::SetNextItemWidth(440.0f);
                ImGui::InputTextWithHint("##LocalCover", "Path to 1 cover image (.jpg / .png) - optional", m_localCoverBuf, sizeof(m_localCoverBuf));
                ImGui::SetNextItemWidth(440.0f);
                ImGui::InputTextWithHint("##LocalTitle", "Title", m_localTitleBuf, sizeof(m_localTitleBuf));
                ImGui::SetNextItemWidth(440.0f);
                ImGui::InputTextWithHint("##LocalArtist", "Artist", m_localArtistBuf, sizeof(m_localArtistBuf));
                ImGui::Checkbox("This file is a Video", &m_localIsVideo);

                if (ImGui::Button("Add Local Track")) {
                    std::string media(m_localMediaBuf);
                    if (!media.empty() && fs::exists(media)) {
                        std::string targetPl = m_selectedPlaylistId;
                        auto pl = m_library.getPlaylist(targetPl);
                        if (!pl || pl->platform != Platform::Local) targetPl = "local_music";
                        m_library.addLocalTrack(targetPl, media, m_localCoverBuf, m_localTitleBuf, m_localArtistBuf, m_localIsVideo);
                        m_library.save();
                        m_selectedPlaylistId = targetPl;
                        m_showOfflineOnlyView = false;
                        m_activeTab = MainTab::Playlists;
                        m_localMediaBuf[0] = '\0';
                        m_localCoverBuf[0] = '\0';
                        m_localTitleBuf[0] = '\0';
                        m_localArtistBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
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
