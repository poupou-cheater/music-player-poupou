#include "vulkan/VulkanContext.hpp"
#include "media/MediaPlayer.hpp"
#include "library/LibraryManager.hpp"
#include "library/PlatformSync.hpp"
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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    int startW = (initSettings.winW >= 640) ? initSettings.winW : 1400;
    int startH = (initSettings.winH >= 480) ? initSettings.winH : 860;
    GLFWwindow* window = glfwCreateWindow(startW, startH, "StreamPulse - C++ Vulkan Media Player", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    if (initSettings.winOsFullscreen) {
        int mCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&mCount);
        GLFWmonitor* mon = (initSettings.winMonitorIndex >= 0 && initSettings.winMonitorIndex < mCount)
            ? monitors[initSettings.winMonitorIndex]
            : glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
        if (mon && mode) {
            glfwSetWindowMonitor(window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        glfwShowWindow(window);
    } else if (initSettings.winMaximized) {
        int mCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&mCount);
        GLFWmonitor* mon = (initSettings.winMonitorIndex >= 0 && initSettings.winMonitorIndex < mCount)
            ? monitors[initSettings.winMonitorIndex]
            : glfwGetPrimaryMonitor();
        if (mon) {
            int mx = 0, my = 0;
            glfwGetMonitorPos(mon, &mx, &my);
            glfwSetWindowPos(window, mx + 40, my + 40);
        }
        glfwShowWindow(window);
        glfwMaximizeWindow(window);
    } else {
        if (initSettings.winX >= -32000 && initSettings.winY >= -32000 && initSettings.winX != -1) {
            glfwSetWindowPos(window, initSettings.winX, initSettings.winY);
        }
        glfwShowWindow(window);
    }

    // Enable Windows Dark Title Bar + Set App Icon from embedded resource 101
    if (HWND hwnd = glfwGetWin32Window(window)) {
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark, sizeof(useDark));
        DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
        COLORREF captionCol = RGB(12, 12, 14);
        DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &captionCol, sizeof(captionCol));

        HICON hIconBig = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 64, 64, 0));
        HICON hIconSmall = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, 0));
        if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
        if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
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

    DiscordRPC discord;

    MediaKeys mediaKeys;
    mediaKeys.init(window);

    PlatformSync sync(library);
    sync.start();

    MediaPlayer player;
    player.init();

    AppUI ui(vk, icons, library, sync, player, stats, discord, mediaKeys);
    ui.init(window);

    glfwSetWindowSizeLimits(window, 340, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
    auto lastTime = std::chrono::steady_clock::now();

    auto tickFrame = [&]() {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) return;

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
    };

    glfwSetWindowUserPointer(window, &tickFrame);
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* w) {
        if (auto* fn = static_cast<decltype(tickFrame)*>(glfwGetWindowUserPointer(w))) {
            (*fn)();
        }
    });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) {
            glfwWaitEvents();
            continue;
        }

        tickFrame();
    }

    // Save exact OS window position, size, monitor index, maximized, and OS fullscreen state on exit
    {
        auto st = library.settings();
        GLFWmonitor* fsMon = glfwGetWindowMonitor(window);
        st.winOsFullscreen = (fsMon != nullptr);
        st.winMaximized = (!st.winOsFullscreen && glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE);

        int wx = 0, wy = 0, ww = 0, wh = 0;
        glfwGetWindowPos(window, &wx, &wy);
        glfwGetWindowSize(window, &ww, &wh);
        int cx = wx + ww / 2;
        int cy = wy + wh / 2;

        int mCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&mCount);
        st.winMonitorIndex = 0;
        for (int i = 0; i < mCount; ++i) {
            if (fsMon == monitors[i]) { st.winMonitorIndex = i; break; }
            int mx = 0, my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode && cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
                st.winMonitorIndex = i;
            }
        }

        if (!st.winMaximized && !st.winOsFullscreen && ww >= 340 && wh >= 480) {
            st.winX = wx;
            st.winY = wy;
            st.winW = ww;
            st.winH = wh;
        }
        library.updateSettings(st);
        library.save();
        stats.save();
    }

    vk.waitIdle();
    ui.shutdown();
    player.shutdown();
    sync.stop();
    mediaKeys.shutdown();
    discord.shutdown();
    icons.destroyAll(vk);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vk.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
