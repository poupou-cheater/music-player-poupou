#include "ui/Templates.hpp"
#include "vulkan/VulkanContext.hpp"

TemplatePalette getTemplatePalette(GuiTemplate tpl) {
    switch (tpl) {
        case GuiTemplate::YouTubeMusic:
            return {
                "YouTube Music",
                ImVec4(0.015f, 0.015f, 0.02f, 1.0f),  // #030305
                ImVec4(0.05f, 0.05f, 0.06f, 1.0f),
                ImVec4(0.11f, 0.11f, 0.13f, 1.0f),
                ImVec4(1.00f, 0.08f, 0.12f, 1.0f),    // YouTube Red
                ImVec4(1.00f, 0.24f, 0.26f, 1.0f),
                ImVec4(0.24f, 0.65f, 1.00f, 1.0f),
                ImVec4(0.96f, 0.96f, 0.97f, 1.0f),
                ImVec4(0.62f, 0.62f, 0.66f, 1.0f),
                8.0f,
                false
            };
        case GuiTemplate::Spotify:
            return {
                "Spotify",
                ImVec4(0.07f, 0.07f, 0.07f, 1.0f),    // #121212
                ImVec4(0.00f, 0.00f, 0.00f, 1.0f),    // #000000
                ImVec4(0.13f, 0.13f, 0.14f, 1.0f),
                ImVec4(0.11f, 0.72f, 0.33f, 1.0f),    // #1DB954
                ImVec4(0.15f, 0.84f, 0.40f, 1.0f),
                ImVec4(0.11f, 0.72f, 0.33f, 1.0f),
                ImVec4(1.00f, 1.00f, 1.00f, 1.0f),
                ImVec4(0.68f, 0.68f, 0.68f, 1.0f),
                10.0f,
                false
            };
        case GuiTemplate::TikTok:
            return {
                "TikTok",
                ImVec4(0.00f, 0.00f, 0.00f, 1.0f),
                ImVec4(0.05f, 0.05f, 0.07f, 1.0f),
                ImVec4(0.10f, 0.10f, 0.13f, 1.0f),
                ImVec4(0.99f, 0.17f, 0.33f, 1.0f),    // #FE2C55
                ImVec4(1.00f, 0.31f, 0.44f, 1.0f),
                ImVec4(0.15f, 0.96f, 0.93f, 1.0f),    // #25F4EE
                ImVec4(0.98f, 0.98f, 0.99f, 1.0f),
                ImVec4(0.60f, 0.62f, 0.68f, 1.0f),
                12.0f,
                true
            };
        case GuiTemplate::Minimal:
        default:
            return {
                "Minimal",
                ImVec4(0.08f, 0.10f, 0.13f, 1.0f),
                ImVec4(0.06f, 0.07f, 0.10f, 1.0f),
                ImVec4(0.12f, 0.15f, 0.19f, 1.0f),
                ImVec4(0.36f, 0.55f, 0.94f, 1.0f),
                ImVec4(0.46f, 0.64f, 0.98f, 1.0f),
                ImVec4(0.36f, 0.55f, 0.94f, 1.0f),
                ImVec4(0.93f, 0.95f, 0.98f, 1.0f),
                ImVec4(0.58f, 0.63f, 0.71f, 1.0f),
                6.0f,
                false
            };
    }
}

void applyTemplateStyle(GuiTemplate tpl, VulkanContext& vk) {
    TemplatePalette p = getTemplatePalette(tpl);
    vk.setClearColor(p.bgMain.x, p.bgMain.y, p.bgMain.z, 1.0f);

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = p.rounding;
    s.ChildRounding = p.rounding;
    s.FrameRounding = p.rounding;
    s.PopupRounding = p.rounding;
    s.GrabRounding = p.rounding;
    s.TabRounding = p.rounding;
    s.FramePadding = ImVec2(10.0f, 6.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = p.bgMain;
    c[ImGuiCol_ChildBg] = p.bgSidebar;
    c[ImGuiCol_PopupBg] = p.bgCard;
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_FrameBg] = p.bgCard;
    c[ImGuiCol_FrameBgHovered] = ImVec4(p.bgCard.x + 0.05f, p.bgCard.y + 0.05f, p.bgCard.z + 0.05f, 1.0f);
    c[ImGuiCol_FrameBgActive] = p.accent;
    c[ImGuiCol_Button] = p.bgCard;
    c[ImGuiCol_ButtonHovered] = p.accentHover;
    c[ImGuiCol_ButtonActive] = p.accent;
    c[ImGuiCol_Header] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.28f);
    c[ImGuiCol_HeaderHovered] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.45f);
    c[ImGuiCol_HeaderActive] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentHover;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_Text] = p.textPrimary;
    c[ImGuiCol_TextDisabled] = p.textMuted;
}
