/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "../effect/effect.h"
#include "../effect/offscreenquickview.h"

#include <QList>
#include <QQuickItem>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <chrono>
#include <memory>

namespace KWin
{

class DirectOverview;

/**
 * DirectOverviewEffect wraps DirectOverview as a proper KWin Effect.
 *
 * This uses OffscreenQuickScene to render DirectOverview UI in an Effect context.
 * The effect owns both the scene and the overview logic.
 *
 * This ensures:
 * - Proper render priority via Effect chain
 * - Frame scheduling via Effect lifecycle
 * - Correct pixel rendering in paintScreen()
 */
class DirectOverviewEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int desktopCount READ desktopCount NOTIFY desktopCountChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString selectedDesktopName READ selectedDesktopName NOTIFY selectedDesktopNameChanged)

public:
    DirectOverviewEffect();
    ~DirectOverviewEffect() override;

    // Effect lifecycle
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen) override;
    void postPaintScreen() override;

    bool isActive() const override;

    int requestedEffectChainPosition() const override;

    // Access to DirectOverview for input filter
    DirectOverview *overview() const;

    // Properties exposed to QML
    int desktopCount() const;
    int selectedIndex() const;
    QString selectedDesktopName() const;

    // Method to get desktop name at index for QML
    Q_INVOKABLE QString desktopNameAt(int index) const;

Q_SIGNALS:
    void desktopCountChanged();
    void selectedIndexChanged();
    void selectedDesktopNameChanged();

private:
    void setupScene();
    void updateQmlProperties();

    std::unique_ptr<OffscreenQuickScene> m_scene;
    std::unique_ptr<DirectOverview> m_overview;
    bool m_needsRepaint = false;
    int m_desktopCount = 0;
    int m_selectedIndex = 0;
    QString m_selectedDesktopName;
};

} // namespace KWin
