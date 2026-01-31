/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vulkansurfacetexture.h"
#include "vulkanbackend.h"
#include "vulkantexture.h"

namespace KWin
{

VulkanSurfaceTexture::VulkanSurfaceTexture(VulkanBackend *backend)
    : m_backend(backend)
{
}

VulkanSurfaceTexture::~VulkanSurfaceTexture()
{
    // Cleanup is handled by VulkanTexture destruction through m_contents
}

bool VulkanSurfaceTexture::isValid() const
{
    return m_contents.isValid();
}

VulkanBackend *VulkanSurfaceTexture::backend() const
{
    return m_backend;
}

} // namespace KWin
