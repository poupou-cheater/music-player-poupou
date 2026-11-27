#pragma once

#include <volk.h>
#include <imgui.h>
#include <cstdint>

class VulkanContext;

class VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() = default;

    bool createFromRGBA(VulkanContext& ctx, const uint8_t* rgba, uint32_t width, uint32_t height);
    bool updateRGBA(VulkanContext& ctx, const uint8_t* rgba, uint32_t width, uint32_t height);
    void destroy(VulkanContext& ctx);

    bool isValid() const { return m_descriptorSet != VK_NULL_HANDLE; }
    ImTextureID imguiId() const { return reinterpret_cast<ImTextureID>(m_descriptorSet); }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

private:
    bool ensureStagingBuffer(VulkanContext& ctx, VkDeviceSize size);

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize m_stagingSize = 0;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};
