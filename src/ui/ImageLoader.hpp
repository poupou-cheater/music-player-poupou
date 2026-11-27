#pragma once

#include <windows.h>
#include <wincodec.h>
#include <stb_image.h>
#include <cstdint>
#include <string>
#include <vector>

inline bool loadImageRGBA(const std::string& path, std::vector<uint8_t>& outRgba, uint32_t& outW, uint32_t& outH) {
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
