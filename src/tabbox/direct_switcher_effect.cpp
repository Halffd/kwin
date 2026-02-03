/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "direct_switcher_effect.h"
#include "direct_switcher.h"

#include "../effect/effecthandler.h"
#include "../workspace.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

namespace KWin
{

DirectSwitcherEffect::DirectSwitcherEffect()
    : Effect()
    , m_needsRepaint(false)
{
    // Create the OffscreenQuickScene for rendering DirectSwitcher UI
    m_scene = std::make_unique<OffscreenQuickScene>();

    // Create the DirectSwitcher logic instance
    m_switcher = std::make_unique<DirectSwitcher>();

    // Connect visibility changes to request repaints
    connect(m_switcher.get(), &DirectSwitcher::visibilityChanged,
            this, [this]() {
        m_needsRepaint = m_switcher->isVisible();
        if (m_switcher->isVisible()) {
            effects->addRepaintFull();
        }
    });

    // Set up the Quick scene (minimal setup - just ensure it has content)
    setupScene();
}

DirectSwitcherEffect::~DirectSwitcherEffect() = default;

void DirectSwitcherEffect::setupScene()
{
    if (!m_scene) {
        return;
    }

    // Create a root Quick item for the scene
    QQuickItem *rootItem = m_scene->rootItem();
    if (rootItem) {
        // Get the active output to determine positioning
        Output *output = workspace()->activeOutput();
        if (output) {
            const auto geometry = output->geometry();
            rootItem->setWidth(geometry.width());
            rootItem->setHeight(geometry.height());
            rootItem->setPosition(geometry.topLeft());
        }

        // TODO: Populate rootItem with Quick-based UI for the switcher
        // For now, just set up the container
    }
}

DirectSwitcher *DirectSwitcherEffect::switcher() const
{
    return m_switcher.get();
}

bool DirectSwitcherEffect::isActive() const
{
    return m_switcher && m_switcher->isVisible();
}

int DirectSwitcherEffect::requestedEffectChainPosition() const
{
    // Render after most effects to ensure it's on top
    return 99;
}

void DirectSwitcherEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    Q_UNUSED(presentTime);

    // Mark the screen as transformed if we're active
    if (isActive()) {
        data.mask |= PAINT_SCREEN_TRANSFORMED;
    }

    effects->prePaintScreen(data, presentTime);
}

void DirectSwitcherEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen)
{
    // Let other effects paint first
    effects->paintScreen(renderTarget, viewport, mask, region, screen);

    // If we're active, render the OffscreenQuickScene on top
    if (isActive() && m_scene) {
        effects->renderOffscreenQuickView(renderTarget, viewport, m_scene.get());
    }
}

void DirectSwitcherEffect::postPaintScreen()
{
    if (m_needsRepaint && isActive()) {
        effects->addRepaintFull();
    }

    effects->postPaintScreen();
}

} // namespace KWin

#include "direct_switcher_effect.moc"
