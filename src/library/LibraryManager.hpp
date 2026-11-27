#pragma once

#include "library/Models.hpp"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct PlaylistSummary {
    std::string id;
    std::string name;
    Platform platform = Platform::YouTubeMusic;
    bool favorite = false;
    size_t trackCount = 0;
};

class LibraryManager {
public:
    void load(const std::string& jsonPath = "data/library.json");
    void save() const;

    static Platform detectPlatform(const std::string& url);
    static std::string sanitizeId(const std::string& raw);

    std::vector<Playlist> playlistsCopy() const;
    std::vector<PlaylistSummary> playlistSummaries() const;
    std::optional<Playlist> getPlaylist(const std::string& playlistId) const;
    std::optional<Track> getTrack(const std::string& playlistId, const std::string& trackId) const;

    std::string createPlaylist(const std::string& name, Platform platform, const std::string& sourceUrl,
                               bool autoSync, bool autoDownload, bool preferVideo);
    void deletePlaylist(const std::string& playlistId);
    bool canUndoDeletePlaylist() const;
    std::string undoDeletePlaylist();
    void clearPlaylistLocalFiles(const std::string& playlistId);
    void renamePlaylist(const std::string& playlistId, const std::string& newName);
    void togglePlaylistFavorite(const std::string& playlistId);
    void reorderPlaylist(const std::string& draggedId, const std::string& targetId);
    void movePlaylistToPlatform(const std::string& playlistId, Platform plat);
    void setPlaylistOptions(const std::string& playlistId, bool autoSync, bool autoDownload, bool preferVideo);
    void mergeSyncedPlaylist(const std::string& playlistId, const std::string& resolvedName,
                             const std::vector<Track>& fetchedTracks, const std::string& timestamp);

    void addTrackToPlaylist(const std::string& playlistId, const Track& track);
    void renameTrack(const std::string& playlistId, const std::string& trackId,
                     const std::string& newTitle, const std::string& newArtist);
    void setPlaylistAudioOverride(const std::string& playlistId, const AudioOverride& ov);
    void setTrackAudioOverride(const std::string& playlistId, const std::string& trackId, const AudioOverride& ov);
    void moveTrackInPlaylist(const std::string& playlistId, size_t fromIdx, size_t toIdx);
    void removeTrackFromPlaylist(const std::string& playlistId, const std::string& trackId);
    void clearTrackLocalMedia(const std::string& playlistId, const std::string& trackId);

    void addLocalTrack(const std::string& playlistId, const std::string& mediaPath,
                       const std::string& coverImagePath, const std::string& title,
                       const std::string& artist, bool isVideo);

    void updateTrackDownload(const std::string& playlistId, const std::string& trackId,
                             DownloadState state, const std::string& localPath = "");
    void updateTrackThumbnail(const std::string& playlistId, const std::string& trackId,
                              const std::string& localThumbPath);
    void toggleTrackLike(const std::string& playlistId, const std::string& trackId);

    AppSettings settings() const;
    void updateSettings(const AppSettings& s);
    bool exportSettingsToFile(const std::string& path) const;
    bool importSettingsFromFile(const std::string& path);

    static std::string exportThemeText(const CustomTheme& t);
    static std::optional<CustomTheme> importThemeText(const std::string& txt);
    static std::string exportAudioProfileText(const CustomAudioProfile& a);
    static std::optional<CustomAudioProfile> importAudioProfileText(const std::string& txt);

    bool offlineMode() const;
    void setOfflineMode(bool offline);

    GuiTemplate activeTemplate() const;
    void setActiveTemplate(GuiTemplate tpl);

    float volume() const;
    void setVolume(float vol);

private:
    void ensureDefaultPlaylists();

    mutable std::mutex m_mtx;
    std::string m_jsonPath = "data/library.json";
    mutable std::string m_lastSavedDump;
    std::vector<Playlist> m_playlists;
    std::vector<Playlist> m_deletedPlaylists;
    AppSettings m_settings;
};
