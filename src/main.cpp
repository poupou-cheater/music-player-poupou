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

#include <GLFW/glfw3.h>
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

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1400, 860, "StreamPulse - C++ Vulkan Media Player", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
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

    LibraryManager library;
    library.load("data/library.json");

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
