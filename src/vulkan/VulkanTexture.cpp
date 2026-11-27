#include "vulkan/VulkanTexture.hpp"
#include "vulkan/VulkanContext.hpp"

#include <imgui_impl_vulkan.h>
#include <cstring>

bool VulkanTexture::ensureStagingBuffer(VulkanContext& ctx, VkDeviceSize size) {
    if (m_stagingBuffer != VK_NULL_HANDLE && m_stagingSize >= size) {
        return true;
    }
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(ctx.device(), m_stagingBuffer, nullptr);
        vkFreeMemory(ctx.device(), m_stagingMemory, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingMemory = VK_NULL_HANDLE;
        m_stagingSize = 0;
    }

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device(), &bufInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(ctx.device(), m_stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = ctx.findMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (vkAllocateMemory(ctx.device(), &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
        return false;
    }
    vkBindBufferMemory(ctx.device(), m_stagingBuffer, m_stagingMemory, 0);
    m_stagingSize = size;
    return true;
}

bool VulkanTexture::createFromRGBA(VulkanContext& ctx, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0) return false;
    destroy(ctx);

    m_width = width;
    m_height = height;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = { width, height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ctx.device(), &imgInfo, nullptr, &m_image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(ctx.device(), m_image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = ctx.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(ctx.device(), &allocInfo, nullptr, &m_imageMemory) != VK_SUCCESS) {
        return false;
    }
    vkBindImageMemory(ctx.device(), m_image, m_imageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(ctx.device(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    if (vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        return false;
    }

    m_descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, m_imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return updateRGBA(ctx, rgba, width, height);
}

bool VulkanTexture::updateRGBA(VulkanContext& ctx, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0) return false;
    if (m_image == VK_NULL_HANDLE || width != m_width || height != m_height) {
        return createFromRGBA(ctx, rgba, width, height);
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
    if (!ensureStagingBuffer(ctx, imageSize)) {
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(ctx.device(), m_stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(ctx.device(), m_stagingMemory);

    ctx.executeImmediate([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = m_image;
        toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toDst
        );

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = m_image;
        toShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShader
        );
    });

    return true;
}

void VulkanTexture::destroy(VulkanContext& ctx) {
    if (ctx.device() == VK_NULL_HANDLE) return;

    VkDescriptorSet desc = m_descriptorSet;
    VkSampler samp = m_sampler;
    VkImageView view = m_imageView;
    VkImage img = m_image;
    VkDeviceMemory imgMem = m_imageMemory;
    VkBuffer stgBuf = m_stagingBuffer;
    VkDeviceMemory stgMem = m_stagingMemory;

    m_descriptorSet = VK_NULL_HANDLE;
    m_sampler = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_imageMemory = VK_NULL_HANDLE;
    m_stagingBuffer = VK_NULL_HANDLE;
    m_stagingMemory = VK_NULL_HANDLE;
    m_stagingSize = 0;
    m_width = 0;
    m_height = 0;

    if (desc == VK_NULL_HANDLE && img == VK_NULL_HANDLE && stgBuf == VK_NULL_HANDLE) return;

    VkDevice dev = ctx.device();
    ctx.deferDestroy([dev, desc, samp, view, img, imgMem, stgBuf, stgMem]() {
        if (desc != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(desc);
        if (samp != VK_NULL_HANDLE) vkDestroySampler(dev, samp, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(dev, view, nullptr);
        if (img != VK_NULL_HANDLE) vkDestroyImage(dev, img, nullptr);
        if (imgMem != VK_NULL_HANDLE) vkFreeMemory(dev, imgMem, nullptr);
        if (stgBuf != VK_NULL_HANDLE) vkDestroyBuffer(dev, stgBuf, nullptr);
        if (stgMem != VK_NULL_HANDLE) vkFreeMemory(dev, stgMem, nullptr);
    });
}
