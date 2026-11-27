#include "media/MediaPlayer.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

template <typename T>
static void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

void MediaPlayer::Biquad::setPeaking(float sampleRate, float freq, float q, float gainDb) {
    constexpr float kPi = 3.14159265358979323846f;
    float A = std::pow(10.0f, gainDb / 40.0f);
    float w0 = 2.0f * kPi * freq / sampleRate;
    float alpha = std::sin(w0) / (2.0f * q);
    float cosw0 = std::cos(w0);

    float b0_ = 1.0f + alpha * A;
    float b1_ = -2.0f * cosw0;
    float b2_ = 1.0f - alpha * A;
    float a0_ = 1.0f + alpha / A;
    float a1_ = -2.0f * cosw0;
    float a2_ = 1.0f - alpha / A;

    b0 = b0_ / a0_;
    b1 = b1_ / a0_;
    b2 = b2_ / a0_;
    a1 = a1_ / a0_;
    a2 = a2_ / a0_;
}

float MediaPlayer::Biquad::process(float in, int ch) {
    float out = b0 * in + z1[ch];
    z1[ch] = b1 * in - a1 * out + z2[ch];
    z2[ch] = b2 * in - a2 * out;
    return out;
}

void MediaPlayer::Biquad::reset() {
    z1[0] = z1[1] = 0.0f;
    z2[0] = z2[1] = 0.0f;
}

MediaPlayer::MediaPlayer() = default;

MediaPlayer::~MediaPlayer() {
    shutdown();
}

bool MediaPlayer::init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        if (SUCCEEDED(MFStartup(MF_VERSION))) {
            m_mfInitialized = true;
        }
    }

    m_audioRing.resize(m_sampleRate * m_channels * 4);
    setEqGains({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

    auto* dev = new ma_device();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = m_channels;
    cfg.sampleRate = m_sampleRate;
    cfg.dataCallback = [](ma_device* pDev, void* pOut, const void* pIn, ma_uint32 frameCount) {
        MediaPlayer::audioDataCallback(pDev, pOut, pIn, frameCount);
    };
    cfg.pUserData = this;

    if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS) {
        delete dev;
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Failed to initialize audio playback device";
        return false;
    }
    ma_device_start(dev);
    m_audioDevice = dev;
    return m_mfInitialized;
}

void MediaPlayer::shutdown() {
    stop();
    if (m_audioDevice) {
        auto* dev = static_cast<ma_device*>(m_audioDevice);
        ma_device_uninit(dev);
        delete dev;
        m_audioDevice = nullptr;
    }
    if (m_mfInitialized) {
        MFShutdown();
        m_mfInitialized = false;
    }
}

void MediaPlayer::setEqGains(const std::array<float, 5>& gainsDb) {
    static constexpr float kFreqs[5] = { 60.0f, 230.0f, 910.0f, 3600.0f, 14000.0f };
    static constexpr float kQ[5] = { 0.8f, 1.0f, 1.0f, 1.0f, 0.8f };
    std::lock_guard<std::mutex> lock(m_eqMutex);
    for (int i = 0; i < 5; ++i) {
        float g = std::clamp(gainsDb[i], -12.0f, 12.0f);
        m_eqBands[i].setPeaking(static_cast<float>(m_sampleRate), kFreqs[i], kQ[i], g);
    }
}

void MediaPlayer::audioDataCallback(void* pDevice, void* pOutput, const void* /*pInput*/, uint32_t frameCount) {
    auto* dev = static_cast<ma_device*>(pDevice);
    auto* self = static_cast<MediaPlayer*>(dev->pUserData);
    self->fillAudioOutput(static_cast<float*>(pOutput), frameCount);
}

void MediaPlayer::fillAudioOutput(float* output, uint32_t frameCount) {
    const size_t totalSamples = static_cast<size_t>(frameCount) * m_channels;
    if (!m_playing.load() || m_paused.load() || m_buffering.load()) {
        std::fill_n(output, totalSamples, 0.0f);
        return;
    }

    size_t popped = m_audioRing.pop(output, totalSamples);
    if (popped < totalSamples) {
        std::fill(output + popped, output + totalSamples, 0.0f);
    }

    // Apply 5-band Equalizer
    if (popped > 0) {
        std::lock_guard<std::mutex> lock(m_eqMutex);
        for (size_t i = 0; i < popped; i += m_channels) {
            for (uint32_t ch = 0; ch < m_channels && ch < 2; ++ch) {
                float s = output[i + ch];
                for (int b = 0; b < 5; ++b) {
                    s = m_eqBands[b].process(s, static_cast<int>(ch));
                }
                output[i + ch] = s;
            }
        }
    }

    const float gain = m_muted.load() ? 0.0f : m_volume.load();
    int fadeRem = m_fadeInSamples.load();
    const int fadeTotal = static_cast<int>(m_sampleRate / 100) * static_cast<int>(m_channels); // 10ms smooth ramp
    for (size_t i = 0; i < popped; ++i) {
        float ramp = 1.0f;
        if (fadeRem > 0) {
            ramp = 1.0f - static_cast<float>(fadeRem) / static_cast<float>(fadeTotal);
            --fadeRem;
        }
        output[i] = std::clamp(output[i] * gain * ramp, -1.0f, 1.0f);
    }
    m_fadeInSamples.store(fadeRem);

    if (popped > 0) {
        double dt = static_cast<double>(popped / m_channels) / static_cast<double>(m_sampleRate);
        double cur = m_clockSec.load();
        m_clockSec.store(cur + dt);
    }
}

bool MediaPlayer::open(const std::string& uriOrPath, bool preferAudioOnly) {
    stop();

    m_stopRequested.store(false);
    m_ended.store(false);
    m_paused.store(false);
    m_playing.store(true);
    m_open.store(true);
    m_hasVideo.store(false);
    m_clockSec.store(0.0);
    m_durationSec.store(0.0);
    m_buffering.store(true);
    m_fadeInSamples.store(static_cast<int>(m_sampleRate / 100) * static_cast<int>(m_channels));
    m_audioRing.clear();

    {
        std::lock_guard<std::mutex> lock(m_eqMutex);
        for (auto& b : m_eqBands) b.reset();
    }

    {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError.clear();
    }

    m_decoderThread = std::thread(&MediaPlayer::decoderLoop, this, uriOrPath, preferAudioOnly);
    return true;
}

void MediaPlayer::play() {
    if (m_open.load()) {
        m_paused.store(false);
        m_playing.store(true);
    }
}

void MediaPlayer::pause() {
    if (m_open.load()) {
        m_paused.store(true);
    }
}

void MediaPlayer::togglePlayPause() {
    if (!m_open.load()) return;
    m_paused.store(!m_paused.load());
}

void MediaPlayer::stop() {
    m_stopRequested.store(true);
    m_playing.store(false);
    m_paused.store(false);
    m_open.store(false);
    if (m_decoderThread.joinable()) {
        m_decoderThread.join();
    }
    m_audioRing.clear();
    m_hasVideo.store(false);
    m_videoFrameReady.store(false);
}

void MediaPlayer::seek(double seconds) {
    if (!m_open.load()) return;
    double dur = m_durationSec.load();
    if (dur > 0.0) {
        seconds = std::clamp(seconds, 0.0, dur);
    } else if (seconds < 0.0) {
        seconds = 0.0;
    }
    m_seekTarget.store(seconds);
    m_seekRequested.store(true);
}

void MediaPlayer::setVolume(float volume) {
    m_volume.store(std::clamp(volume, 0.0f, 1.5f));
}

void MediaPlayer::setMuted(bool muted) {
    m_muted.store(muted);
}

bool MediaPlayer::consumeEnded() {
    return m_ended.exchange(false);
}

double MediaPlayer::positionSeconds() const {
    return m_clockSec.load();
}

std::string MediaPlayer::lastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

bool MediaPlayer::consumeVideoFrame(std::vector<uint8_t>& outRgba, uint32_t& outW, uint32_t& outH) {
    if (!m_videoFrameReady.exchange(false)) return false;
    std::lock_guard<std::mutex> lock(m_videoMutex);
    outRgba = m_videoRGBA;
    outW = m_videoW;
    outH = m_videoH;
    return outW > 0 && outH > 0 && !outRgba.empty();
}

void MediaPlayer::decoderLoop(std::string uri, bool preferAudioOnly) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMFAttributes* attrs = nullptr;
    IMFSourceReader* reader = nullptr;
    MFCreateAttributes(&attrs, 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    std::wstring wUri = utf8ToWide(uri);
    HRESULT hr = MFCreateSourceReaderFromURL(wUri.c_str(), attrs, &reader);
    safeRelease(attrs);

    if (FAILED(hr) || !reader) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "Cannot open media stream/file";
        m_playing.store(false);
        m_open.store(false);
        CoUninitialize();
        return;
    }

    PROPVARIANT var{};
    PropVariantInit(&var);
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var))) {
        if (var.vt == VT_UI8) {
            m_durationSec.store(static_cast<double>(var.uhVal.QuadPart) / 10000000.0);
        }
        PropVariantClear(&var);
    }

    bool hasAudio = false;
    IMFMediaType* audioType = nullptr;
    if (SUCCEEDED(MFCreateMediaType(&audioType))) {
        audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        audioType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_sampleRate);
        audioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_channels);
        audioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        if (SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audioType))) {
            hasAudio = true;
        }
        safeRelease(audioType);
    }

    bool hasVid = false;
    uint32_t vidW = 0, vidH = 0;
    if (!preferAudioOnly) {
        IMFMediaType* videoType = nullptr;
        if (SUCCEEDED(MFCreateMediaType(&videoType))) {
            videoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            videoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, videoType))) {
                IMFMediaType* actualType = nullptr;
                if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType))) {
                    MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &vidW, &vidH);
                    if (vidW > 0 && vidH > 0) {
                        hasVid = true;
                        m_hasVideo.store(true);
                    }
                    safeRelease(actualType);
                }
            }
            safeRelease(videoType);
        }
    }

    if (!hasAudio && !hasVid) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = "No supported audio or video stream found";
        safeRelease(reader);
        m_playing.store(false);
        m_open.store(false);
        CoUninitialize();
        return;
    }

    bool audioEOF = !hasAudio;
    bool videoEOF = !hasVid;
    double pendingVideoPts = -1.0;
    std::vector<uint8_t> pendingVideoRGBA;
    auto lastWallTick = std::chrono::steady_clock::now();

    while (!m_stopRequested.load()) {
        if (m_seekRequested.exchange(false)) {
            double target = m_seekTarget.load();
            PROPVARIANT seekVar{};
            PropVariantInit(&seekVar);
            seekVar.vt = VT_I8;
            seekVar.hVal.QuadPart = static_cast<LONGLONG>(target * 10000000.0);
            reader->SetCurrentPosition(GUID_NULL, seekVar);
            PropVariantClear(&seekVar);
            m_buffering.store(true);
            m_fadeInSamples.store(static_cast<int>(m_sampleRate / 100) * static_cast<int>(m_channels));
            m_audioRing.clear();
            {
                std::lock_guard<std::mutex> lock(m_eqMutex);
                for (auto& b : m_eqBands) b.reset();
            }
            m_clockSec.store(target);
            pendingVideoPts = -1.0;
            audioEOF = !hasAudio;
            videoEOF = !hasVid;
        }

        if (m_paused.load()) {
            lastWallTick = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }

        if (!hasAudio) {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - lastWallTick).count();
            lastWallTick = now;
            m_clockSec.store(m_clockSec.load() + dt);
        }

        if (!audioEOF && m_audioRing.availableWrite() > m_sampleRate * m_channels / 4) {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            HRESULT ahr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &ts, &sample);
            if (FAILED(ahr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                audioEOF = true;
            } else if (sample) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                        size_t floatCount = len / sizeof(float);
                        m_audioRing.push(reinterpret_cast<const float*>(data), floatCount);
                        buf->Unlock();
                    }
                    safeRelease(buf);
                }
                safeRelease(sample);
            }
        }

        if (!videoEOF && pendingVideoPts < 0.0) {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            HRESULT vhr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample);
            if (FAILED(vhr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                videoEOF = true;
            } else if (sample) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                        size_t expected = static_cast<size_t>(vidW) * vidH * 4;
                        if (len >= expected) {
                            pendingVideoRGBA.resize(expected);
                            for (size_t i = 0; i < expected; i += 4) {
                                pendingVideoRGBA[i + 0] = data[i + 2];
                                pendingVideoRGBA[i + 1] = data[i + 1];
                                pendingVideoRGBA[i + 2] = data[i + 0];
                                pendingVideoRGBA[i + 3] = 255;
                            }
                            pendingVideoPts = static_cast<double>(ts) / 10000000.0;
                        }
                        buf->Unlock();
                    }
                    safeRelease(buf);
                }
                safeRelease(sample);
            }
        }

        if (pendingVideoPts >= 0.0) {
            double clock = m_clockSec.load();
            if (pendingVideoPts <= clock + 0.02) {
                {
                    std::lock_guard<std::mutex> lock(m_videoMutex);
                    m_videoRGBA.swap(pendingVideoRGBA);
                    m_videoW = vidW;
                    m_videoH = vidH;
                    m_videoFrameReady.store(true);
                }
                pendingVideoPts = -1.0;
            }
        }

        if (m_buffering.load()) {
            if (audioEOF || m_audioRing.size() >= (m_sampleRate * m_channels / 12)) {
                m_buffering.store(false);
            }
        }

        if (audioEOF && videoEOF && pendingVideoPts < 0.0) {
            m_ended.store(true);
            m_playing.store(false);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    safeRelease(reader);
    CoUninitialize();
}
