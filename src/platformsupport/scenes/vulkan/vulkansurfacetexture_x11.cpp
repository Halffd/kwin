/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vulkansurfacetexture_x11.h"
#include "core/graphicsbuffer.h"
#include "effect/xcb.h"
#include "scene/surfaceitem_x11.h"
#include "utils/common.h"
#include "utils/filedescriptor.h"
#include "vulkanbackend.h"
#include "vulkanbuffer.h"
#include "vulkancontext.h"
#include "vulkanperformancetimer.h"
#include "vulkantexture.h"

#include <QImage>
#include <cstring>
#include <libdrm/drm_fourcc.h>
#include <unistd.h>
#include <xcb/dri3.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>

#include "scene/surfaceitem_x11.h"
#include "x11window.h"

namespace KWin
{

// Helper function to convert VkImageLayout to string for better logging
QString VulkanSurfaceTextureX11::layoutToString(VkImageLayout layout) const
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        return QStringLiteral("VK_IMAGE_LAYOUT_UNDEFINED (Initial, don't care about existing contents)");
    case VK_IMAGE_LAYOUT_GENERAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_GENERAL (General layout with no optimizations)");
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL (Optimal for color attachment)");
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL (Optimal for depth/stencil attachment)");
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL (Optimal for depth/stencil read-only)");
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (Optimal for shader reading)");
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL (Optimal as transfer source)");
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return QStringLiteral("VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (Optimal as transfer destination)");
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        return QStringLiteral("VK_IMAGE_LAYOUT_PREINITIALIZED (Preinitialized layout)");
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return QStringLiteral("VK_IMAGE_LAYOUT_PRESENT_SRC_KHR (Optimal for presentation)");
    default:
        return QStringLiteral("Unknown layout (0x%1)").arg(layout, 0, 16);
    }
}

// Helper to check if DRI3 extension is available
static bool isDri3Available(xcb_connection_t *c)
{
    static int dri3Available = -1;
    if (dri3Available >= 0) {
        return dri3Available == 1;
    }

    // Query DRI3 extension
    xcb_dri3_query_version_cookie_t cookie = xcb_dri3_query_version(c, 1, 0);
    xcb_dri3_query_version_reply_t *reply = xcb_dri3_query_version_reply(c, cookie, nullptr);

    if (reply) {
        qDebug() << "DRI3 extension available, version:" << reply->major_version << "." << reply->minor_version;
        // DRI3 1.0 is sufficient for xcb_dri3_fd_from_pixmap
        dri3Available = 1;
        free(reply);
    } else {
        qDebug() << "DRI3 extension not available";
        dri3Available = 0;
    }

    return dri3Available == 1;
}

// Convert pixmap depth to DRM format
static uint32_t depthToDrmFormat(uint8_t depth)
{
    switch (depth) {
    case 32:
        return DRM_FORMAT_ARGB8888;
    case 24:
        return DRM_FORMAT_XRGB8888;
    case 30:
        return DRM_FORMAT_XRGB2101010;
    case 16:
        return DRM_FORMAT_RGB565;
    default:
        qWarning() << "Unknown pixmap depth:" << depth;
        return 0;
    }
}

// Convert DRM format to Vulkan format
// Helper struct to store format information
struct FormatInfo
{
    VkFormat vkFormat;
    bool hasAlpha;
    bool isFloat;
    bool isCompressed;
    int bitsPerPixel;
    QString description;
    bool isABGR; // Flag to indicate if format is ABGR vs RGBA
};

// Helper function to get format information
static FormatInfo getFormatInfo(uint32_t drmFormat, VkFormat vkFormat)
{
    FormatInfo info;
    info.vkFormat = vkFormat;
    info.hasAlpha = false;
    info.isFloat = false;
    info.isCompressed = false;
    info.bitsPerPixel = 0;
    info.isABGR = false;

    // Set format-specific properties
    if (vkFormat == VK_FORMAT_B8G8R8A8_UNORM) {
        info.hasAlpha = true;
        info.bitsPerPixel = 32;
        info.description = QStringLiteral("8-bit per channel BGRA format (standard RGBA8 with swapped R and B)");
    } else if (vkFormat == VK_FORMAT_R8G8B8A8_UNORM) {
        info.hasAlpha = true;
        info.bitsPerPixel = 32;
        info.description = QStringLiteral("8-bit per channel RGBA format");
    } else if (vkFormat == VK_FORMAT_R8G8B8_UNORM || vkFormat == VK_FORMAT_B8G8R8_UNORM) {
        info.hasAlpha = false;
        info.bitsPerPixel = 24;
        info.description = QStringLiteral("8-bit per channel RGB format (no alpha)");
    } else if (vkFormat == VK_FORMAT_R5G6B5_UNORM_PACK16 || vkFormat == VK_FORMAT_B5G6R5_UNORM_PACK16) {
        info.hasAlpha = false;
        info.bitsPerPixel = 16;
        info.description = QStringLiteral("16-bit packed RGB format (5-6-5 bits per channel)");
    } else if (vkFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32 || vkFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
        info.hasAlpha = true;
        info.bitsPerPixel = 32;
        info.description = QStringLiteral("10-bit per RGB channel with 2-bit alpha (30-bit color)");
    } else if (vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT) {
        info.hasAlpha = true;
        info.isFloat = true;
        info.bitsPerPixel = 64;
        info.description = QStringLiteral("16-bit floating point per channel RGBA format (HDR)");
    }

    // Check if format is ABGR vs RGBA
    switch (drmFormat) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_ABGR16161616F:
        info.isABGR = true;
        break;
    default:
        info.isABGR = false;
        break;
    }

    return info;
}

// Get memory layout and channel swizzling description
static QString getMemoryLayoutDescription(uint32_t drmFormat)
{
    switch (drmFormat) {
    case DRM_FORMAT_ARGB8888:
        return QStringLiteral("Memory layout: ARGB (32-bit) = [A][R][G][B] = BGRA byte order (little-endian)\n"
                              "Channel swizzling: DRM ARGB → Vulkan BGRA (R and B swapped)");
    case DRM_FORMAT_XRGB8888:
        return QStringLiteral("Memory layout: XRGB (32-bit) = [X][R][G][B] = BGRX byte order (little-endian)\n"
                              "Channel swizzling: DRM XRGB → Vulkan BGRX (R and B swapped)\n"
                              "Alpha handling: X channel treated as opaque (alpha=1.0)");
    case DRM_FORMAT_ABGR8888:
        return QStringLiteral("Memory layout: ABGR (32-bit) = [A][B][G][R] = RGBA byte order (little-endian)\n"
                              "Channel swizzling: DRM ABGR → Vulkan RGBA (direct mapping)");
    case DRM_FORMAT_XBGR8888:
        return QStringLiteral("Memory layout: XBGR (32-bit) = [X][B][G][R] = RGBX byte order (little-endian)\n"
                              "Channel swizzling: DRM XBGR → Vulkan RGBX (direct mapping)\n"
                              "Alpha handling: X channel treated as opaque (alpha=1.0)");
    default:
        return QStringLiteral("Unknown memory layout for format: 0x") + QString::number(drmFormat, 16);
    }
}

// Get DRM format name as string
static QString drmFormatToString(uint32_t format)
{
    // DRM formats are defined as FourCC codes
    // Convert the uint32_t to a 4-character string
    char formatStr[5];
    formatStr[0] = (format >> 0) & 0xFF;
    formatStr[1] = (format >> 8) & 0xFF;
    formatStr[2] = (format >> 16) & 0xFF;
    formatStr[3] = (format >> 24) & 0xFF;
    formatStr[4] = '\0';

    // Map known DRM formats to their symbolic names
    QString formatName;
    switch (format) {
    case DRM_FORMAT_ARGB8888:
        formatName = "ARGB8888";
        break;
    case DRM_FORMAT_XRGB8888:
        formatName = "XRGB8888";
        break;
    case DRM_FORMAT_ABGR8888:
        formatName = "ABGR8888";
        break;
    case DRM_FORMAT_XBGR8888:
        formatName = "XBGR8888";
        break;
    case DRM_FORMAT_RGB888:
        formatName = "RGB888";
        break;
    case DRM_FORMAT_BGR888:
        formatName = "BGR888";
        break;
    case DRM_FORMAT_RGB565:
        formatName = "RGB565";
        break;
    case DRM_FORMAT_BGR565:
        formatName = "BGR565";
        break;
    case DRM_FORMAT_ARGB2101010:
        formatName = "ARGB2101010";
        break;
    case DRM_FORMAT_XRGB2101010:
        formatName = "XRGB2101010";
        break;
    case DRM_FORMAT_ABGR2101010:
        formatName = "ABGR2101010";
        break;
    case DRM_FORMAT_XBGR2101010:
        formatName = "XBGR2101010";
        break;
    case DRM_FORMAT_ABGR16161616F:
        formatName = "ABGR16161616F";
        break;
    default:
        formatName = QString(formatStr);
        break;
    }

    return QString("DRM_FORMAT_%1 (0x%2)").arg(formatName).arg(format, 0, 16);
}

static VkFormat drmFormatToVkFormat(uint32_t drmFormat)
{
    VkFormat result = VK_FORMAT_UNDEFINED;

    // Map DRM format to Vulkan format
    switch (drmFormat) {
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
        result = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
        result = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case DRM_FORMAT_RGB888:
        result = VK_FORMAT_R8G8B8_UNORM;
        break;
    case DRM_FORMAT_BGR888:
        result = VK_FORMAT_B8G8R8_UNORM;
        break;
    case DRM_FORMAT_RGB565:
        result = VK_FORMAT_R5G6B5_UNORM_PACK16;
        break;
    case DRM_FORMAT_BGR565:
        result = VK_FORMAT_B5G6R5_UNORM_PACK16;
        break;
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_XRGB2101010:
        result = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        break;
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
        result = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        break;
    case DRM_FORMAT_ABGR16161616F:
        result = VK_FORMAT_R16G16B16A16_SFLOAT;
        break;
    default:
        qWarning() << "Unknown DRM format:" << Qt::hex << drmFormat;
        break;
    }

    // Get format information
    FormatInfo info = getFormatInfo(drmFormat, result);
    QString memoryLayout = getMemoryLayoutDescription(drmFormat);

    // Log detailed format conversion information
    qDebug() << "DRM format conversion:";
    qDebug() << "  - DRM format:" << drmFormatToString(drmFormat);
    qDebug() << "  - Vulkan format:" << result;
    qDebug() << "  - Format properties:";
    qDebug() << "    *" << info.description;
    qDebug() << "    * Has alpha channel:" << info.hasAlpha;
    qDebug() << "    * Is floating point:" << info.isFloat;
    qDebug() << "    * Is compressed:" << info.isCompressed;
    qDebug() << "    * Bits per pixel:" << info.bitsPerPixel;
    qDebug() << "    * Is ABGR format (vs RGBA):" << info.isABGR;
    qDebug() << "  -" << memoryLayout;

    return result;
}

VulkanSurfaceTextureX11::VulkanSurfaceTextureX11(VulkanBackend *backend, SurfacePixmapX11 *pixmap)
    : VulkanSurfaceTexture(backend)
    , m_pixmap(pixmap)
    , m_context(backend->vulkanContext())
{
}

VulkanSurfaceTextureX11::~VulkanSurfaceTextureX11()
{
    // Clear base class handles BEFORE destroying m_texture to prevent double-destruction.
    // The base class destructor would otherwise try to destroy these already-freed handles.
    m_image = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;

    m_stagingBuffer.reset();
    m_texture.reset();
}

bool VulkanSurfaceTextureX11::create()
{
    if (!m_pixmap || !m_pixmap->isValid()) {
        qWarning() << "VulkanSurfaceTextureX11::create() - invalid pixmap";
        return false;
    }

    if (!m_context) {
        qWarning() << "VulkanSurfaceTextureX11::create() - no Vulkan context";
        return false;
    }

    m_size = m_pixmap->size();
    if (m_size.isEmpty()) {
        qWarning() << "VulkanSurfaceTextureX11::create() - empty size";
        return false;
    }

    // Log detailed information about the pixmap and its size
    qDebug() << "VulkanSurfaceTextureX11::create() - pixmap details:";
    qDebug() << "  - Pixmap ID:" << m_pixmap->pixmap();
    qDebug() << "  - Actual size:" << m_size;
    qDebug() << "  - Expected size (standard):" << QSize(1920, 1080);
    qDebug() << "  - Size ratio (width):" << (m_size.width() > 0 ? 1920.0 / m_size.width() : 0);
    qDebug() << "  - Size ratio (height):" << (m_size.height() > 0 ? 1080.0 / m_size.height() : 0);

    qWarning() << "VulkanSurfaceTextureX11::create() - attempting to create texture for pixmap of size" << m_size;

    // Try DMA-BUF import first (if available)
    if (m_context->supportsDmaBufImport()) {
        qInfo() << "[DMA-BUF] Import supported, attempting for pixmap size" << m_size;
        if (createWithDmaBuf()) {
            m_useDmaBuf = true;
            qInfo() << "[DMA-BUF] SUCCESS: Using zero-copy DMA-BUF import for pixmap";

            // Set window state information
            if (m_texture) {
                m_texture->setIsFromMaximizedWindow(isWindowMaximized());
                qDebug() << "VulkanSurfaceTextureX11::create() - Set window state: isMaximized =" << isWindowMaximized();
            }

            return true;
        } else {
            qWarning() << "[DMA-BUF] Import failed, falling back to CPU upload path";
        }
    } else {
        qInfo() << "[DMA-BUF] Import not supported by Vulkan implementation, using CPU upload";
    }

    // Fall back to CPU upload
    if (createWithCpuUpload()) {
        m_useDmaBuf = false;
        qWarning() << "VulkanSurfaceTextureX11::create() - SUCCESS: using CPU upload";

        // Set window state information
        if (m_texture) {
            m_texture->setIsFromMaximizedWindow(isWindowMaximized());
            qDebug() << "VulkanSurfaceTextureX11::create() - Set window state: isMaximized =" << isWindowMaximized();
        }

        return true;
    }

    qWarning() << "VulkanSurfaceTextureX11::create() - failed to create texture";
    return false;
}

bool VulkanSurfaceTextureX11::createWithDmaBuf()
{
    // Add performance timer to measure DMA-BUF import time
    PerformanceTimer timer(QStringLiteral("DMA-BUF import"), 1);

    qDebug() << "=== DMA-BUF IMPORT DIAGNOSTICS ===";
    qDebug() << "Starting DMA-BUF import process for pixmap";

    xcb_connection_t *c = connection();
    if (!c) {
        qDebug() << "createWithDmaBuf: no X11 connection";
        return false;
    }

    // Check if DRI3 is available (DRI3 1.0 is sufficient)
    if (!isDri3Available(c)) {
        qDebug() << "createWithDmaBuf: DRI3 not available";
        return false;
    }

    const xcb_pixmap_t nativePixmap = m_pixmap->pixmap();
    if (nativePixmap == XCB_PIXMAP_NONE) {
        qDebug() << "createWithDmaBuf: invalid pixmap";
        return false;
    }

    qDebug() << "createWithDmaBuf: attempting to import pixmap" << nativePixmap;
    qDebug() << "DRI3 version: 1.0 (using xcb_dri3_buffer_from_pixmap)";

    // Get pixmap info
    xcb_get_geometry_cookie_t geomCookie = xcb_get_geometry(c, nativePixmap);
    xcb_get_geometry_reply_t *geomReply = xcb_get_geometry_reply(c, geomCookie, nullptr);
    if (!geomReply) {
        qDebug() << "createWithDmaBuf: xcb_get_geometry failed";
        return false;
    }

    // Get the pixmap depth to determine format
    const uint8_t depth = geomReply->depth;
    const uint16_t pixmapWidth = geomReply->width;
    const uint16_t pixmapHeight = geomReply->height;

    qDebug() << "createWithDmaBuf: pixmap dimensions:" << pixmapWidth << "x" << pixmapHeight << "depth:" << depth;

    // Log detailed information about the pixmap geometry
    qDebug() << "Pixmap geometry details:";
    qDebug() << "  - Width:" << pixmapWidth;
    qDebug() << "  - Height:" << pixmapHeight;
    qDebug() << "  - Depth:" << depth;
    qDebug() << "  - Stored size:" << m_size;

    // Check for size mismatches
    if (pixmapWidth != m_size.width() || pixmapHeight != m_size.height()) {
        qWarning() << "  - POTENTIAL ISSUE: Geometry size doesn't match stored size";
        qWarning() << "    * Geometry: " << QSize(pixmapWidth, pixmapHeight);
        qWarning() << "    * Stored: " << m_size;
    }

    const uint32_t drmFormat = depthToDrmFormat(depth);
    if (drmFormat == 0) {
        qWarning() << "createWithDmaBuf: unsupported pixmap depth:" << depth;
        free(geomReply);
        return false;
    }

    free(geomReply);

    qDebug() << "createWithDmaBuf: DRM format is" << Qt::hex << drmFormat;

    // Log DRM format details
    char formatStr[5] = {0};
    formatStr[0] = (drmFormat >> 0) & 0xFF;
    formatStr[1] = (drmFormat >> 8) & 0xFF;
    formatStr[2] = (drmFormat >> 16) & 0xFF;
    formatStr[3] = (drmFormat >> 24) & 0xFF;
    qDebug() << "DRM format as string:" << formatStr;

    // Check if format is ABGR vs RGBA
    bool isABGR = false;
    switch (drmFormat) {
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_ABGR16161616F:
        isABGR = true;
        break;
    default:
        isABGR = false;
        break;
    }
    qDebug() << "Format is ABGR (vs RGBA):" << isABGR;

    // Convert DRM format to Vulkan format
    VkFormat vkFormat = drmFormatToVkFormat(drmFormat);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        qWarning() << "createWithDmaBuf: unsupported DRM format:" << Qt::hex << drmFormat;
        return false;
    }

    qDebug() << "createWithDmaBuf: Vulkan format is" << vkFormat;

    // Log Vulkan format details
    bool hasAlpha = (vkFormat == VK_FORMAT_B8G8R8A8_UNORM || vkFormat == VK_FORMAT_R8G8B8A8_UNORM || vkFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32 || vkFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT);
    qDebug() << "Vulkan format has alpha channel:" << hasAlpha;

    // Get the DMA-BUF file descriptor using DRI3 1.0 API
    qDebug() << "Requesting DMA-BUF file descriptor using DRI3 1.0 API (xcb_dri3_buffer_from_pixmap)";
    xcb_dri3_buffer_from_pixmap_cookie_t cookie = xcb_dri3_buffer_from_pixmap(c, nativePixmap);
    xcb_dri3_buffer_from_pixmap_reply_t *reply = xcb_dri3_buffer_from_pixmap_reply(c, cookie, nullptr);

    if (!reply) {
        qDebug() << "createWithDmaBuf: xcb_dri3_buffer_from_pixmap failed";
        return false;
    }

    // Get the file descriptor from the reply
    int fd = xcb_dri3_buffer_from_pixmap_reply_fds(c, reply)[0];
    uint32_t stride = reply->stride;
    uint32_t size = reply->size;
    uint8_t replyDepth = reply->depth;
    uint8_t bpp = reply->bpp;

    qDebug() << "DMA-BUF file descriptor details:";
    qDebug() << "  - File descriptor:" << fd;
    qDebug() << "  - Stride (bytes per row):" << stride;
    qDebug() << "  - Size (total bytes):" << size;
    qDebug() << "  - Depth (bits per pixel, excluding padding):" << replyDepth;
    qDebug() << "  - BPP (bits per pixel, including padding):" << bpp;

    // Validate file descriptor
    if (fd < 0) {
        qWarning() << "createWithDmaBuf: Invalid file descriptor received from DRI3";
        free(reply);
        return false;
    }

    // Check if stride makes sense for the width
    if (stride < (pixmapWidth * (bpp / 8))) {
        qWarning() << "createWithDmaBuf: Stride is too small for the pixmap width";
        qWarning() << "  - Expected minimum stride:" << (pixmapWidth * (bpp / 8));
        qWarning() << "  - Actual stride:" << stride;
    }

    // Check if size makes sense for the height and stride
    if (size < (stride * pixmapHeight)) {
        qWarning() << "createWithDmaBuf: Size is too small for the pixmap dimensions";
        qWarning() << "  - Expected minimum size:" << (stride * pixmapHeight);
        qWarning() << "  - Actual size:" << size;
    }

    // Build DmaBufAttributes
    qDebug() << "Building DmaBufAttributes for import";
    DmaBufAttributes dmaBufAttrs;
    dmaBufAttrs.planeCount = 1; // DRI3 1.0 only supports single-plane formats
    dmaBufAttrs.width = pixmapWidth;
    dmaBufAttrs.height = pixmapHeight;

    // Fix for texture corruption: Ensure we're using the correct DRM format
    // For ARGB8888 and XRGB8888, we need to explicitly handle them as BGRA in Vulkan
    if (drmFormat == DRM_FORMAT_ARGB8888 || drmFormat == DRM_FORMAT_XRGB8888) {
        // Force BGRA format for ARGB to ensure correct byte order
        dmaBufAttrs.format = DRM_FORMAT_BGRA8888;
        qDebug() << "Format conversion: Converting" << Qt::hex << drmFormat
                 << "to DRM_FORMAT_BGRA8888 for Vulkan compatibility";
    } else {
        dmaBufAttrs.format = drmFormat;
    }

    dmaBufAttrs.modifier = DRM_FORMAT_MOD_LINEAR; // Assume linear layout for DRI3 1.0

    // FileDescriptor takes ownership of the fd
    dmaBufAttrs.fd[0] = FileDescriptor(fd);
    dmaBufAttrs.offset[0] = 0;

    // Ensure stride is properly aligned for Vulkan
    // Many Vulkan implementations require 16-byte alignment
    const uint32_t alignment = 16;
    uint32_t alignedStride = (stride + alignment - 1) & ~(alignment - 1);
    if (alignedStride != stride) {
        qDebug() << "Aligning stride from" << stride << "to" << alignedStride << "for Vulkan compatibility";
    }
    dmaBufAttrs.pitch[0] = alignedStride;

    free(reply);

    // Import the DMA-BUF as a Vulkan texture
    qDebug() << "DMA-BUF attributes for Vulkan import:";
    qDebug() << "  - Width:" << dmaBufAttrs.width;
    qDebug() << "  - Height:" << dmaBufAttrs.height;
    qDebug() << "  - Format:" << Qt::hex << dmaBufAttrs.format;
    qDebug() << "  - Modifier:" << Qt::hex << dmaBufAttrs.modifier;
    qDebug() << "  - Modifier name:" << (dmaBufAttrs.modifier == DRM_FORMAT_MOD_LINEAR ? "DRM_FORMAT_MOD_LINEAR" : "Unknown");
    qDebug() << "  - Plane count:" << dmaBufAttrs.planeCount;
    qDebug() << "  - Stride:" << dmaBufAttrs.pitch[0];
    qDebug() << "  - Offset:" << dmaBufAttrs.offset[0];

    // We already aligned the stride above, so this check is redundant

    qDebug() << "Calling VulkanContext::importDmaBufAsTexture...";
    m_texture = m_context->importDmaBufAsTexture(dmaBufAttrs);

    // Fix for texture corruption: Ensure proper layout transition
    // After import, the texture layout might not be optimal for sampling
    if (m_texture && m_texture->isValid() && m_texture->currentLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        qDebug() << "Performing layout transition to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL";
        qDebug() << "Current layout:" << layoutToString(m_texture->currentLayout());

        // Get a command buffer for the transition
        VkCommandBuffer cmdBuffer = m_context->beginSingleTimeCommands();
        if (cmdBuffer) {
            // Transition to optimal sampling layout
            m_texture->transitionLayout(
                cmdBuffer,
                m_texture->currentLayout(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            // Submit the command buffer
            m_context->endSingleTimeCommands(cmdBuffer);

            // Update the current layout
            m_texture->setCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            qDebug() << "Layout transition completed. New layout:"
                     << layoutToString(m_texture->currentLayout());
        }
    }
    if (!m_texture) {
        qWarning() << "CRITICAL ERROR: Failed to import DMA-BUF as Vulkan texture (null texture)";
        qWarning() << "This could be due to:";
        qWarning() << "  - Unsupported format/modifier combination";
        qWarning() << "  - Invalid file descriptor";
        qWarning() << "  - Memory allocation failure";
        qWarning() << "  - Missing Vulkan extensions";
        return false;
    }
    if (!m_texture->isValid()) {
        qWarning() << "CRITICAL ERROR: Failed to import DMA-BUF as Vulkan texture (invalid texture)";
        qWarning() << "This could be due to:";
        qWarning() << "  - Image creation failure";
        qWarning() << "  - Memory binding failure";
        qWarning() << "  - Image view creation failure";
        return false;
    }

    // Log detailed information about the imported texture
    qDebug() << "SUCCESS: DMA-BUF successfully imported as Vulkan texture";
    qDebug() << "Texture details:";
    qDebug() << "  - Texture size:" << m_texture->size();
    qDebug() << "  - Requested size:" << QSize(dmaBufAttrs.width, dmaBufAttrs.height);
    qDebug() << "  - Pixmap size:" << m_size;
    qDebug() << "  - Vulkan format:" << m_texture->format();
    qDebug() << "  - Current layout:" << layoutToString(m_texture->currentLayout());
    qDebug() << "  - Image handle:" << m_texture->image();
    qDebug() << "  - Image view handle:" << m_texture->imageView();

    // Check for size mismatches
    if (m_texture->size() != QSize(dmaBufAttrs.width, dmaBufAttrs.height)) {
        qWarning() << "  - POTENTIAL ISSUE: Texture size doesn't match requested size";
        qWarning() << "    * Expected:" << QSize(dmaBufAttrs.width, dmaBufAttrs.height);
        qWarning() << "    * Actual:" << m_texture->size();
        qWarning() << "    * This could cause rendering artifacts or incorrect scaling";
    }
    if (m_texture->size() != m_size) {
        qWarning() << "  - POTENTIAL ISSUE: Texture size doesn't match pixmap size";
        qWarning() << "    * Expected:" << m_size;
        qWarning() << "    * Actual:" << m_texture->size();
        qWarning() << "    * This could cause rendering artifacts or incorrect scaling";

        // Check for 1/4 scale issue (0.5x in both dimensions)
        float widthRatio = static_cast<float>(m_texture->size().width()) / m_size.width();
        float heightRatio = static_cast<float>(m_texture->size().height()) / m_size.height();
        if (qAbs(widthRatio - 0.5) < 0.01 && qAbs(heightRatio - 0.5) < 0.01) {
            qWarning() << "    * DETECTED: 1/4 scale issue (0.5x in both X and Y dimensions)";
            qWarning() << "    * This matches the known opacity mask scaling bug";
        }
    }

    // Set the VkImage/VkImageView on the base class for compatibility
    m_image = m_texture->image();
    m_imageView = m_texture->imageView();

    // Fix for texture corruption: Set proper content transform
    // The GLX backend sets contentTransform based on y_inverted flag,
    // but the Vulkan backend does not. Let's explicitly set it to identity
    // to ensure consistent behavior.
    m_texture->setContentTransform(OutputTransform::Normal);
    qDebug() << "Setting content transform to OutputTransform::Normal (identity)";

    // Log detailed information about the imported texture
    qDebug() << "DMA-BUF texture memory details:";
    qDebug() << "  - Memory type: External DMA-BUF";
    qDebug() << "  - Memory access: Zero-copy (GPU direct access)";
    qDebug() << "  - Memory synchronization: Handled by Vulkan driver";
    qDebug() << "  - Memory layout: Linear (DRM_FORMAT_MOD_LINEAR)";

    // Log information about the coordinate systems
    qDebug() << "Coordinate system analysis:";
    qDebug() << "  - X11 origin: top-left";
    qDebug() << "  - Vulkan texture origin: top-left";
    qDebug() << "  - Vulkan framebuffer origin: top-left (with negative viewport height)";
    qDebug() << "  - No Y-flip applied (Vulkan backend)";
    qDebug() << "  - Potential issue: Coordinate system mismatch between X11 and Vulkan";

    // Log texture sampling parameters
    qDebug() << "Texture sampling parameters:";
    qDebug() << "  - Filter mode:" << (m_texture->filter() == VK_FILTER_LINEAR ? "Linear" : "Nearest");
    qDebug() << "  - Address mode:" << (m_texture->wrapMode() == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ? "Clamp to edge" : "Repeat/Other");
    qDebug() << "  - Normalized coordinates:" << "Yes (UV in [0,1] range)";
    qDebug() << "  - Mipmap mode:" << "Linear (default)";
    qDebug() << "  - Anisotropy:" << "Disabled (default)";

    // Log information about potential texture transformation issues
    qDebug() << "Texture transformation analysis:";
    qDebug() << "  - Opacity mask scaling: Check for 1/4 scale issue (0.5x in both X and Y dimensions)";
    qDebug() << "  - Content transform: Not set for X11 surfaces in Vulkan backend (by design)";
    qDebug() << "  - Texture matrix: Checking for non-identity transformations";

    // Log detailed texture coordinate transformation information
    qDebug() << "Texture matrix analysis:";
    qDebug() << "  - Normalized coordinates: UV in [0,1] range";

    QMatrix4x4 normalizedMatrix = m_texture->matrix(VulkanCoordinateType::Normalized);
    qDebug() << "  - Texture matrix (normalized):";
    qDebug() << "    * [0,0]:" << normalizedMatrix(0, 0) << "(U scale)";
    qDebug() << "    * [1,1]:" << normalizedMatrix(1, 1) << "(V scale)";
    qDebug() << "    * [0,3]:" << normalizedMatrix(0, 3) << "(U translation)";
    qDebug() << "    * [1,3]:" << normalizedMatrix(1, 3) << "(V translation)";

    // Check if matrix is identity
    bool isIdentity = (qAbs(normalizedMatrix(0, 0) - 1.0) < 0.01 && qAbs(normalizedMatrix(1, 1) - 1.0) < 0.01 && qAbs(normalizedMatrix(0, 3)) < 0.01 && qAbs(normalizedMatrix(1, 3)) < 0.01);

    qDebug() << "  - Matrix is identity:" << isIdentity;

    if (!isIdentity) {
        qDebug() << "  - Non-identity matrix detected, analyzing transformations:";

        // Check for scaling
        if (qAbs(normalizedMatrix(0, 0) - 1.0) >= 0.01 || qAbs(normalizedMatrix(1, 1) - 1.0) >= 0.01) {
            qDebug() << "    * Scaling detected:";
            qDebug() << "      - X scale:" << normalizedMatrix(0, 0);
            qDebug() << "      - Y scale:" << normalizedMatrix(1, 1);
        }

        // Check for translation
        if (qAbs(normalizedMatrix(0, 3)) >= 0.01 || qAbs(normalizedMatrix(1, 3)) >= 0.01) {
            qDebug() << "    * Translation detected:";
            qDebug() << "      - X translation:" << normalizedMatrix(0, 3);
            qDebug() << "      - Y translation:" << normalizedMatrix(1, 3);
        }
    }

    // Check for scaling issues that might indicate opacity mask problems
    if (qAbs(normalizedMatrix(0, 0) - 0.5) < 0.01 && qAbs(normalizedMatrix(1, 1) - 0.5) < 0.01) {
        qWarning() << "  - DETECTED 1/4 scale issue: Matrix shows 0.5x scaling in both dimensions";
        qWarning() << "    * This matches the known opacity mask scaling bug";
        qWarning() << "    * The opacity mask is incorrectly scaled to 1/4 size (0.5x in both X and Y dimensions)";
        qWarning() << "    * This is a scaling transformation issue, not a Y-flip issue";
    }

    // Summary of the DMA-BUF import process
    qDebug() << "=== DMA-BUF IMPORT SUMMARY ===";
    qDebug() << "Successfully imported" << pixmapWidth << "x" << pixmapHeight
             << "pixmap with" << dmaBufAttrs.planeCount << "plane(s)";
    qDebug() << "DRM format:" << Qt::hex << drmFormat << "(Vulkan format:" << m_texture->format() << ")";
    qDebug() << "DRM modifier:" << Qt::hex << dmaBufAttrs.modifier << "(Linear layout)";
    qDebug() << "Texture size:" << m_texture->size() << "(Pixmap size:" << m_size << ")";
    qDebug() << "Current layout:" << layoutToString(m_texture->currentLayout());
    qDebug() << "=== END OF DMA-BUF IMPORT DIAGNOSTICS ===";

    return true;
}

bool VulkanSurfaceTextureX11::createWithCpuUpload()
{
    // Add performance timer to measure CPU upload time
    PerformanceTimer timer(QStringLiteral("CPU upload"), 1);

    qDebug() << "VulkanSurfaceTextureX11::createWithCpuUpload() - creating texture with size" << m_size;

    // Log detailed information about the CPU upload path
    qDebug() << "CPU upload texture creation details:";
    qDebug() << "  - Pixmap size:" << m_size;
    qDebug() << "  - Pixmap ID:" << m_pixmap->pixmap();
    qDebug() << "  - Memory allocation: Host-visible staging buffer + device-local texture";

    // Create a Vulkan texture with the appropriate format
    // X11 data is sRGB encoded, so we use SRGB format for correct sampling
    // This makes the hardware do sRGB-to-linear conversion on texture fetch
    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
    qDebug() << "  - Texture format:" << format << "(VK_FORMAT_B8G8R8A8_SRGB)";

    m_texture = VulkanTexture::allocate(m_context, m_size, format);
    if (!m_texture) {
        qWarning() << "VulkanSurfaceTextureX11::createWithCpuUpload() - failed to allocate texture (null texture)";
        return false;
    }
    if (!m_texture->isValid()) {
        qWarning() << "VulkanSurfaceTextureX11::createWithCpuUpload() - failed to allocate texture (invalid texture)";
        return false;
    }

    qDebug() << "VulkanSurfaceTextureX11::createWithCpuUpload() - texture allocated successfully";

    // Create staging buffer for CPU → GPU transfers
    const VkDeviceSize bufferSize = m_size.width() * m_size.height() * 4; // BGRA = 4 bytes per pixel
    m_stagingBuffer = VulkanBuffer::createStagingBuffer(m_context, bufferSize);

    if (!m_stagingBuffer) {
        qWarning() << "VulkanSurfaceTextureX11::createWithCpuUpload() - failed to create staging buffer (null buffer)";
        m_texture.reset();
        return false;
    }
    if (!m_stagingBuffer->isValid()) {
        qWarning() << "VulkanSurfaceTextureX11::createWithCpuUpload() - failed to create staging buffer (invalid buffer)";
        m_texture.reset();
        return false;
    }

    qDebug() << "VulkanSurfaceTextureX11::createWithCpuUpload() - staging buffer created successfully";

    // Do initial upload
    updateWithCpuUpload(QRegion(QRect(QPoint(0, 0), m_size)));

    // Set the VkImage/VkImageView on the base class for compatibility
    m_image = m_texture->image();
    m_imageView = m_texture->imageView();

    // Note: We don't apply FlipY here because the viewport already has Y-flip via negative height.
    // The texture data from X11 has Y=0 at top, which matches Vulkan's image coordinate system.

    return true;
}

void VulkanSurfaceTextureX11::update(const QRegion &region)
{
    if (!m_pixmap || !m_pixmap->isValid()) {
        qDebug() << "VulkanSurfaceTextureX11::update - invalid pixmap";
        return;
    }

    qDebug() << "VulkanSurfaceTextureX11::update - region:" << region.boundingRect()
             << "using DMA-BUF:" << m_useDmaBuf
             << "texture size:" << m_size
             << "pixmap size:" << m_pixmap->size();

    if (m_useDmaBuf) {
        // For DMA-BUF, we just need to invalidate caches
        // The GPU shares memory with the X server
        // A memory barrier might be needed in the rendering code

        // Add performance timer for DMA-BUF update (should be very fast)
        PerformanceTimer timer(QStringLiteral("DMA-BUF update"), 0);

        qDebug() << "VulkanSurfaceTextureX11::update - DMA-BUF path - zero-copy update";
        qDebug() << "  - Update region:" << region.boundingRect();
        qDebug() << "  - Region count:" << region.rectCount();

        // Log detailed information about the DMA-BUF texture
        if (m_texture) {
            qDebug() << "  - Texture valid:" << m_texture->isValid();
            qDebug() << "  - Current layout:" << m_texture->currentLayout();
            qDebug() << "  - Image:" << m_texture->image();
            qDebug() << "  - Image view:" << m_texture->imageView();
            qDebug() << "  - Texture size:" << m_texture->size();
            qDebug() << "  - Pixmap size:" << m_size;

            // Check for size mismatches
            if (m_texture->size() != m_size) {
                qWarning() << "  - POTENTIAL ISSUE: Texture size doesn't match pixmap size";
                qWarning() << "    * Expected size:" << m_size;
                qWarning() << "    * Actual size:" << m_texture->size();
                qWarning() << "    * Size ratio (width):" << (m_texture->size().width() > 0 ? static_cast<float>(m_size.width()) / m_texture->size().width() : 0);
                qWarning() << "    * Size ratio (height):" << (m_texture->size().height() > 0 ? static_cast<float>(m_size.height()) / m_texture->size().height() : 0);
            }

            // Log layout transition information
            qDebug() << "  - Layout transition info:";
            qDebug() << "    * Current layout:" << layoutToString(m_texture->currentLayout());
            qDebug() << "    * Optimal layout for sampling:" << layoutToString(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            qDebug() << "    * Optimal layout for DMA-BUF:" << layoutToString(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            // Log memory synchronization information
            qDebug() << "  - Memory synchronization:";
            qDebug() << "    * DMA-BUF uses zero-copy, but may need memory barriers";
            qDebug() << "    * External memory type:" << (m_texture->ownsImage() ? "Owned by texture" : "External memory");
        }

        return;
    }

    // CPU upload path
    updateWithCpuUpload(region);
}

void VulkanSurfaceTextureX11::updateWithCpuUpload(const QRegion &region)
{
    // Add performance timer for CPU upload path
    PerformanceTimer timer(QStringLiteral("CPU upload update"), 1);

    if (!m_texture || !m_stagingBuffer) {
        qDebug() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - texture or staging buffer not available";
        return;
    }

    const xcb_pixmap_t nativePixmap = m_pixmap->pixmap();
    if (nativePixmap == XCB_PIXMAP_NONE) {
        qDebug() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - invalid pixmap";
        return;
    }

    qDebug() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - updating region" << region.boundingRect();

    // Use the global X11 connection
    xcb_connection_t *c = connection();
    if (!c) {
        qWarning() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - no X11 connection";
        return;
    }

    // For simplicity, we update the entire texture when any region changes
    // A more optimized implementation would only update the damaged regions
    const QRect bounds = region.boundingRect();
    const int x = bounds.x();
    const int y = bounds.y();
    const int width = bounds.width();
    const int height = bounds.height();

    // Get the image data from X11
    xcb_get_image_cookie_t cookie = xcb_get_image(
        c,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        nativePixmap,
        x, y,
        width, height,
        ~0);

    xcb_get_image_reply_t *reply = xcb_get_image_reply(c, cookie, nullptr);
    if (!reply) {
        qWarning() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - xcb_get_image failed";
        return;
    }

    const uint8_t *data = xcb_get_image_data(reply);
    const int dataLength = xcb_get_image_data_length(reply);
    const uint8_t depth = reply->depth;
    const int bytesPerPixel = (depth == 24) ? 4 : 4; // X11 pads 24-bit to 32-bit

    // Log detailed information about the image data received from X11
    qWarning() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - xcb_get_image details:";
    qWarning() << "  - Depth:" << depth;
    qWarning() << "  - Data length:" << dataLength;
    qWarning() << "  - Expected data length:" << (width * height * bytesPerPixel);
    qWarning() << "  - Region size:" << width << "x" << height;
    qWarning() << "  - Texture size:" << m_size.width() << "x" << m_size.height();
    qWarning() << "  - Bytes per pixel:" << bytesPerPixel;

    // Check for potential sizing issues
    if (dataLength != width * height * bytesPerPixel) {
        qWarning() << "  - POTENTIAL ISSUE: Data length mismatch";
    }

    // Copy data to staging buffer
    void *mappedData = m_stagingBuffer->map();
    if (!mappedData) {
        qWarning() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - failed to map staging buffer";
        free(reply);
        return;
    }

    qDebug() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - staging buffer mapped successfully";

    if (mappedData) {
        // For depth 24 (XRGB), the X byte (padding/alpha) may be 0 or garbage.
        // We need to set it to 0xFF for proper alpha blending.
        const bool needsAlphaFix = (depth == 24);

        if (needsAlphaFix) {
            // Copy pixel-by-pixel, fixing alpha channel
            // X11 XRGB is stored as BGRX in memory (little-endian), byte 3 is the X/alpha
            uint8_t *dst = static_cast<uint8_t *>(mappedData);
            const int dstStride = m_size.width() * 4;

            if (x == 0 && y == 0 && width == m_size.width() && height == m_size.height()) {
                // Full update
                const int pixelCount = width * height;
                for (int i = 0; i < pixelCount; ++i) {
                    dst[i * 4 + 0] = data[i * 4 + 0]; // B
                    dst[i * 4 + 1] = data[i * 4 + 1]; // G
                    dst[i * 4 + 2] = data[i * 4 + 2]; // R
                    dst[i * 4 + 3] = 0xFF; // A = opaque
                }
            } else {
                // Partial update
                const int srcStride = width * 4;
                dst += (y * dstStride) + (x * 4);

                for (int row = 0; row < height; ++row) {
                    for (int col = 0; col < width; ++col) {
                        const int srcIdx = row * srcStride + col * 4;
                        const int dstIdx = row * dstStride + col * 4;
                        dst[dstIdx + 0] = data[srcIdx + 0]; // B
                        dst[dstIdx + 1] = data[srcIdx + 1]; // G
                        dst[dstIdx + 2] = data[srcIdx + 2]; // R
                        dst[dstIdx + 3] = 0xFF; // A = opaque
                    }
                }
            }
        } else {
            // Depth 32 (ARGB) - copy with forced alpha=0xFF
            // Many X11 apps don't properly set alpha even on 32-bit visuals
            uint8_t *dst = static_cast<uint8_t *>(mappedData);
            const int dstStride = m_size.width() * 4;

            if (x == 0 && y == 0 && width == m_size.width() && height == m_size.height()) {
                // Full update
                const int pixelCount = width * height;
                for (int i = 0; i < pixelCount; ++i) {
                    dst[i * 4 + 0] = data[i * 4 + 0]; // B
                    dst[i * 4 + 1] = data[i * 4 + 1]; // G
                    dst[i * 4 + 2] = data[i * 4 + 2]; // R
                    dst[i * 4 + 3] = 0xFF; // A = opaque
                }
            } else {
                // Partial update
                const int srcStride = width * 4;
                dst += (y * dstStride) + (x * 4);

                for (int row = 0; row < height; ++row) {
                    for (int col = 0; col < width; ++col) {
                        const int srcIdx = row * srcStride + col * 4;
                        const int dstIdx = row * dstStride + col * 4;
                        dst[dstIdx + 0] = data[srcIdx + 0]; // B
                        dst[dstIdx + 1] = data[srcIdx + 1]; // G
                        dst[dstIdx + 2] = data[srcIdx + 2]; // R
                        dst[dstIdx + 3] = 0xFF; // A = opaque
                    }
                }
            }
        }
        m_stagingBuffer->unmap();
        // Flush the staging buffer to ensure data is visible to GPU
        // VMA may allocate non-coherent memory, so explicit flush is required
        m_stagingBuffer->flush(0, m_size.width() * m_size.height() * 4);
    }

    free(reply);

    // Copy from staging buffer to texture using a command buffer
    VkCommandBuffer cmd = m_context->beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        qWarning() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - failed to begin command buffer";
        return;
    }

    qDebug() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - command buffer allocated successfully";

    // Transition image to transfer destination layout
    m_texture->transitionLayout(cmd,
                                m_texture->currentLayout(),
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Copy buffer to image
    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    // bufferRowLength specifies the row stride in PIXELS (not bytes)
    // We always write with full texture stride, so set this to full width
    copyRegion.bufferRowLength = static_cast<uint32_t>(m_size.width());
    copyRegion.bufferImageHeight = static_cast<uint32_t>(m_size.height());
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;

    // Log detailed information about the buffer-to-image copy operation
    qDebug() << "Buffer-to-image copy details:";
    qDebug() << "  - Buffer row length (pixels):" << copyRegion.bufferRowLength;
    qDebug() << "  - Buffer image height:" << copyRegion.bufferImageHeight;
    qDebug() << "  - Update region:" << QRect(x, y, width, height);
    qDebug() << "  - Texture size:" << m_size;

    if (x == 0 && y == 0 && width == m_size.width() && height == m_size.height()) {
        // Full copy
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {static_cast<uint32_t>(m_size.width()),
                                  static_cast<uint32_t>(m_size.height()), 1};
        qDebug() << "  - Copy type: Full texture update";
    } else {
        // Partial copy - buffer data is at the same offset we wrote to in staging buffer
        // The staging buffer has full texture stride, which we specified in bufferRowLength
        copyRegion.bufferOffset = (y * m_size.width() + x) * 4;
        copyRegion.imageOffset = {x, y, 0};
        copyRegion.imageExtent = {static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height), 1};
        qDebug() << "  - Copy type: Partial texture update";
        qDebug() << "  - Buffer offset:" << copyRegion.bufferOffset;
        qDebug() << "  - Image offset:" << copyRegion.imageOffset.x << "," << copyRegion.imageOffset.y;
        qDebug() << "  - Image extent:" << copyRegion.imageExtent.width << "x" << copyRegion.imageExtent.height;
    }

    vkCmdCopyBufferToImage(cmd,
                           m_stagingBuffer->buffer(),
                           m_texture->image(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

    // Transition image to shader read layout
    m_texture->transitionLayout(cmd,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    qDebug() << "VulkanSurfaceTextureX11::updateWithCpuUpload() - image layout transition recorded";

    m_context->endSingleTimeCommands(cmd);
}

KWin::X11Window *VulkanSurfaceTextureX11::parentWindow() const
{
    if (!m_pixmap) {
        return nullptr;
    }

    // Get the SurfaceItemX11 from the SurfacePixmapX11
    SurfaceItem *item = m_pixmap->item();
    if (!item) {
        return nullptr;
    }

    // Cast to SurfaceItemX11
    SurfaceItemX11 *x11Item = qobject_cast<SurfaceItemX11 *>(item);
    if (!x11Item) {
        return nullptr;
    }

    // Get the X11Window
    return x11Item->window();
}

bool VulkanSurfaceTextureX11::isWindowMaximized() const
{
    KWin::X11Window *window = parentWindow();
    if (!window) {
        return false;
    }

    // Check if the window is maximized (either horizontally, vertically, or both)
    return window->maximizeMode() != MaximizeRestore;
}

} // namespace KWin
