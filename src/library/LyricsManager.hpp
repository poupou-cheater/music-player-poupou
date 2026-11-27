#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct LyricLine {
    double timeSec = -1.0; // -1 for unsynced
    std::string text;
};

class LyricsManager {
public:
    void fetchForTrackAsync(const std::string& trackId, const std::string& title,
                            const std::string& artist, double durationSec, bool offlineMode);

    bool isLoading() const { return m_loading.load(); }
    bool hasLyrics() const;
    bool isSynced() const;
    std::vector<LyricLine> linesCopy() const;
    int activeIndex(double positionSec) const;
    std::string statusText() const;

private:
    void parseLrc(const std::string& lrc, bool synced);
    static std::string urlEncode(const std::string& s);
    static std::string httpGet(const std::string& host, const std::string& path);

    mutable std::mutex m_mtx;
    std::string m_currentTrackId;
    std::vector<LyricLine> m_lines;
    bool m_synced = false;
    std::string m_status = "No lyrics loaded";
    std::atomic<bool> m_loading{false};
    std::atomic<uint64_t> m_requestSeq{0};
};
