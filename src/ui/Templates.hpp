#pragma once

#include "library/Models.hpp"
#include <imgui.h>

class VulkanContext;

struct TemplatePalette {
    const char* name;
    ImVec4 bgMain;
    ImVec4 bgSidebar;
    ImVec4 bgCard;
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 accentSecondary;
    ImVec4 textPrimary;
    ImVec4 textMuted;
    float rounding;
    bool verticalStage; // TikTok-style vertical video stage
};

TemplatePalette getTemplatePalette(GuiTemplate tpl);
TemplatePalette paletteFromCustom(const CustomTheme& ct);
void applyTemplateStyle(GuiTemplate tpl, VulkanContext& vk);
void applyPaletteStyle(const TemplatePalette& p, VulkanContext& vk);
