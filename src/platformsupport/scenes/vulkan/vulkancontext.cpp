/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vulkancontext.h"
#include "core/graphicsbuffer.h"
#include "vulkanbackend.h"
#include "vulkanbuffer.h"
#include "vulkanframebuffer.h"
#include "vulkanpipelinemanager.h"
#include "vulkantexture.h"

#include <QDebug>
#include <cerrno>
#include <cstring>
#include <drm_fourcc.h>
#include <unistd.h>

namespace KWin
{

// Convert DRM format to Vulkan format
static VkFormat drmFormatToVkFormat(uint32_t drmFormat)
{
    switch (drmFormat) {
    case DRM_FORMAT_ARGB8888:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case DRM_FORMAT_XRGB8888:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case DRM_FORMAT_ABGR8888:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case DRM_FORMAT_XBGR8888:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case DRM_FORMAT_RGB888:
        return VK_FORMAT_R8G8B8_UNORM;
    case DRM_FORMAT_BGR888:
        return VK_FORMAT_B8G8R8_UNORM;
    case DRM_FORMAT_RGB565:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case DRM_FORMAT_BGR565:
        return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case DRM_FORMAT_ARGB2101010:
        return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    case DRM_FORMAT_XRGB2101010:
        return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    case DRM_FORMAT_ABGR2101010:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case DRM_FORMAT_XBGR2101010:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    default:
        qWarning() << "Unsupported DRM format:" << Qt::hex << drmFormat;
        return VK_FORMAT_UNDEFINED;
    }
}

VulkanContext *VulkanContext::s_currentContext = nullptr;

VulkanContext::VulkanContext(VulkanBackend *backend)
    : m_backend(backend)
{
    // Initialize VMA allocator
    if (!VulkanAllocator::initialize(backend)) {
        qWarning() << "Failed to initialize VMA allocator";
        return;
    }

    if (!createCommandPool()) {
        qWarning() << "Failed to create Vulkan command pool";
        return;
    }

    if (!createDescriptorPool()) {
        qWarning() << "Failed to create Vulkan descriptor pool";
        cleanup();
        return;
    }

    // Create pipeline manager
    m_pipelineManager = std::make_unique<VulkanPipelineManager>(this);

    // Create streaming vertex buffer (4MB initial size, can grow)
    m_streamingBuffer = VulkanBuffer::createStreamingBuffer(this, 4 * 1024 * 1024);

    // Check for DMA-BUF import support
    // This requires VK_EXT_external_memory_dma_buf extension
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2(m_backend->physicalDevice(), &props2);

    // Check if external memory extensions are available
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_backend->physicalDevice(), nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_backend->physicalDevice(), nullptr, &extensionCount, extensions.data());

    for (const auto &ext : extensions) {
        if (strcmp(ext.extensionName, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) == 0) {
            m_supportsDmaBufImport = true;
            break;
        }
    }

    qDebug() << "VulkanContext created, DMA-BUF import:" << m_supportsDmaBufImport;
}

VulkanContext::~VulkanContext()
{
    if (s_currentContext == this) {
        doneCurrent();
    }
    cleanup();
}

bool VulkanContext::createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_backend->graphicsQueueFamily();

    VkResult result = vkCreateCommandPool(m_backend->device(), &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create command pool:" << result;
        return false;
    }

    return true;
}

bool VulkanContext::createDescriptorPool()
{
    // Create a descriptor pool with enough descriptors for typical usage
    // Each render node needs 1 descriptor set with 2 bindings (texture + UBO)
    // With ~90 nodes/frame and proactive reset at 80%, we reset every ~1.5 seconds
    std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DESCRIPTOR_POOL_MAX_SETS},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DESCRIPTOR_POOL_MAX_SETS},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = DESCRIPTOR_POOL_MAX_SETS;

    VkResult result = vkCreateDescriptorPool(m_backend->device(), &poolInfo, nullptr, &m_descriptorPool);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create descriptor pool:" << result;
        return false;
    }

    qWarning() << "Created descriptor pool with maxSets=" << poolInfo.maxSets;
    return true;
}

void VulkanContext::cleanup()
{
    VkDevice device = m_backend->device();
    if (device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device);

    m_streamingBuffer.reset();
    m_pipelineManager.reset();

    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, m_fence, nullptr);
        m_fence = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    // Shutdown VMA allocator - must be done before device is destroyed
    VulkanAllocator::shutdown();
}

bool VulkanContext::makeCurrent()
{
    s_currentContext = this;
    return true;
}

void VulkanContext::doneCurrent()
{
    if (s_currentContext == this) {
        s_currentContext = nullptr;
    }
}

bool VulkanContext::isValid() const
{
    return m_commandPool != VK_NULL_HANDLE && m_descriptorPool != VK_NULL_HANDLE;
}

VulkanBackend *VulkanContext::backend() const
{
    return m_backend;
}

VkCommandPool VulkanContext::commandPool() const
{
    return m_commandPool;
}

VkDescriptorPool VulkanContext::descriptorPool() const
{
    return m_descriptorPool;
}

VulkanPipelineManager *VulkanContext::pipelineManager() const
{
    return m_pipelineManager.get();
}

VulkanBuffer *VulkanContext::streamingBuffer() const
{
    return m_streamingBuffer.get();
}

VkCommandBuffer VulkanContext::allocateCommandBuffer()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(m_backend->device(), &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to allocate command buffer:" << result;
        return VK_NULL_HANDLE;
    }

    return commandBuffer;
}

void VulkanContext::freeCommandBuffer(VkCommandBuffer commandBuffer)
{
    if (commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_backend->device(), m_commandPool, 1, &commandBuffer);
    }
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands()
{
    VkCommandBuffer commandBuffer = allocateCommandBuffer();
    if (commandBuffer == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_backend->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_backend->graphicsQueue());

    freeCommandBuffer(commandBuffer);
}

VkDescriptorSet VulkanContext::allocateDescriptorSet(VkDescriptorSetLayout layout)
{
    // Proactively reset pool when approaching capacity
    // This ensures we reset at a safe point before we run out
    if (m_descriptorAllocCount >= DESCRIPTOR_POOL_RESET_THRESHOLD) {
        qDebug() << "Proactive descriptor pool reset at" << m_descriptorAllocCount << "allocations";
        vkDeviceWaitIdle(m_backend->device());
        vkResetDescriptorPool(m_backend->device(), m_descriptorPool, 0);
        m_descriptorAllocCount = 0;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(m_backend->device(), &allocInfo, &descriptorSet);

    // If pool is exhausted, try emergency reset
    // VK_ERROR_OUT_OF_POOL_MEMORY = -1000069000, VK_ERROR_FRAGMENTED_POOL = -1000069001
    const bool poolExhausted = (result == VK_ERROR_OUT_OF_POOL_MEMORY) || (result == VK_ERROR_FRAGMENTED_POOL) || (static_cast<int>(result) == -1000069000) || (static_cast<int>(result) == -1000069001);
    if (poolExhausted) {
        qWarning() << "Descriptor pool exhausted at" << m_descriptorAllocCount << "allocations, emergency reset";
        vkDeviceWaitIdle(m_backend->device());
        vkResetDescriptorPool(m_backend->device(), m_descriptorPool, 0);
        m_descriptorAllocCount = 0;

        // Retry allocation
        result = vkAllocateDescriptorSets(m_backend->device(), &allocInfo, &descriptorSet);
    }

    if (result != VK_SUCCESS) {
        qWarning() << "Failed to allocate descriptor set:" << result;
        return VK_NULL_HANDLE;
    }

    m_descriptorAllocCount++;
    return descriptorSet;
}

void VulkanContext::resetDescriptorPool()
{
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(m_backend->device(), m_descriptorPool, 0);
        m_descriptorAllocCount = 0;
    }
}

std::unique_ptr<VulkanTexture> VulkanContext::importDmaBufAsTexture(const DmaBufAttributes &attributes)
{
    if (!m_supportsDmaBufImport) {
        qWarning() << "[DMA-BUF] Import not supported by Vulkan implementation";
        return nullptr;
    }

    // Check if the format/modifier combination is supported
    if (!checkFormatModifierSupport(attributes.format, attributes.modifier)) {
        qWarning() << "[DMA-BUF] Format/modifier combination not supported, aborting import";
        return nullptr;
    }

    qInfo() << "[DMA-BUF] Importing buffer:"
            << "size:" << attributes.width << "x" << attributes.height
            << "format:" << Qt::hex << attributes.format
            << "modifier:" << Qt::hex << attributes.modifier
            << "planes:" << attributes.planeCount;

    // Convert DRM format to Vulkan format
    VkFormat vkFormat = drmFormatToVkFormat(attributes.format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        qWarning() << "[DMA-BUF] Import failed: unsupported DRM format" << Qt::hex << attributes.format;
        return nullptr;
    }

    qInfo() << "[DMA-BUF] Using Vulkan format:" << vkFormat;

    // Build subresource layout for DRM format modifier
    std::vector<VkSubresourceLayout> planeLayouts;
    for (int i = 0; i < attributes.planeCount; i++) {
        VkSubresourceLayout layout{};
        layout.offset = attributes.offset[i];
        layout.rowPitch = attributes.pitch[i];
        layout.arrayPitch = 0;
        layout.depthPitch = 0;
        layout.size = 0; // Driver will determine size
        planeLayouts.push_back(layout);
    }

    // Set up DRM format modifier info
    VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo{};
    modifierInfo.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modifierInfo.drmFormatModifier = attributes.modifier;
    modifierInfo.drmFormatModifierPlaneCount = attributes.planeCount;
    modifierInfo.pPlaneLayouts = planeLayouts.data();

    // Create VkImage with external memory and DRM format modifier
    VkExternalMemoryImageCreateInfo externalMemoryInfo{};
    externalMemoryInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalMemoryInfo.pNext = &modifierInfo;
    externalMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalMemoryInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vkFormat;
    imageInfo.extent.width = attributes.width;
    imageInfo.extent.height = attributes.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    // Add VK_IMAGE_USAGE_TRANSFER_DST_BIT to allow the image to be used as a transfer destination
    // This provides more flexibility for operations like clearing or updating the image
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    VkResult result = vkCreateImage(m_backend->device(), &imageInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        qWarning() << "[DMA-BUF] Failed to create image:" << getVulkanResultString(result)
                   << "for format:" << Qt::hex << attributes.format
                   << "with modifier:" << Qt::hex << attributes.modifier;
        return nullptr;
    }

    qInfo() << "[DMA-BUF] VkImage created successfully";

    // Get memory requirements for the image
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_backend->device(), image, &memReqs);

    qInfo() << "[DMA-BUF] Memory requirements:"
            << "size:" << memReqs.size
            << "alignment:" << memReqs.alignment
            << "memoryTypeBits:" << Qt::hex << memReqs.memoryTypeBits;

    // Get dedicated memory requirements (may be more restrictive)
    VkMemoryDedicatedRequirements dedicatedReqs{};
    dedicatedReqs.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;

    VkMemoryRequirements2 memReqs2{};
    memReqs2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memReqs2.pNext = &dedicatedReqs;

    VkImageMemoryRequirementsInfo2 memReqsInfo{};
    memReqsInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    memReqsInfo.image = image;

    vkGetImageMemoryRequirements2(m_backend->device(), &memReqsInfo, &memReqs2);

    // Find appropriate memory type for DMA-BUF import
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_backend->physicalDevice(), &memProperties);

    // Use our optimized memory type selection function
    uint32_t memoryTypeIndex = findMemoryTypeForDmaBuf(memReqs.memoryTypeBits);

    if (memoryTypeIndex == UINT32_MAX) {
        qWarning() << "[DMA-BUF] Failed to find suitable memory type";
        vkDestroyImage(m_backend->device(), image, nullptr);
        return nullptr;
    }

    qInfo() << "[DMA-BUF] Using memory type index" << memoryTypeIndex;

    // Set up dedicated memory allocation if required
    VkMemoryDedicatedAllocateInfo dedicatedInfo{};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.image = image;

    // Import memory with VkImportMemoryFdInfoKHR
    VkImportMemoryFdInfoKHR importFdInfo{};
    importFdInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importFdInfo.pNext = (dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation) ? &dedicatedInfo : nullptr;
    importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    // dup() the fd because Vulkan takes ownership
    importFdInfo.fd = dup(attributes.fd[0].get());

    if (importFdInfo.fd < 0) {
        qWarning() << "[DMA-BUF] Failed to duplicate file descriptor:"
                   << strerror(errno) << "(errno:" << errno << ")";
        vkDestroyImage(m_backend->device(), image, nullptr);
        return nullptr;
    }

    qInfo() << "[DMA-BUF] Duplicated file descriptor:" << importFdInfo.fd
            << "from original:" << attributes.fd[0].get();

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &importFdInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory;
    result = vkAllocateMemory(m_backend->device(), &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        qWarning() << "[DMA-BUF] Failed to allocate memory:" << getVulkanResultString(result)
                   << "for memory type:" << memoryTypeIndex
                   << "with size:" << memReqs.size;
        close(importFdInfo.fd);
        vkDestroyImage(m_backend->device(), image, nullptr);
        return nullptr;
    }

    qInfo() << "[DMA-BUF] Memory allocated successfully, size:" << memReqs.size;

    // Bind memory to image
    result = vkBindImageMemory(m_backend->device(), image, memory, 0);
    if (result != VK_SUCCESS) {
        qWarning() << "[DMA-BUF] Failed to bind memory to image:" << getVulkanResultString(result)
                   << "for image:" << image << "memory:" << memory;
        vkFreeMemory(m_backend->device(), memory, nullptr);
        vkDestroyImage(m_backend->device(), image, nullptr);
        return nullptr;
    }

    qInfo() << "[DMA-BUF] Memory bound to image successfully";

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    result = vkCreateImageView(m_backend->device(), &viewInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        qWarning() << "[DMA-BUF] Failed to create image view:" << getVulkanResultString(result)
                   << "for format:" << viewInfo.format;
        vkFreeMemory(m_backend->device(), memory, nullptr);
        vkDestroyImage(m_backend->device(), image, nullptr);
        return nullptr;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler sampler;
    result = vkCreateSampler(m_backend->device(), &samplerInfo, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create sampler:" << result;
        vkDestroyImageView(m_backend->device(), imageView, nullptr);
        vkFreeMemory(m_backend->device(), memory, nullptr);
        vkDestroyImage(m_backend->device(), image, nullptr);
        return nullptr;
    }

    // Create VulkanTexture wrapper
    auto texture = std::unique_ptr<VulkanTexture>(new VulkanTexture(this));
    texture->m_image = image;
    texture->m_imageView = imageView;
    texture->m_sampler = sampler;
    texture->m_deviceMemory = memory; // Store device memory for cleanup
    texture->m_format = imageInfo.format;
    texture->m_size = QSize(attributes.width, attributes.height);
    texture->m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    texture->m_ownsImage = true;

    qInfo() << "[DMA-BUF] Import completed successfully:"
            << "size:" << attributes.width << "x" << attributes.height
            << "format:" << Qt::hex << attributes.format
            << "modifier:" << Qt::hex << attributes.modifier;

    return texture;
}

bool VulkanContext::supportsDmaBufImport() const
{
    return m_supportsDmaBufImport;
}

QString VulkanContext::getVulkanResultString(VkResult result)
{
    switch (result) {
    case VK_SUCCESS:
        return QStringLiteral("VK_SUCCESS");
    case VK_NOT_READY:
        return QStringLiteral("VK_NOT_READY");
    case VK_TIMEOUT:
        return QStringLiteral("VK_TIMEOUT");
    case VK_EVENT_SET:
        return QStringLiteral("VK_EVENT_SET");
    case VK_EVENT_RESET:
        return QStringLiteral("VK_EVENT_RESET");
    case VK_INCOMPLETE:
        return QStringLiteral("VK_INCOMPLETE");
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return QStringLiteral("VK_ERROR_OUT_OF_HOST_MEMORY");
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return QStringLiteral("VK_ERROR_OUT_OF_DEVICE_MEMORY");
    case VK_ERROR_INITIALIZATION_FAILED:
        return QStringLiteral("VK_ERROR_INITIALIZATION_FAILED");
    case VK_ERROR_DEVICE_LOST:
        return QStringLiteral("VK_ERROR_DEVICE_LOST");
    case VK_ERROR_MEMORY_MAP_FAILED:
        return QStringLiteral("VK_ERROR_MEMORY_MAP_FAILED");
    case VK_ERROR_LAYER_NOT_PRESENT:
        return QStringLiteral("VK_ERROR_LAYER_NOT_PRESENT");
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return QStringLiteral("VK_ERROR_EXTENSION_NOT_PRESENT");
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return QStringLiteral("VK_ERROR_FEATURE_NOT_PRESENT");
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return QStringLiteral("VK_ERROR_INCOMPATIBLE_DRIVER");
    case VK_ERROR_TOO_MANY_OBJECTS:
        return QStringLiteral("VK_ERROR_TOO_MANY_OBJECTS");
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return QStringLiteral("VK_ERROR_FORMAT_NOT_SUPPORTED");
    case VK_ERROR_FRAGMENTED_POOL:
        return QStringLiteral("VK_ERROR_FRAGMENTED_POOL");
    case VK_ERROR_UNKNOWN:
        return QStringLiteral("VK_ERROR_UNKNOWN");
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return QStringLiteral("VK_ERROR_OUT_OF_POOL_MEMORY");
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return QStringLiteral("VK_ERROR_INVALID_EXTERNAL_HANDLE");
    case VK_ERROR_FRAGMENTATION:
        return QStringLiteral("VK_ERROR_FRAGMENTATION");
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
        return QStringLiteral("VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS");
    case VK_ERROR_SURFACE_LOST_KHR:
        return QStringLiteral("VK_ERROR_SURFACE_LOST_KHR");
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return QStringLiteral("VK_ERROR_NATIVE_WINDOW_IN_USE_KHR");
    case VK_SUBOPTIMAL_KHR:
        return QStringLiteral("VK_SUBOPTIMAL_KHR");
    case VK_ERROR_OUT_OF_DATE_KHR:
        return QStringLiteral("VK_ERROR_OUT_OF_DATE_KHR");
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return QStringLiteral("VK_ERROR_INCOMPATIBLE_DISPLAY_KHR");
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return QStringLiteral("VK_ERROR_VALIDATION_FAILED_EXT");
    case VK_ERROR_INVALID_SHADER_NV:
        return QStringLiteral("VK_ERROR_INVALID_SHADER_NV");
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
        return QStringLiteral("VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT");
    case VK_ERROR_NOT_PERMITTED_EXT:
        return QStringLiteral("VK_ERROR_NOT_PERMITTED_EXT");
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return QStringLiteral("VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT");
    case VK_THREAD_IDLE_KHR:
        return QStringLiteral("VK_THREAD_IDLE_KHR");
    case VK_THREAD_DONE_KHR:
        return QStringLiteral("VK_THREAD_DONE_KHR");
    case VK_OPERATION_DEFERRED_KHR:
        return QStringLiteral("VK_OPERATION_DEFERRED_KHR");
    case VK_OPERATION_NOT_DEFERRED_KHR:
        return QStringLiteral("VK_OPERATION_NOT_DEFERRED_KHR");
    case VK_PIPELINE_COMPILE_REQUIRED_EXT:
        return QStringLiteral("VK_PIPELINE_COMPILE_REQUIRED_EXT");
    default:
        return QString("Unknown VkResult: %1").arg(result);
    }
}

bool VulkanContext::checkFormatModifierSupport(uint32_t drmFormat, uint64_t drmModifier)
{
    if (!m_supportsDmaBufImport) {
        qDebug() << "[DMA-BUF] Import not supported, format/modifier check skipped";
        return false;
    }

    // Convert DRM format to Vulkan format
    VkFormat vkFormat = drmFormatToVkFormat(drmFormat);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        qWarning() << "[DMA-BUF] Unsupported DRM format:" << Qt::hex << drmFormat;
        return false;
    }

    // Check if the VK_EXT_image_drm_format_modifier extension is available
    // This is required for querying format modifier properties
    PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
            vkGetInstanceProcAddr(m_backend->instance(), "vkGetPhysicalDeviceFormatProperties2"));

    if (!vkGetPhysicalDeviceFormatProperties2) {
        qWarning() << "[DMA-BUF] vkGetPhysicalDeviceFormatProperties2 not available";
        return false;
    }

    // Query format properties with modifiers
    VkDrmFormatModifierPropertiesListEXT modifierPropsList{};
    modifierPropsList.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

    VkFormatProperties2 formatProps{};
    formatProps.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    formatProps.pNext = &modifierPropsList;

    // First call to get count
    vkGetPhysicalDeviceFormatProperties2(m_backend->physicalDevice(), vkFormat, &formatProps);

    if (modifierPropsList.drmFormatModifierCount == 0) {
        qWarning() << "[DMA-BUF] No DRM format modifiers supported for format" << Qt::hex << drmFormat;
        return false;
    }

    // Allocate memory for modifier properties
    std::vector<VkDrmFormatModifierPropertiesEXT> modifierProps(modifierPropsList.drmFormatModifierCount);
    modifierPropsList.pDrmFormatModifierProperties = modifierProps.data();

    // Second call to get data
    vkGetPhysicalDeviceFormatProperties2(m_backend->physicalDevice(), vkFormat, &formatProps);

    // Check if our modifier is supported
    bool supported = false;
    for (const auto &prop : modifierProps) {
        if (prop.drmFormatModifier == drmModifier) {
            qDebug() << "[DMA-BUF] Format" << Qt::hex << drmFormat
                     << "with modifier" << Qt::hex << drmModifier << "is supported";
            supported = true;
            break;
        }
    }

    if (!supported) {
        qWarning() << "[DMA-BUF] Format" << Qt::hex << drmFormat
                   << "with modifier" << Qt::hex << drmModifier << "is NOT supported";

        // Log available modifiers for debugging
        qDebug() << "[DMA-BUF] Supported modifiers for format" << Qt::hex << drmFormat << ":";
        for (const auto &prop : modifierProps) {
            qDebug() << "  - Modifier:" << Qt::hex << prop.drmFormatModifier;
        }
    }

    return supported;
}

uint32_t VulkanContext::findMemoryTypeForDmaBuf(uint32_t memoryTypeBits)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_backend->physicalDevice(), &memProperties);

    // For DMA-BUF, we want device-local memory that is also host-visible if possible
    // This allows both zero-copy and fallback paths
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        const bool isCompatible = (memoryTypeBits & (1 << i));
        const bool isDeviceLocal = (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const bool isHostVisible = (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        if (isCompatible && isDeviceLocal && isHostVisible) {
            qDebug() << "[DMA-BUF] Selected memory type" << i
                     << "with device-local and host-visible properties";
            return i;
        }
    }

    // Fallback to device-local only
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            qDebug() << "[DMA-BUF] Selected memory type" << i
                     << "with device-local property";
            return i;
        }
    }

    // Last resort: any compatible memory
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (memoryTypeBits & (1 << i)) {
            qDebug() << "[DMA-BUF] Selected memory type" << i
                     << "(no special properties)";
            return i;
        }
    }

    qWarning() << "[DMA-BUF] Failed to find suitable memory type";
    return UINT32_MAX;
}

void VulkanContext::pushFramebuffer(VulkanFramebuffer *fbo)
{
    m_framebufferStack.push(fbo);
}

VulkanFramebuffer *VulkanContext::popFramebuffer()
{
    if (m_framebufferStack.isEmpty()) {
        return nullptr;
    }
    return m_framebufferStack.pop();
}

VulkanFramebuffer *VulkanContext::currentFramebuffer()
{
    if (m_framebufferStack.isEmpty()) {
        return nullptr;
    }
    return m_framebufferStack.top();
}

VulkanContext *VulkanContext::currentContext()
{
    return s_currentContext;
}

VkFence VulkanContext::getOrCreateFence()
{
    if (m_fence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = 0;

        VkResult result = vkCreateFence(m_backend->device(), &fenceInfo, nullptr, &m_fence);
        if (result != VK_SUCCESS) {
            qWarning() << "Failed to create fence:" << result;
            return VK_NULL_HANDLE;
        }
    }
    return m_fence;
}

VkFence VulkanContext::createExportableFence()
{
    if (!m_backend->supportsExternalFenceFd()) {
        return VK_NULL_HANDLE;
    }

    // Set up export info for sync fd
    VkExportFenceCreateInfo exportInfo{};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = &exportInfo;
    fenceInfo.flags = 0;

    VkFence fence = VK_NULL_HANDLE;
    VkResult result = vkCreateFence(m_backend->device(), &fenceInfo, nullptr, &fence);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create exportable fence:" << result;
        return VK_NULL_HANDLE;
    }

    return fence;
}

FileDescriptor VulkanContext::exportFenceToSyncFd(VkFence fence)
{
    if (!m_backend->supportsExternalFenceFd() || fence == VK_NULL_HANDLE) {
        return FileDescriptor();
    }

    VkFenceGetFdInfoKHR getFdInfo{};
    getFdInfo.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
    getFdInfo.fence = fence;
    getFdInfo.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

    int fd = -1;
    VkResult result = m_backend->vkGetFenceFdKHR()(m_backend->device(), &getFdInfo, &fd);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to export fence to sync fd:" << result;
        return FileDescriptor();
    }

    return FileDescriptor(fd);
}

bool VulkanContext::supportsExternalFenceFd() const
{
    return m_backend->supportsExternalFenceFd();
}

} // namespace KWin
