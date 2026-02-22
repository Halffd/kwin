/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "direct_overview_effect.h"
#include "direct_overview.h"

#include "../effect/effecthandler.h"
#include "../workspace.h"

#include <QFile>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

namespace KWin
{

DirectOverviewEffect::DirectOverviewEffect()
    : Effect()
    , m_needsRepaint(false)
{
    // Create the OffscreenQuickScene for rendering DirectOverview UI
    m_scene = std::make_unique<OffscreenQuickScene>();

    // Create the DirectOverview logic instance
    m_overview = std::make_unique<DirectOverview>();

    // Connect visibility changes to request repaints
    connect(m_overview.get(), &DirectOverview::visibilityChanged,
            this, [this](bool visible) {
        m_needsRepaint = visible;
        if (visible) {
            effects->addRepaintFull();
        }
    });

    // Connect selection changes to update QML properties
    connect(m_overview.get(), &DirectOverview::selectionChanged,
            this, [this]() {
        updateQmlProperties();
    });

    // Set up the Quick scene with QML
    setupScene();
}

DirectOverviewEffect::~DirectOverviewEffect() = default;

void DirectOverviewEffect::setupScene()
{
    if (!m_scene) {
        return;
    }

    // Load the QML file
    const auto url = QUrl::fromLocalFile(
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               KWIN_DATADIR + QStringLiteral("/tabbox/directoverview/qml/main.qml")));

    if (!url.isValid() || url.isLocalFile() && !QFile::exists(url.toLocalFile())) {
        qWarning() << "DirectOverviewEffect: QML file not found at" << url;
        return;
    }

    m_scene->setSource(url, {{QStringLiteral("effect"), QVariant::fromValue(this)}});

    // Set up the root item geometry
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
    }

    // Initial property update
    updateQmlProperties();
}

void DirectOverviewEffect::updateQmlProperties()
{
    if (!m_overview || !m_scene || !m_scene->rootItem()) {
        return;
    }

    // Get actual desktop count and selection from overview
    const int newDesktopCount = m_overview->desktopCount();
    const int newSelectedIndex = m_overview->currentSelection();

    // Get desktop name from current selection
    QString newDesktopName = m_overview->desktopNameAt(newSelectedIndex);

    // Update cached values and emit signals if changed
    if (newDesktopCount != m_desktopCount) {
        m_desktopCount = newDesktopCount;
        Q_EMIT desktopCountChanged();
    }

    if (newSelectedIndex != m_selectedIndex) {
        m_selectedIndex = newSelectedIndex;
        Q_EMIT selectedIndexChanged();
    }

    if (newDesktopName != m_selectedDesktopName) {
        m_selectedDesktopName = newDesktopName;
        Q_EMIT selectedDesktopNameChanged();
    }

    // Push properties to QML
    QQuickItem *rootItem = m_scene->rootItem();
    if (rootItem) {
        rootItem->setProperty("desktopCount", m_desktopCount);
        rootItem->setProperty("selectedIndex", m_selectedIndex);
        rootItem->setProperty("selectedDesktopName", m_selectedDesktopName);
    }
}

DirectOverview *DirectOverviewEffect::overview() const
{
    return m_overview.get();
}

bool DirectOverviewEffect::isActive() const
{
    return m_overview && m_overview->isVisible();
}

int DirectOverviewEffect::requestedEffectChainPosition() const
{
    // Render after most effects to ensure it's on top
    return 99;
}

void DirectOverviewEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    Q_UNUSED(presentTime);

    // Mark the screen as transformed if we're active
    if (isActive()) {
        data.mask |= PAINT_SCREEN_TRANSFORMED;
    }

    effects->prePaintScreen(data, presentTime);
}

void DirectOverviewEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen)
{
    // Let other effects paint first
    effects->paintScreen(renderTarget, viewport, mask, region, screen);

    // If we're active, render the OffscreenQuickScene on top
    if (isActive() && m_scene) {
        effects->renderOffscreenQuickView(renderTarget, viewport, m_scene.get());
    }
}

void DirectOverviewEffect::postPaintScreen()
{
    if (m_needsRepaint && isActive()) {
        effects->addRepaintFull();
    }

    effects->postPaintScreen();
}

// Property implementations
int DirectOverviewEffect::desktopCount() const
{
    return m_desktopCount;
}

int DirectOverviewEffect::selectedIndex() const
{
    return m_selectedIndex;
}

QString DirectOverviewEffect::selectedDesktopName() const
{
    return m_selectedDesktopName;
}

QString DirectOverviewEffect::desktopNameAt(int index) const
{
    if (!m_overview || !m_overview->isVisible()) {
        return QString();
    }
    // Placeholder - would get actual desktop name from Workspace
    return QStringLiteral("Desktop %1").arg(index + 1);
}

} // namespace KWin

#include "direct_overview_effect.moc"
