#pragma once

#include "library/Models.hpp"
#include <array>
#include <mutex>
#include <string>
#include <vector>

struct TrackStat {
    std::string id;
    std::string title;
    std::string artist;
    Platform platform = Platform::YouTubeMusic;
    int playCount = 0;
    double secondsListened = 0.0;
    std::string lastPlayed;
};

struct HistoryEntry {
    std::string title;
    std::string artist;
    Platform platform = Platform::YouTubeMusic;
    std::string timestamp;
};

class StatsManager {
public:
    void load(const std::string& jsonPath = "data/stats.json");
    void save() const;

    void recordPlayStart(const Track& track);
    void accumulateTime(const Track& track, double dtSeconds);

    double totalSeconds() const;
    int totalPlays() const;
    std::array<double, 4> platformSeconds() const;
    std::vector<TrackStat> topTracks(size_t limit = 15) const;
    std::vector<HistoryEntry> recentHistory(size_t limit = 30) const;

private:
    mutable std::mutex m_mtx;
    std::string m_jsonPath = "data/stats.json";
    double m_totalSeconds = 0.0;
    int m_totalPlays = 0;
    std::array<double, 4> m_platformSeconds = { 0, 0, 0, 0 };
    std::vector<TrackStat> m_tracks;
    std::vector<HistoryEntry> m_history;
};
