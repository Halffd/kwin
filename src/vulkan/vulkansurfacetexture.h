/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "scene/surfaceitem.h"
#include <vulkan/vulkan.h>

namespace KWin
{

class VulkanBackend;
class VulkanTexture;

/**
 * @brief Texture wrapper for Vulkan surface textures
 */
class KWIN_EXPORT VulkanSurfaceContents
{
public:
    VulkanSurfaceContents()
    {
    }
    VulkanSurfaceContents(const std::shared_ptr<VulkanTexture> &contents)
        : planes({contents})
    {
    }
    VulkanSurfaceContents(const QList<std::shared_ptr<VulkanTexture>> &planeList)
        : planes(planeList)
    {
    }

    void reset()
    {
        planes.clear();
    }
    bool isValid() const
    {
        return !planes.isEmpty();
    }

    QList<std::shared_ptr<VulkanTexture>> planes;
};

class KWIN_EXPORT VulkanSurfaceTexture : public SurfaceTexture
{
public:
    explicit VulkanSurfaceTexture(VulkanBackend *backend);
    ~VulkanSurfaceTexture() override;

    bool isValid() const override;

    VulkanBackend *backend() const;
    VulkanSurfaceContents texture() const
    {
        return m_contents;
    }

protected:
    VulkanBackend *m_backend;
    VulkanSurfaceContents m_contents;
};

} // namespace KWin
