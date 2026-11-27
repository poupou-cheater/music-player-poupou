#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();

    bool init();
    void shutdown();

    bool open(const std::string& uriOrPath, bool preferAudioOnly = false);
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(double seconds);

    void setVolume(float volume); // 0.0 .. 1.5 (0% .. 150%)
    float volume() const { return m_volume.load(); }
    void setExtraGainDb(float db); // per-track / per-playlist dB gain (-12 .. +12 dB)
    void setMuted(bool muted);
    bool isMuted() const { return m_muted.load(); }

    // 5-band Equalizer (60Hz, 230Hz, 910Hz, 3.6kHz, 14kHz) in dB (-12 .. +12)
    void setEqGains(const std::array<float, 5>& gainsDb);
    void setAudioMixer(float preampDb, float balance, float stereoWidth, bool monoMix, bool softLimiter);

    bool isPlaying() const { return m_playing.load() && !m_paused.load(); }
    bool isPaused() const { return m_paused.load(); }
    bool isOpen() const { return m_open.load(); }
    bool hasVideo() const { return m_hasVideo.load(); }
    bool consumeEnded();

    double positionSeconds() const;
    double durationSeconds() const { return m_durationSec.load(); }
    std::string lastError() const;

    bool consumeVideoFrame(std::vector<uint8_t>& outRgba, uint32_t& outW, uint32_t& outH);

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] = {0, 0};
        float z2[2] = {0, 0};

        void setPeaking(float sampleRate, float freq, float q, float gainDb);
        float process(float in, int ch);
        void reset();
    };

    void decoderLoop(std::string uri, bool preferAudioOnly);
    static void audioDataCallback(void* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
    void fillAudioOutput(float* output, uint32_t frameCount);

    struct RingBuffer {
        std::vector<float> data;
        size_t readPos = 0;
        size_t writePos = 0;
        size_t count = 0;
        std::mutex mtx;

        void resize(size_t capacity) {
            std::lock_guard<std::mutex> lock(mtx);
            data.assign(capacity, 0.0f);
            readPos = writePos = count = 0;
        }
        void clear() {
            std::lock_guard<std::mutex> lock(mtx);
            readPos = writePos = count = 0;
        }
        size_t availableWrite() {
            std::lock_guard<std::mutex> lock(mtx);
            return data.size() - count;
        }
        void push(const float* samples, size_t n) {
            std::lock_guard<std::mutex> lock(mtx);
            for (size_t i = 0; i < n && count < data.size(); ++i) {
                data[writePos] = samples[i];
                writePos = (writePos + 1) % data.size();
                ++count;
            }
        }
        size_t pop(float* out, size_t n) {
            std::lock_guard<std::mutex> lock(mtx);
            size_t toRead = (n < count) ? n : count;
            for (size_t i = 0; i < toRead; ++i) {
                out[i] = data[readPos];
                readPos = (readPos + 1) % data.size();
            }
            count -= toRead;
            return toRead;
        }
        size_t size() {
            std::lock_guard<std::mutex> lock(mtx);
            return count;
        }
    };

    bool m_mfInitialized = false;
    void* m_audioDevice = nullptr;
    uint32_t m_sampleRate = 48000;
    uint32_t m_channels = 2;
    RingBuffer m_audioRing;

    std::mutex m_eqMutex;
    std::array<Biquad, 5> m_eqBands;

    std::thread m_decoderThread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_open{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_hasVideo{false};
    std::atomic<bool> m_ended{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekTarget{0.0};
    std::atomic<bool> m_buffering{false};
    std::atomic<int> m_fadeInSamples{0};

    std::atomic<float> m_volume{0.85f};
    std::atomic<float> m_extraGainLinear{1.0f};
    std::atomic<float> m_preampLinear{1.0f};
    std::atomic<float> m_balance{0.0f};
    std::atomic<float> m_stereoWidth{1.0f};
    std::atomic<bool> m_monoMix{false};
    std::atomic<bool> m_softLimiter{true};
    std::atomic<bool> m_muted{false};
    std::atomic<double> m_durationSec{0.0};
    std::atomic<double> m_clockSec{0.0};

    mutable std::mutex m_errorMutex;
    std::string m_lastError;

    std::mutex m_videoMutex;
    std::vector<uint8_t> m_videoRGBA;
    uint32_t m_videoW = 0;
    uint32_t m_videoH = 0;
    std::atomic<bool> m_videoFrameReady{false};
};
