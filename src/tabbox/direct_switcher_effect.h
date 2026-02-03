/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "../effect/effect.h"
#include "../effect/offscreenquickview.h"

#include <chrono>
#include <memory>

namespace KWin
{

class DirectSwitcher;

/**
 * DirectSwitcherEffect wraps DirectSwitcher as a proper KWin Effect.
 *
 * This uses OffscreenQuickScene to render DirectSwitcher UI in an Effect context.
 * The effect owns both the scene and the switcher logic.
 *
 * This ensures:
 * - Proper render priority via Effect chain
 * - Frame scheduling via Effect lifecycle
 * - Correct pixel rendering in paintScreen()
 * - Input lifecycle with Alt press/release semantics
 */
class DirectSwitcherEffect : public Effect
{
    Q_OBJECT

public:
    DirectSwitcherEffect();
    ~DirectSwitcherEffect() override;

    // Effect lifecycle
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen) override;
    void postPaintScreen() override;

    bool isActive() const override;

    int requestedEffectChainPosition() const override;

    // Access to DirectSwitcher for input filter
    DirectSwitcher *switcher() const;

private:
    void setupScene();

    std::unique_ptr<OffscreenQuickScene> m_scene;
    std::unique_ptr<DirectSwitcher> m_switcher;
    bool m_needsRepaint = false;
};

} // namespace KWin
