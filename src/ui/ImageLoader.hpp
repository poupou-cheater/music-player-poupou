#pragma once

#include <windows.h>
#include <wincodec.h>
#include <stb_image.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

inline void downscaleRgbaIfNeeded(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h, uint32_t maxSide) {
    if (maxSide == 0 || (w <= maxSide && h <= maxSide) || w == 0 || h == 0) return;
    float scale = (w >= h) ? (static_cast<float>(maxSide) / static_cast<float>(w))
                           : (static_cast<float>(maxSide) / static_cast<float>(h));
    uint32_t nw = std::max(1u, static_cast<uint32_t>(w * scale));
    uint32_t nh = std::max(1u, static_cast<uint32_t>(h * scale));
    std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y) {
        uint32_t sy = std::min(h - 1, (y * h) / nh);
        for (uint32_t x = 0; x < nw; ++x) {
            uint32_t sx = std::min(w - 1, (x * w) / nw);
            const uint8_t* srcPx = &rgba[(static_cast<size_t>(sy) * w + sx) * 4];
            uint8_t* dstPx = &dst[(static_cast<size_t>(y) * nw + x) * 4];
            dstPx[0] = srcPx[0];
            dstPx[1] = srcPx[1];
            dstPx[2] = srcPx[2];
            dstPx[3] = srcPx[3];
        }
    }
    rgba.swap(dst);
    w = nw;
    h = nh;
}

inline bool loadImageRGBA(const std::string& path, std::vector<uint8_t>& outRgba, uint32_t& outW, uint32_t& outH, uint32_t maxSide = 0) {
    outRgba.clear();
    outW = outH = 0;

    // 1. Try stb_image first (fast for JPEG/PNG/BMP/GIF)
    int w = 0, h = 0, comp = 0;
    unsigned char* stbiData = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (stbiData && w > 0 && h > 0) {
        outW = static_cast<uint32_t>(w);
        outH = static_cast<uint32_t>(h);
        outRgba.assign(stbiData, stbiData + static_cast<size_t>(w) * h * 4);
        stbi_image_free(stbiData);
        downscaleRgbaIfNeeded(outRgba, outW, outH, maxSide);
        return true;
    }
    if (stbiData) stbi_image_free(stbiData);

    // 2. Fallback to Windows Imaging Component (WIC) for WebP, etc.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wPath(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wPath.data(), wlen);

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    bool ok = false;
    IWICBitmapDecoder* decoder = nullptr;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(wPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) && decoder) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(factory->CreateFormatConverter(&conv)) && conv) {
                if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    UINT uw = 0, uh = 0;
                    conv->GetSize(&uw, &uh);
                    if (uw > 0 && uh > 0) {
                        outW = uw;
                        outH = uh;
                        outRgba.resize(static_cast<size_t>(uw) * uh * 4);
                        if (SUCCEEDED(conv->CopyPixels(nullptr, uw * 4, static_cast<UINT>(outRgba.size()), outRgba.data()))) {
                            ok = true;
                        }
                    }
                }
                conv->Release();
            }
            frame->Release();
        }
        decoder->Release();
    }
    factory->Release();
    return ok;
}
