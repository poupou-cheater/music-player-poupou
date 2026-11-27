#include "vulkan/VulkanContext.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <stdexcept>
#include <vector>
#include <cstdio>

static void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Error VkResult = %d\n", static_cast<int>(err));
    }
}

bool VulkanContext::init(GLFWwindow* window) {
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] volkInitialize failed (vulkan-1.dll missing?)\n");
        return false;
    }

    uint32_t extensionsCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionsCount);
    if (!setupVulkan(extensions, extensionsCount)) {
        return false;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult err = glfwCreateWindowSurface(m_instance, window, m_allocator, &surface);
    if (err != VK_SUCCESS) {
        checkVkResult(err);
        return false;
    }

    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    if (!setupVulkanWindow(surface, w, h)) {
        return false;
    }

    // Create dedicated command pool & fence for immediate texture uploads
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    checkVkResult(vkCreateCommandPool(m_device, &poolInfo, m_allocator, &m_uploadCommandPool));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_uploadCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    checkVkResult(vkAllocateCommandBuffers(m_device, &allocInfo, &m_uploadCommandBuffer));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    checkVkResult(vkCreateFence(m_device, &fenceInfo, m_allocator, &m_uploadFence));

    setClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    return true;
}

bool VulkanContext::setupVulkan(const char** extensions, uint32_t extensionsCount) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "StreamPulse";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "StreamPulseVulkan";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionsCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult err = vkCreateInstance(&createInfo, m_allocator, &m_instance);
    if (err != VK_SUCCESS) {
        checkVkResult(err);
        return false;
    }
    volkLoadInstance(m_instance);

    // Select Physical Device (prefer discrete GPU)
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);
    if (gpuCount == 0) return false;

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, gpus.data());

    m_physicalDevice = gpus[0];
    for (auto gpu : gpus) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = gpu;
            break;
        }
    }

    // Find graphics queue family
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count, queues.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_queueFamily = i;
            break;
        }
    }
    if (m_queueFamily == static_cast<uint32_t>(-1)) return false;

    // Create logical device
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    err = vkCreateDevice(m_physicalDevice, &deviceInfo, m_allocator, &m_device);
    if (err != VK_SUCCESS) {
        checkVkResult(err);
        return false;
    }
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    // Descriptor Pool for ImGui + all textures (icons, album art, video frames)
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048 },
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 2048;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    err = vkCreateDescriptorPool(m_device, &poolInfo, m_allocator, &m_descriptorPool);
    checkVkResult(err);

    return err == VK_SUCCESS;
}

bool VulkanContext::setupVulkanWindow(VkSurfaceKHR surface, int width, int height) {
    m_mainWindowData.Surface = surface;

    VkBool32 res = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily, surface, &res);
    if (res != VK_TRUE) {
        std::fprintf(stderr, "[Vulkan] No WSI support on physical device\n");
        return false;
    }

    const VkFormat requestSurfaceImageFormat[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM
    };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    m_mainWindowData.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        m_physicalDevice, m_mainWindowData.Surface,
        requestSurfaceImageFormat, 4, requestSurfaceColorSpace
    );

    VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_FIFO_KHR };
    m_mainWindowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        m_physicalDevice, m_mainWindowData.Surface, presentModes, 1
    );

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_instance, m_physicalDevice, m_device, &m_mainWindowData,
        m_queueFamily, m_allocator, width, height, m_minImageCount
    );
    return true;
}

void VulkanContext::handleResize(int width, int height) {
    if (width > 0 && height > 0) {
        m_pendingWidth = width;
        m_pendingHeight = height;
        m_swapChainRebuild = true;
    }
}

void VulkanContext::deferDestroy(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(m_deferMutex);
    m_deferredDeletes.push_back({ 5, std::move(fn) });
}

bool VulkanContext::beginFrame() {
    {
        std::lock_guard<std::mutex> lock(m_deferMutex);
        for (auto it = m_deferredDeletes.begin(); it != m_deferredDeletes.end(); ) {
            if (--it->framesLeft <= 0) {
                it->fn();
                it = m_deferredDeletes.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (m_swapChainRebuild && m_pendingWidth > 0 && m_pendingHeight > 0) {
        ImGui_ImplVulkan_SetMinImageCount(m_minImageCount);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            m_instance, m_physicalDevice, m_device, &m_mainWindowData,
            m_queueFamily, m_allocator, m_pendingWidth, m_pendingHeight, m_minImageCount
        );
        m_mainWindowData.FrameIndex = 0;
        m_swapChainRebuild = false;
    }
    return true;
}

void VulkanContext::renderFrame(ImDrawData* drawData) {
    ImGui_ImplVulkanH_Window* wd = &m_mainWindowData;
    VkSemaphore imageAcquired = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore renderComplete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    VkResult err = vkAcquireNextImageKHR(
        m_device, wd->Swapchain, UINT64_MAX, imageAcquired, VK_NULL_HANDLE, &wd->FrameIndex
    );
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        m_swapChainRebuild = true;
        return;
    }
    checkVkResult(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    checkVkResult(vkWaitForFences(m_device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    checkVkResult(vkResetFences(m_device, 1, &fd->Fence));

    checkVkResult(vkResetCommandPool(m_device, fd->CommandPool, 0));
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVkResult(vkBeginCommandBuffer(fd->CommandBuffer, &info));

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = wd->RenderPass;
    rpInfo.framebuffer = fd->Framebuffer;
    rpInfo.renderArea.extent.width = wd->Width;
    rpInfo.renderArea.extent.height = wd->Height;
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    checkVkResult(vkEndCommandBuffer(fd->CommandBuffer));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAcquired;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &fd->CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderComplete;

    checkVkResult(vkQueueSubmit(m_queue, 1, &submitInfo, fd->Fence));
}

void VulkanContext::presentFrame() {
    if (m_swapChainRebuild) return;
    ImGui_ImplVulkanH_Window* wd = &m_mainWindowData;
    VkSemaphore renderComplete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &renderComplete;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;

    VkResult err = vkQueuePresentKHR(m_queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        m_swapChainRebuild = true;
        return;
    }
    checkVkResult(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

void VulkanContext::waitIdle() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type");
}

void VulkanContext::executeImmediate(const std::function<void(VkCommandBuffer)>& fn) {
    std::lock_guard<std::mutex> lock(m_uploadMutex);
    checkVkResult(vkResetCommandPool(m_device, m_uploadCommandPool, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVkResult(vkBeginCommandBuffer(m_uploadCommandBuffer, &beginInfo));

    fn(m_uploadCommandBuffer);

    checkVkResult(vkEndCommandBuffer(m_uploadCommandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_uploadCommandBuffer;
    checkVkResult(vkQueueSubmit(m_queue, 1, &submitInfo, m_uploadFence));

    checkVkResult(vkWaitForFences(m_device, 1, &m_uploadFence, VK_TRUE, UINT64_MAX));
    checkVkResult(vkResetFences(m_device, 1, &m_uploadFence));
}

void VulkanContext::setClearColor(float r, float g, float b, float a) {
    m_mainWindowData.ClearValue.color.float32[0] = r;
    m_mainWindowData.ClearValue.color.float32[1] = g;
    m_mainWindowData.ClearValue.color.float32[2] = b;
    m_mainWindowData.ClearValue.color.float32[3] = a;
}

void VulkanContext::shutdown() {
    waitIdle();
    {
        std::lock_guard<std::mutex> lock(m_deferMutex);
        for (auto& d : m_deferredDeletes) d.fn();
        m_deferredDeletes.clear();
    }
    if (m_uploadFence != VK_NULL_HANDLE) vkDestroyFence(m_device, m_uploadFence, m_allocator);
    if (m_uploadCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_uploadCommandPool, m_allocator);

    ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_mainWindowData, m_allocator);
    if (m_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptorPool, m_allocator);
    if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, m_allocator);
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, m_allocator);
}
