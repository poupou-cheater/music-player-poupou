#pragma once

#include "library/Models.hpp"
#include <string>

class DiscordRPC {
public:
    ~DiscordRPC();

    void setEnabled(bool enabled, const std::string& clientId);
    void updatePresence(const Track& track, bool isPlaying, double positionSec, double durationSec);
    void clearPresence();
    void shutdown();

    bool isConnected() const { return m_pipe != nullptr; }

private:
    bool ensureConnected();
    bool sendFrame(uint32_t opcode, const std::string& jsonPayload);

    bool m_enabled = false;
    std::string m_clientId = "1206348731234566144";
    void* m_pipe = nullptr; // HANDLE
    int m_nonce = 1;
};
