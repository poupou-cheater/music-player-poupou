#include "ui/IconStore.hpp"
#include "vulkan/VulkanContext.hpp"
#include "EmbeddedIcons.hpp"

#include <nanosvg.h>
#include <nanosvgrast.h>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static void rasterizeAndStore(VulkanContext& ctx, NSVGrasterizer* rast,
                              std::vector<uint8_t>& rgba, int sizePx,
                              std::unordered_map<std::string, VulkanTexture>& icons,
                              const std::string& key, NSVGimage* image) {
    if (!image || image->width <= 0.0f || image->height <= 0.0f) {
        if (image) nsvgDelete(image);
        return;
    }

    float scaleX = static_cast<float>(sizePx) / image->width;
    float scaleY = static_cast<float>(sizePx) / image->height;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;

    std::fill(rgba.begin(), rgba.end(), 0);
    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, rgba.data(), sizePx, sizePx, sizePx * 4);
    nsvgDelete(image);

    VulkanTexture tex;
    if (tex.createFromRGBA(ctx, rgba.data(), static_cast<uint32_t>(sizePx), static_cast<uint32_t>(sizePx))) {
        auto it = icons.find(key);
        if (it != icons.end()) it->second.destroy(ctx);
        icons[key] = tex;
    }
}

bool IconStore::loadAll(VulkanContext& ctx, const std::string& folder, int sizePx) {
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) return false;

    std::vector<uint8_t> rgba(static_cast<size_t>(sizePx) * sizePx * 4);

    // 1. Load all SVG icons embedded directly inside StreamPulse.exe
    for (const auto& [name, svgText] : kEmbeddedIcons) {
        std::string buf(svgText);
        NSVGimage* image = nsvgParse(buf.data(), "px", 96.0f);
        rasterizeAndStore(ctx, rast, rgba, sizePx, m_icons, std::string(name), image);
    }

    // 2. Allow optional disk overrides / extras if assets/icons exists
    if (fs::exists(folder)) {
        for (const auto& entry : fs::directory_iterator(folder)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".svg") continue;
            std::string key = entry.path().stem().string();
            if (m_icons.find(key) != m_icons.end()) continue;
            std::string pathStr = entry.path().string();
            NSVGimage* image = nsvgParseFromFile(pathStr.c_str(), "px", 96.0f);
            rasterizeAndStore(ctx, rast, rgba, sizePx, m_icons, key, image);
        }
    }

    nsvgDeleteRasterizer(rast);
    return !m_icons.empty();
}

void IconStore::destroyAll(VulkanContext& ctx) {
    for (auto& kv : m_icons) {
        kv.second.destroy(ctx);
    }
    m_icons.clear();
}

ImTextureID IconStore::get(const std::string& name) const {
    auto it = m_icons.find(name);
    if (it != m_icons.end() && it->second.isValid()) {
        return it->second.imguiId();
    }
    return 0;
}

void IconStore::draw(const std::string& name, float size, ImVec4 tint) const {
    ImTextureID id = get(name);
    if (id) {
        ImGui::Image(id, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), tint);
    } else {
        ImGui::Dummy(ImVec2(size, size));
    }
}

bool IconStore::button(const char* strId, const std::string& iconName, float size, ImVec4 tint, ImVec4 bg) const {
    ImTextureID id = get(iconName);
    if (id) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        bool clicked = ImGui::ImageButton(strId, id, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), bg, tint);
        ImGui::PopStyleVar();
        return clicked;
    }
    return ImGui::Button(strId, ImVec2(size + 8, size + 8));
}
