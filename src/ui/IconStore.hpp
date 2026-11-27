#pragma once

#include "vulkan/VulkanTexture.hpp"
#include <imgui.h>
#include <string>
#include <unordered_map>

class VulkanContext;

class IconStore {
public:
    bool loadAll(VulkanContext& ctx, const std::string& folder = "assets/icons", int sizePx = 64);
    void destroyAll(VulkanContext& ctx);

    ImTextureID get(const std::string& name) const;
    void draw(const std::string& name, float size, ImVec4 tint = ImVec4(1, 1, 1, 1)) const;
    bool button(const char* strId, const std::string& iconName, float size,
                ImVec4 tint = ImVec4(1, 1, 1, 1), ImVec4 bg = ImVec4(0, 0, 0, 0)) const;

private:
    std::unordered_map<std::string, VulkanTexture> m_icons;
};
