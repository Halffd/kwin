/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "vulkansurfacetexture.h"
#include "vulkantexture.h"

#include <QSize>
#include <memory>

namespace KWin
{
class X11Window;
}

namespace KWin
{

class SurfacePixmapX11;
class VulkanContext;
class VulkanBuffer;

/**
 * @brief X11-specific Vulkan surface texture implementation
 *
 * This class imports X11 pixmaps as Vulkan textures. It supports two methods:
 * 1. DMA-BUF import (efficient, requires DRI3 support)
 * 2. CPU fallback via XGetImage (slower, always available)
 */
class KWIN_EXPORT VulkanSurfaceTextureX11 : public VulkanSurfaceTexture
{
public:
    VulkanSurfaceTextureX11(VulkanBackend *backend, SurfacePixmapX11 *pixmap);
    ~VulkanSurfaceTextureX11() override;

    bool create() override;
    void update(const QRegion &region) override;

    /**
     * @brief Get the Vulkan texture for rendering.
     */
    VulkanTexture *texture() const
    {
        return m_texture.get();
    }

private:
    /**
     * @brief Get the parent X11Window for this surface texture.
     *
     * This method traverses the item hierarchy to find the parent X11Window.
     * @return X11Window* Pointer to the parent window, or nullptr if not found.
     */
    KWin::X11Window *parentWindow() const;

    /**
     * @brief Check if the parent window is maximized.
     *
     * @return bool True if the window is maximized (either horizontally, vertically, or both).
     */
    bool isWindowMaximized() const;

    bool createWithDmaBuf();
    bool createWithCpuUpload();
    void updateWithCpuUpload(const QRegion &region);

    // Helper function to convert VkImageLayout to string for better logging
    QString layoutToString(VkImageLayout layout) const;

    /**
     * @brief Detect Y-inversion state from X11 surface characteristics.
     *
     * This function mirrors the GLX backend's use of GLX_Y_INVERTED_EXT.
     * It determines whether the X11 surface requires Y-flipping for proper
     * Vulkan rendering by checking visual type and depth information.
     *
     * @return bool True if Y-flipping is needed, false otherwise.
     */
    bool detectYInversionFromX11Surface() const;

    SurfacePixmapX11 *m_pixmap;
    VulkanContext *m_context;
    std::unique_ptr<VulkanTexture> m_texture;
    std::unique_ptr<VulkanBuffer> m_stagingBuffer;
    QSize m_size;
    bool m_useDmaBuf = false;
};

} // namespace KWin
