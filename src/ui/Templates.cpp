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
        case GuiTemplate::ColorHuntNeonNight:
            return {
                "ColorHunt: Neon Night",
                ImVec4(0.10f, 0.10f, 0.18f, 1.0f),    // #1A1A2E
                ImVec4(0.08f, 0.13f, 0.24f, 1.0f),    // #16213E
                ImVec4(0.06f, 0.20f, 0.38f, 1.0f),    // #0F3460
                ImVec4(0.91f, 0.27f, 0.38f, 1.0f),    // #E94560
                ImVec4(0.96f, 0.39f, 0.48f, 1.0f),
                ImVec4(0.35f, 0.72f, 1.00f, 1.0f),
                ImVec4(0.96f, 0.96f, 0.98f, 1.0f),
                ImVec4(0.64f, 0.68f, 0.78f, 1.0f),
                10.0f,
                false
            };
        case GuiTemplate::ColorHuntCyberMint:
            return {
                "ColorHunt: Cyber Mint",
                ImVec4(0.13f, 0.16f, 0.19f, 1.0f),    // #222831
                ImVec4(0.10f, 0.12f, 0.15f, 1.0f),
                ImVec4(0.22f, 0.24f, 0.27f, 1.0f),    // #393E46
                ImVec4(0.00f, 0.68f, 0.71f, 1.0f),    // #00ADB5
                ImVec4(0.12f, 0.78f, 0.81f, 1.0f),
                ImVec4(0.93f, 0.93f, 0.93f, 1.0f),    // #EEEEEE
                ImVec4(0.93f, 0.93f, 0.93f, 1.0f),
                ImVec4(0.62f, 0.66f, 0.70f, 1.0f),
                8.0f,
                false
            };
        case GuiTemplate::ColorHuntSunsetSynth:
            return {
                "ColorHunt: Sunset Synth",
                ImVec4(0.17f, 0.18f, 0.29f, 1.0f),    // #2B2E4A
                ImVec4(0.13f, 0.13f, 0.22f, 1.0f),
                ImVec4(0.33f, 0.21f, 0.29f, 1.0f),    // #53354A
                ImVec4(0.91f, 0.27f, 0.27f, 1.0f),    // #E84545
                ImVec4(0.96f, 0.39f, 0.39f, 1.0f),
                ImVec4(0.56f, 0.22f, 0.29f, 1.0f),    // #903749
                ImVec4(0.97f, 0.95f, 0.96f, 1.0f),
                ImVec4(0.72f, 0.65f, 0.70f, 1.0f),
                10.0f,
                false
            };
        case GuiTemplate::ColorHuntEmeraldDark:
            return {
                "ColorHunt: Emerald Dark",
                ImVec4(0.02f, 0.05f, 0.07f, 1.0f),    // #040D12
                ImVec4(0.05f, 0.11f, 0.12f, 1.0f),
                ImVec4(0.09f, 0.24f, 0.24f, 1.0f),    // #183D3D
                ImVec4(0.36f, 0.51f, 0.45f, 1.0f),    // #5C8374
                ImVec4(0.46f, 0.63f, 0.56f, 1.0f),
                ImVec4(0.58f, 0.69f, 0.65f, 1.0f),    // #93B1A6
                ImVec4(0.92f, 0.96f, 0.95f, 1.0f),
                ImVec4(0.58f, 0.69f, 0.65f, 1.0f),
                10.0f,
                false
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

TemplatePalette paletteFromCustom(const CustomTheme& ct) {
    return {
        ct.name.c_str(),
        ImVec4(ct.bgMain[0], ct.bgMain[1], ct.bgMain[2], ct.bgMain[3]),
        ImVec4(ct.bgSidebar[0], ct.bgSidebar[1], ct.bgSidebar[2], ct.bgSidebar[3]),
        ImVec4(ct.bgCard[0], ct.bgCard[1], ct.bgCard[2], ct.bgCard[3]),
        ImVec4(ct.accent[0], ct.accent[1], ct.accent[2], ct.accent[3]),
        ImVec4(std::min(1.0f, ct.accent[0] + 0.12f), std::min(1.0f, ct.accent[1] + 0.12f), std::min(1.0f, ct.accent[2] + 0.12f), 1.0f),
        ImVec4(ct.accentSecondary[0], ct.accentSecondary[1], ct.accentSecondary[2], ct.accentSecondary[3]),
        ImVec4(ct.textPrimary[0], ct.textPrimary[1], ct.textPrimary[2], ct.textPrimary[3]),
        ImVec4(ct.textPrimary[0] * 0.62f, ct.textPrimary[1] * 0.62f, ct.textPrimary[2] * 0.62f, 1.0f),
        ct.rounding,
        false
    };
}

void applyTemplateStyle(GuiTemplate tpl, VulkanContext& vk) {
    applyPaletteStyle(getTemplatePalette(tpl), vk);
}

void applyPaletteStyle(const TemplatePalette& p, VulkanContext& vk) {
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
