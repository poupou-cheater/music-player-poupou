#pragma once

#include <mutex>
#include <string>
#include <vector>

class AppLog {
public:
    static AppLog& instance() {
        static AppLog inst;
        return inst;
    }

    void add(const std::string& msg) {
        if (msg.empty()) return;
        std::lock_guard<std::mutex> lock(m_mtx);
        m_lines.push_back(msg);
        if (m_lines.size() > 400) {
            m_lines.erase(m_lines.begin(), m_lines.begin() + 100);
        }
    }

    std::vector<std::string> snapshot() const {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_lines;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_lines.clear();
    }

private:
    mutable std::mutex m_mtx;
    std::vector<std::string> m_lines;
};
