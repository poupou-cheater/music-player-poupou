#pragma once

#include <volk.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_vulkan.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class VulkanContext {
public:
    bool init(GLFWwindow* window);
    void shutdown();

    void handleResize(int width, int height);
    bool beginFrame();
    void renderFrame(ImDrawData* drawData);
    void presentFrame();
    void waitIdle();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void executeImmediate(const std::function<void(VkCommandBuffer)>& fn);
    void deferDestroy(std::function<void()> fn);

    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_queue; }
    uint32_t queueFamily() const { return m_queueFamily; }
    VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
    ImGui_ImplVulkanH_Window* mainWindowData() { return &m_mainWindowData; }
    uint32_t minImageCount() const { return m_minImageCount; }

    void setClearColor(float r, float g, float b, float a = 1.0f);

private:
    bool setupVulkan(const char** extensions, uint32_t extensionsCount);
    bool setupVulkanWindow(VkSurfaceKHR surface, int width, int height);

    VkAllocationCallbacks* m_allocator = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = static_cast<uint32_t>(-1);
    VkQueue m_queue = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_uploadCommandBuffer = VK_NULL_HANDLE;
    VkFence m_uploadFence = VK_NULL_HANDLE;
    std::mutex m_uploadMutex;

    ImGui_ImplVulkanH_Window m_mainWindowData{};
    uint32_t m_minImageCount = 2;
    bool m_swapChainRebuild = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;

    struct DeferredItem {
        int framesLeft;
        std::function<void()> fn;
    };
    std::mutex m_deferMutex;
    std::vector<DeferredItem> m_deferredDeletes;
};
