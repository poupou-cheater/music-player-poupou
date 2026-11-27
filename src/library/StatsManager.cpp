#include "library/StatsManager.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string nowTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmNow{};
    localtime_s(&tmNow, &now);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmNow);
    return std::string(buf);
}

void StatsManager::load(const std::string& jsonPath) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_jsonPath = jsonPath;
    if (!fs::exists(m_jsonPath)) return;

    try {
        std::ifstream f(m_jsonPath);
        json j;
        f >> j;
        m_totalSeconds = j.value("totalSeconds", 0.0);
        m_totalPlays = j.value("totalPlays", 0);
        if (j.contains("platformSeconds") && j["platformSeconds"].is_array() && j["platformSeconds"].size() == 4) {
            for (size_t i = 0; i < 4; ++i) m_platformSeconds[i] = j["platformSeconds"][i].get<double>();
        }
        if (j.contains("tracks")) {
            for (const auto& it : j["tracks"]) {
                TrackStat ts;
                ts.id = it.value("id", "");
                ts.title = it.value("title", "");
                ts.artist = it.value("artist", "");
                ts.platform = static_cast<Platform>(it.value("platform", 0));
                ts.playCount = it.value("playCount", 0);
                ts.secondsListened = it.value("secondsListened", 0.0);
                ts.lastPlayed = it.value("lastPlayed", "");
                m_tracks.push_back(std::move(ts));
            }
        }
        if (j.contains("history")) {
            for (const auto& it : j["history"]) {
                HistoryEntry he;
                he.title = it.value("title", "");
                he.artist = it.value("artist", "");
                he.platform = static_cast<Platform>(it.value("platform", 0));
                he.timestamp = it.value("timestamp", "");
                m_history.push_back(std::move(he));
            }
        }
    } catch (...) {}
}

void StatsManager::save() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    fs::create_directories(fs::path(m_jsonPath).parent_path());
    json jTracks = json::array();
    for (const auto& t : m_tracks) {
        jTracks.push_back({
            {"id", t.id},
            {"title", t.title},
            {"artist", t.artist},
            {"platform", static_cast<int>(t.platform)},
            {"playCount", t.playCount},
            {"secondsListened", t.secondsListened},
            {"lastPlayed", t.lastPlayed}
        });
    }
    json jHist = json::array();
    for (const auto& h : m_history) {
        jHist.push_back({
            {"title", h.title},
            {"artist", h.artist},
            {"platform", static_cast<int>(h.platform)},
            {"timestamp", h.timestamp}
        });
    }
    json j = {
        {"totalSeconds", m_totalSeconds},
        {"totalPlays", m_totalPlays},
        {"platformSeconds", m_platformSeconds},
        {"tracks", jTracks},
        {"history", jHist}
    };
    std::ofstream f(m_jsonPath);
    f << j.dump(2);
}

void StatsManager::recordPlayStart(const Track& track) {
    std::lock_guard<std::mutex> lock(m_mtx);
    ++m_totalPlays;
    std::string tsStr = nowTimestamp();

    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](const TrackStat& s) {
        return s.id == track.id;
    });
    if (it == m_tracks.end()) {
        m_tracks.push_back({ track.id, track.title, track.artist, track.platform, 1, 0.0, tsStr });
    } else {
        ++it->playCount;
        it->lastPlayed = tsStr;
        it->title = track.title;
        it->artist = track.artist;
    }

    m_history.insert(m_history.begin(), { track.title, track.artist, track.platform, tsStr });
    if (m_history.size() > 50) m_history.resize(50);
}

void StatsManager::accumulateTime(const Track& track, double dtSeconds) {
    if (dtSeconds <= 0.0) return;
    std::lock_guard<std::mutex> lock(m_mtx);
    m_totalSeconds += dtSeconds;
    size_t pIdx = static_cast<size_t>(track.platform);
    if (pIdx < 4) m_platformSeconds[pIdx] += dtSeconds;

    for (auto& s : m_tracks) {
        if (s.id == track.id) {
            s.secondsListened += dtSeconds;
            break;
        }
    }
}

double StatsManager::totalSeconds() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_totalSeconds;
}

int StatsManager::totalPlays() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_totalPlays;
}

std::array<double, 4> StatsManager::platformSeconds() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_platformSeconds;
}

std::vector<TrackStat> StatsManager::topTracks(size_t limit) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto copy = m_tracks;
    std::sort(copy.begin(), copy.end(), [](const TrackStat& a, const TrackStat& b) {
        if (a.secondsListened != b.secondsListened) return a.secondsListened > b.secondsListened;
        return a.playCount > b.playCount;
    });
    if (copy.size() > limit) copy.resize(limit);
    return copy;
}

std::vector<HistoryEntry> StatsManager::recentHistory(size_t limit) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_history.size() <= limit) return m_history;
    return std::vector<HistoryEntry>(m_history.begin(), m_history.begin() + limit);
}
