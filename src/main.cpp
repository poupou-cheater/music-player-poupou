#include "vulkan/VulkanContext.hpp"
#include "media/MediaPlayer.hpp"
#include "library/LibraryManager.hpp"
#include "library/PlatformSync.hpp"
#include "library/LyricsManager.hpp"
#include "library/StatsManager.hpp"
#include "library/DiscordRPC.hpp"
#include "ui/IconStore.hpp"
#include "ui/MediaKeys.hpp"
#include "ui/AppUI.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <windows.h>
#include <chrono>
#include <cstdio>

static VulkanContext* g_vkCtx = nullptr;

static void glfwFramebufferResizeCallback(GLFWwindow* /*window*/, int width, int height) {
    if (g_vkCtx) {
        g_vkCtx->handleResize(width, height);
    }
}

int main() {
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    LibraryManager library;
    library.load("data/library.json");
    AppSettings initSettings = library.settings();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    int startW = (initSettings.winW >= 640) ? initSettings.winW : 1400;
    int startH = (initSettings.winH >= 480) ? initSettings.winH : 860;
    GLFWwindow* window = glfwCreateWindow(startW, startH, "StreamPulse - C++ Vulkan Media Player", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    if (initSettings.winX >= -32000 && initSettings.winY >= -32000 && initSettings.winX != -1) {
        glfwSetWindowPos(window, initSettings.winX, initSettings.winY);
    }
    if (initSettings.winMaximized) {
        glfwMaximizeWindow(window);
    }

    // Enable Windows Dark Title Bar (DWMWA_USE_IMMERSIVE_DARK_MODE)
    if (HWND hwnd = glfwGetWin32Window(window)) {
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark, sizeof(useDark));
        DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
        COLORREF captionCol = RGB(12, 12, 14);
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &captionCol, sizeof(captionCol));
    }

    VulkanContext vk;
    g_vkCtx = &vk;
    glfwSetFramebufferSizeCallback(window, glfwFramebufferResizeCallback);

    if (!vk.init(window)) {
        std::fprintf(stderr, "Failed to initialize Vulkan\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES) {
        static const ImWchar latinAndSymbols[] = {
            0x0020, 0x024F, // Basic Latin + Latin Extended
            0x0400, 0x04FF, // Cyrillic
            0x2000, 0x27BF, // Punctuation, Arrows, Math, Dingbats
            0,
        };
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.5f, nullptr, latinAndSymbols);

        ImFontConfig mergeCfg;
        mergeCfg.MergeMode = true;
        if (GetFileAttributesW(L"C:\\Windows\\Fonts\\seguisym.ttf") != INVALID_FILE_ATTRIBUTES) {
            static const ImWchar symRange[] = { 0x2000, 0x2BFF, 0 };
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 17.5f, &mergeCfg, symRange);
        }
        if (GetFileAttributesW(L"C:\\Windows\\Fonts\\seguiemj.ttf") != INVALID_FILE_ATTRIBUTES) {
            static const ImWchar emjRange[] = { 0x1F300, 0x1FAFF, 0 };
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguiemj.ttf", 17.5f, &mergeCfg, emjRange);
        }
        if (GetFileAttributesW(L"C:\\Windows\\Fonts\\YuGothR.ttc") != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\YuGothR.ttc", 17.5f, &mergeCfg, io.Fonts->GetGlyphRangesJapanese());
        } else if (GetFileAttributesW(L"C:\\Windows\\Fonts\\meiryo.ttc") != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 17.5f, &mergeCfg, io.Fonts->GetGlyphRangesJapanese());
        } else if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msgothic.ttc") != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msgothic.ttc", 17.5f, &mergeCfg, io.Fonts->GetGlyphRangesJapanese());
        }
        if (GetFileAttributesW(L"C:\\Windows\\Fonts\\malgun.ttf") != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 17.5f, &mergeCfg, io.Fonts->GetGlyphRangesKorean());
        }
        if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 17.5f, &mergeCfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
    }
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = vk.instance();
    initInfo.PhysicalDevice = vk.physicalDevice();
    initInfo.Device = vk.device();
    initInfo.QueueFamily = vk.queueFamily();
    initInfo.Queue = vk.graphicsQueue();
    initInfo.DescriptorPool = vk.descriptorPool();
    initInfo.RenderPass = vk.mainWindowData()->RenderPass;
    initInfo.MinImageCount = vk.minImageCount();
    initInfo.ImageCount = vk.mainWindowData()->ImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);

    IconStore icons;
    icons.loadAll(vk, "assets/icons", 64);

    StatsManager stats;
    stats.load("data/stats.json");

    LyricsManager lyrics;
    DiscordRPC discord;

    MediaKeys mediaKeys;
    mediaKeys.init(window);

    PlatformSync sync(library);
    sync.start();

    MediaPlayer player;
    player.init();

    AppUI ui(vk, icons, library, sync, player, lyrics, stats, discord, mediaKeys);
    ui.init(window);

    auto lastTime = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) {
            glfwWaitEvents();
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        ui.update(dt);

        vk.beginFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui.render();

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData->DisplaySize.x > 0.0f && drawData->DisplaySize.y > 0.0f) {
            vk.renderFrame(drawData);
            vk.presentFrame();
        }
    }

    // Save exact OS window position, size, and maximized state on exit
    {
        auto st = library.settings();
        st.winMaximized = (glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE);
        if (!st.winMaximized) {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);
            if (ww >= 640 && wh >= 480) {
                st.winX = wx;
                st.winY = wy;
                st.winW = ww;
                st.winH = wh;
            }
        }
        library.updateSettings(st);
    }

    vk.waitIdle();
    ui.shutdown();
    player.shutdown();
    sync.stop();
    mediaKeys.shutdown();
    discord.shutdown();
    stats.save();
    library.save();
    icons.destroyAll(vk);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vk.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
