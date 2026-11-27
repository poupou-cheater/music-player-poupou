#include "library/DiscordRPC.hpp"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>

using json = nlohmann::json;

DiscordRPC::~DiscordRPC() {
    shutdown();
}

void DiscordRPC::setEnabled(bool enabled, const std::string& clientId) {
    if (!clientId.empty()) m_clientId = clientId;
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!m_enabled) {
        clearPresence();
        shutdown();
    }
}

bool DiscordRPC::ensureConnected() {
    if (!m_enabled) return false;
    if (m_pipe) return true;

    for (int i = 0; i < 10; ++i) {
        std::wstring pipeName = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            m_pipe = h;
            json handshake = { {"v", 1}, {"client_id", m_clientId} };
            if (!sendFrame(0, handshake.dump())) {
                shutdown();
                return false;
            }
            return true;
        }
    }
    return false;
}

bool DiscordRPC::sendFrame(uint32_t opcode, const std::string& jsonPayload) {
    if (!m_pipe) return false;
    uint32_t header[2] = { opcode, static_cast<uint32_t>(jsonPayload.size()) };
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_pipe), header, sizeof(header), &written, nullptr) || written != sizeof(header)) {
        shutdown();
        return false;
    }
    if (!jsonPayload.empty()) {
        if (!WriteFile(static_cast<HANDLE>(m_pipe), jsonPayload.data(), static_cast<DWORD>(jsonPayload.size()), &written, nullptr)) {
            shutdown();
            return false;
        }
    }
    return true;
}

void DiscordRPC::updatePresence(const Track& track, bool isPlaying, double positionSec, double durationSec) {
    if (!m_enabled || !ensureConnected()) return;

    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    json activity = {
        {"details", track.title.empty() ? "StreamPulse" : track.title},
        {"state", (track.artist.empty() ? "Unknown" : track.artist) + std::string(" • ") + platformName(track.platform)}
    };

    if (isPlaying && durationSec > 0.0) {
        long long startTs = nowSec - static_cast<long long>(positionSec);
        long long endTs = startTs + static_cast<long long>(durationSec);
        activity["timestamps"] = { {"start", startTs}, {"end", endTs} };
    }

    json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {
            {"pid", static_cast<int>(GetCurrentProcessId())},
            {"activity", activity}
        }},
        {"nonce", std::to_string(++m_nonce)}
    };
    sendFrame(1, payload.dump());
}

void DiscordRPC::clearPresence() {
    if (!m_pipe) return;
    json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", { {"pid", static_cast<int>(GetCurrentProcessId())} }},
        {"nonce", std::to_string(++m_nonce)}
    };
    sendFrame(1, payload.dump());
}

void DiscordRPC::shutdown() {
    if (m_pipe) {
        CloseHandle(static_cast<HANDLE>(m_pipe));
        m_pipe = nullptr;
    }
}
