#include "ui/IconStore.hpp"
#include "vulkan/VulkanContext.hpp"

#include <nanosvg.h>
#include <nanosvgrast.h>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

bool IconStore::loadAll(VulkanContext& ctx, const std::string& folder, int sizePx) {
    if (!fs::exists(folder)) return false;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) return false;

    std::vector<uint8_t> rgba(static_cast<size_t>(sizePx) * sizePx * 4);

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".svg") continue;

        std::string key = entry.path().stem().string();
        std::string pathStr = entry.path().string();
        NSVGimage* image = nsvgParseFromFile(pathStr.c_str(), "px", 96.0f);
        if (!image || image->width <= 0.0f || image->height <= 0.0f) {
            if (image) nsvgDelete(image);
            continue;
        }

        float scaleX = static_cast<float>(sizePx) / image->width;
        float scaleY = static_cast<float>(sizePx) / image->height;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;

        std::fill(rgba.begin(), rgba.end(), 0);
        nsvgRasterize(rast, image, 0.0f, 0.0f, scale, rgba.data(), sizePx, sizePx, sizePx * 4);
        nsvgDelete(image);

        VulkanTexture tex;
        if (tex.createFromRGBA(ctx, rgba.data(), static_cast<uint32_t>(sizePx), static_cast<uint32_t>(sizePx))) {
            m_icons[key] = tex;
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
        return ImGui::ImageButton(strId, id, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), bg, tint);
    }
    return ImGui::Button(strId, ImVec2(size + 8, size + 8));
}
