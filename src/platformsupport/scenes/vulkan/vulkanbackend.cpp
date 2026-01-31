/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vulkanbackend.h"
#include "utils/common.h"
#include "vulkancontext.h"
#include "vulkanframebuffer.h"
#include "vulkantexture.h"

#include <QDebug>
#include <vector>

namespace KWin
{

static QString getVulkanResultString(VkResult result)
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

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
    switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        qDebug() << "Vulkan validation (verbose):" << pCallbackData->pMessage;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        qInfo() << "Vulkan validation (info):" << pCallbackData->pMessage;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        qWarning() << "Vulkan validation (warning):" << pCallbackData->pMessage;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        qCCritical(KWIN_CORE) << "Vulkan validation (error):" << pCallbackData->pMessage;
        break;
    default:
        qDebug() << "Vulkan validation:" << pCallbackData->pMessage;
        break;
    }
    return VK_FALSE;
}

VulkanBackend::VulkanBackend()
    : RenderBackend()
    , m_failed(false)
{
}

VulkanBackend::~VulkanBackend()
{
    cleanup();
}

CompositingType VulkanBackend::compositingType() const
{
    return VulkanCompositing;
}

bool VulkanBackend::checkGraphicsReset()
{
    // Check if device is lost
    if (m_device != VK_NULL_HANDLE) {
        VkResult result = vkDeviceWaitIdle(m_device);
        if (result == VK_ERROR_DEVICE_LOST) {
            qWarning() << "Vulkan device lost";
            return true;
        }
    }
    return false;
}

void VulkanBackend::setFailed(const QString &reason)
{
    qWarning() << "Creating Vulkan backend failed:" << reason;
    m_failed = true;
}

bool VulkanBackend::createInstance(const QList<const char *> &requiredExtensions)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "KWin";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "KWin";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Convert QList to std::vector
    std::vector<const char *> extensions;
    for (const char *ext : requiredExtensions) {
        extensions.push_back(ext);
    }

#ifndef NDEBUG
    // Enable validation layers in debug builds
    const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    // Add debug utils extension
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#else
    createInfo.enabledLayerCount = 0;
#endif

    // Check for instance-level external fence capabilities extension
    uint32_t instanceExtensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());

    m_hasExternalFenceCapabilities = false;
    for (const auto &ext : instanceExtensions) {
        if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME) == 0) {
            m_hasExternalFenceCapabilities = true;
            qInfo() << "[DMA-BUF] Found instance extension:" << VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME;
            // Enable the capabilities extension at instance level
            extensions.push_back(VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME);
            break;
        }
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        setFailed(QString("Failed to create Vulkan instance: %1 (VK_ERROR: %2)").arg(result).arg(getVulkanResultString(result)));
        return false;
    }

#ifndef NDEBUG
    // Set up debug messenger
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(m_instance, &debugCreateInfo, nullptr, &m_debugMessenger);
    }
#endif

    qDebug() << "Vulkan instance created successfully";
    return true;
}

bool VulkanBackend::selectPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        setFailed("Failed to find GPUs with Vulkan support");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Pick the first suitable device (could be improved with device scoring)
    for (const auto &device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        // Find graphics queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_physicalDevice = device;
                m_graphicsQueueFamily = i;
                qDebug() << "Selected Vulkan device:" << deviceProperties.deviceName;
                return true;
            }
        }
    }

    setFailed("Failed to find a suitable GPU");
    return false;
}

bool VulkanBackend::createDevice(const QList<const char *> &requiredDeviceExtensions)
{
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;

    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

    // Convert QList to std::vector
    std::vector<const char *> extensions;
    for (const char *ext : requiredDeviceExtensions) {
        extensions.push_back(ext);
    }

    // Check for available extensions
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    // Check for DMA-BUF import support extensions
    bool hasExternalMemoryFd = false;
    bool hasDmaBufImport = false;
    bool hasDrmFormatModifier = false;

    qInfo() << "[DMA-BUF] Checking for required Vulkan extensions...";
    bool hasExternalFenceFd = false;

    for (const auto &ext : availableExtensions) {
        if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0) {
            hasExternalMemoryFd = true;
            qInfo() << "[DMA-BUF] Found extension:" << VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        } else if (strcmp(ext.extensionName, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) == 0) {
            hasDmaBufImport = true;
            qInfo() << "[DMA-BUF] Found extension:" << VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
        } else if (strcmp(ext.extensionName, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME) == 0) {
            hasDrmFormatModifier = true;
            qInfo() << "[DMA-BUF] Found extension:" << VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
        } else if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME) == 0) {
            hasExternalFenceFd = true;
            qInfo() << "[DMA-BUF] Found extension:" << VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME;
        }
    }

    // Enable DMA-BUF import extensions if available
    if (hasExternalMemoryFd && hasDmaBufImport && hasDrmFormatModifier) {
        extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        extensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        extensions.push_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
        qInfo() << "[DMA-BUF] All required extensions found and enabled:";
        qInfo() << "[DMA-BUF]   - " << VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        qInfo() << "[DMA-BUF]   - " << VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
        qInfo() << "[DMA-BUF]   - " << VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
    } else {
        qWarning() << "[DMA-BUF] Import not supported - missing extensions:";
        if (!hasExternalMemoryFd) {
            qWarning() << "[DMA-BUF]   - Missing" << VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        }
        if (!hasDmaBufImport) {
            qWarning() << "[DMA-BUF]   - Missing" << VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
        }
        if (!hasDrmFormatModifier) {
            qWarning() << "[DMA-BUF]   - Missing" << VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
        }
    }

    // Enable external fence fd extension if available (requires instance-level external_fence_capabilities)
    if (hasExternalFenceFd && m_hasExternalFenceCapabilities) {
        extensions.push_back(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
        m_supportsExternalFenceFd = true;
        qDebug() << "VK_KHR_external_fence_fd extension enabled";
    } else if (hasExternalFenceFd && !m_hasExternalFenceCapabilities) {
        qDebug() << "VK_KHR_external_fence_fd available but external_fence_capabilities not supported";
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
    const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};
    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#else
    createInfo.enabledLayerCount = 0;
#endif

    VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        setFailed(QString("Failed to create logical device: %1 (%2)").arg(result).arg(getVulkanResultString(result)));
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);

    // Load extension function pointers
    if (m_supportsExternalFenceFd) {
        m_vkGetFenceFdKHR = reinterpret_cast<PFN_vkGetFenceFdKHR>(
            vkGetDeviceProcAddr(m_device, "vkGetFenceFdKHR"));
        if (!m_vkGetFenceFdKHR) {
            qWarning() << "Failed to load vkGetFenceFdKHR, disabling external fence support";
            m_supportsExternalFenceFd = false;
        }
    }

    qDebug() << "Vulkan logical device created successfully";

    // Store enabled extensions for later querying
    for (const char *ext : extensions) {
        m_extensions.append(ext);
    }

    return true;
}

void VulkanBackend::cleanup()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

#ifndef NDEBUG
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

std::pair<std::shared_ptr<VulkanTexture>, ColorDescription> VulkanBackend::textureForOutput(Output *output) const
{
    Q_UNUSED(output);
    return {nullptr, ColorDescription::sRGB};
}

void VulkanBackend::copyPixels(const QRegion &region, const QSize &screenSize)
{
    VulkanContext *context = vulkanContext();
    if (!context || !context->makeCurrent()) {
        return;
    }

    // Create command buffer for pixel copying
    VkCommandBuffer cmd = context->beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        return;
    }

    // Get the current framebuffer or swapchain image
    VulkanFramebuffer *framebuffer = context->currentFramebuffer();
    if (!framebuffer || !framebuffer->colorTexture()) {
        context->endSingleTimeCommands(cmd);
        return;
    }

    // Get the source image
    VkImage srcImage = framebuffer->colorTexture()->image();

    // Get the source image for the blit operation
    // This performs an intra-image copy matching OpenGL's glBlitFramebuffer behavior

    // Transition source image layout for transfer
    framebuffer->colorTexture()->transitionLayout(cmd,
                                                  framebuffer->colorTexture()->currentLayout(),
                                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Copy or blit image data based on region
    // Using vkCmdBlitImage for the copy operation, matching OpenGL's glBlitFramebuffer
    // This implementation performs an intra-image copy for buffer preservation
    // Note: For production use, a separate destination image is recommended to avoid
    // read/write hazards on the same image. This implementation assumes non-overlapping regions.
    for (const QRect &rect : region) {
        // Convert from Qt coordinates (top-left origin) to Vulkan/OpenGL coordinates (bottom-left origin)
        // OpenGL uses: y0 = height - y - height, y1 = height - y
        const int x0 = rect.x();
        const int x1 = rect.x() + rect.width();
        const int y0 = screenSize.height() - rect.y() - rect.height();
        const int y1 = screenSize.height() - rect.y();

        // Define source and destination regions for vkCmdBlitImage
        VkImageBlit blitRegion = {};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {x0, y0, 0};
        blitRegion.srcOffsets[1] = {x1, y1, 1};

        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = {x0, y0, 0};
        blitRegion.dstOffsets[1] = {x1, y1, 1};

        // Perform the blit operation with nearest neighbor filtering (matching GL_NEAREST)
        vkCmdBlitImage(cmd,
                       srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       1, &blitRegion,
                       VK_FILTER_NEAREST);
    }

    // Add memory barrier to ensure all blit operations are visible before the image is used again
    // This is important for intra-image copies to avoid read/write hazards
    VkImageMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    memoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // Keep same layout
    memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    memoryBarrier.image = srcImage;
    memoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    memoryBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    memoryBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &memoryBarrier);

    // Transition source image back to original layout
    framebuffer->colorTexture()->transitionLayout(cmd,
                                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                  framebuffer->colorTexture()->currentLayout(),
                                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    // End and submit command buffer
    context->endSingleTimeCommands(cmd);
}

} // namespace KWin

#include "moc_vulkanbackend.cpp"
