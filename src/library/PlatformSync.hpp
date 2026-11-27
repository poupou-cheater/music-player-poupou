#pragma once

#include "library/LibraryManager.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct DownloadItem {
    std::string playlistId;
    std::string trackId;
    std::string title;
    std::string artist;
    std::string sourceUrl;
    Platform platform = Platform::YouTubeMusic;
    bool asVideo = false;
    DownloadState state = DownloadState::Queued;
    float progressPct = 0.0f;
    std::string speed;
    std::string eta;
    std::string outputPath;
    std::string errorMsg;
};

class PlatformSync {
public:
    explicit PlatformSync(LibraryManager& library);
    ~PlatformSync();

    void start();
    void stop();

    bool isNetworkAvailable() const { return m_networkAvailable.load(); }
    void refreshNetworkStatusAsync();
    void checkUpdatesAsync();

    bool hasUpdateAvailable() const { return m_updateAvailable.load(); }
    std::string latestVersion() const;
    std::string updateUrl() const;
    std::string updateStatus() const;

    void syncPlaylistAsync(const std::string& playlistId);
    void syncAllAutoPlaylistsAsync();
    void hydrateMissingThumbnailsAsync();
    void cleanupOrphanCacheAsync();

    void queueDownload(const std::string& playlistId, const std::string& trackId, bool asVideo);
    void queueDirectDownload(const Track& track, const std::string& playlistId, bool asVideo);
    void reinstallTrack(const std::string& playlistId, const Track& track, bool asVideo);
    void queuePlaylistDownload(const std::string& playlistId, bool asVideo);
    void retryFailedDownloads();
    void cancelAllDownloads();
    std::vector<DownloadItem> downloadsSnapshot() const;
    void clearCompletedDownloads();

    using StreamResolvedCb = std::function<void(bool ok, const std::string& streamUrl, const std::string& error)>;
    void resolveDirectStreamAsync(const Track& track, StreamResolvedCb cb);

    using SearchCb = std::function<void(bool ok, const std::vector<Track>& results, const std::string& error)>;
    void searchPlatformAsync(Platform platform, const std::string& queryOrUrl, int limit, SearchCb cb);

    std::string statusMessage() const;
    int activeTaskCount() const { return m_activeTasks.load(); }

private:
    enum class TaskType {
        CheckNetwork,
        SyncPlaylist,
        HydrateThumbnails,
        CleanupCache,
        DownloadTrack,
        ResolveStream,
        SearchPlatform,
        CheckUpdates
    };

    struct Task {
        TaskType type;
        std::string playlistId;
        std::string trackId;
        std::string url;
        Platform platform = Platform::YouTubeMusic;
        bool asVideo = false;
        int searchLimit = 12;
        StreamResolvedCb streamCb;
        SearchCb searchCb;
        uint64_t seq = 0;
    };

    struct TaskQueue {
        std::mutex mtx;
        std::condition_variable cv;
        std::queue<Task> q;
        void push(Task t) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                q.push(std::move(t));
            }
            cv.notify_one();
        }
        void clear() {
            std::lock_guard<std::mutex> lock(mtx);
            std::queue<Task> empty;
            q.swap(empty);
        }
    };

    void fastWorkerLoop();
    void syncWorkerLoop();
    void dlWorkerLoop();

    bool checkNetworkWinHttp();
    bool downloadFileWinHttp(const std::string& url, const std::string& destPath);
    std::string runYtDlp(const std::string& args, int& exitCode,
                         const std::function<void(const std::string& line)>& onLine = nullptr,
                         int timeoutMs = 0);
    void setStatus(const std::string& msg);
    void updateDownloadItem(const std::string& trackId, const std::function<void(DownloadItem&)>& fn);

    LibraryManager& m_library;
    TaskQueue m_fastQueue;
    TaskQueue m_syncQueue;
    TaskQueue m_dlQueue;
    std::thread m_fastWorker;
    std::thread m_syncWorker;
    std::thread m_dlWorker;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelDownloads{false};
    std::atomic<bool> m_networkAvailable{true};
    std::atomic<bool> m_updateAvailable{false};
    mutable std::mutex m_updateMutex;
    std::string m_latestVersion;
    std::string m_updateUrl = "https://github.com/poupou-cheater/music-player-poupou/releases/latest";
    std::string m_updateStatus;
    std::atomic<int> m_activeTasks{0};
    std::atomic<uint64_t> m_latestStreamSeq{0};

    mutable std::mutex m_dlMutex;
    std::vector<DownloadItem> m_downloads;

    std::mutex m_procMutex;
    void* m_jobHandle = nullptr;
    std::vector<void*> m_activeProcesses;
    std::mutex m_httpMutex;
    std::vector<void*> m_activeHttpSessions;

    mutable std::mutex m_streamCacheMutex;
    std::unordered_map<std::string, std::string> m_streamCache;

    mutable std::mutex m_statusMutex;
    std::string m_statusMsg = "Ready";
};
