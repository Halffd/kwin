/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "direct_switcher.h"
#include "../opengl/texture.h"
#include "../output.h"
#include "../scene/imageitem.h"
#include "../scene/item.h"
#include "../scene/surfaceitem.h"
#include "../window.h"
#include "../workspace.h"
#include "direct_window_list.h"

#include <QFontMetrics>
#include <QList>
#include <QPainter>
#include <QRect>

namespace KWin
{

class DirectSwitcher::Private
{
public:
    Private(DirectSwitcher *q);
    ~Private();

    void createVisualRepresentation();
    void destroyVisualRepresentation();
    void updateVisualRepresentation();
    void renderThumbnail(Window *window, const QRect &rect);
    void renderText(const QString &text, const QRect &rect, const QColor &color = Qt::white);

    DirectSwitcher *q;
    DirectWindowList windowList;
    QList<Window *> currentWindows;
    int currentIndex;
    Mode currentMode;
    bool visible;
    Output *output;
    QList<std::unique_ptr<SurfaceItem>> thumbnailItems;
    std::unique_ptr<SurfaceItem> selectedItem;
    std::unique_ptr<Item> rootItem;
};

DirectSwitcher::Private::Private(DirectSwitcher *q)
    : q(q)
    , currentIndex(-1)
    , currentMode(Mode::Windows)
    , visible(false)
    , output(nullptr)
{
}

DirectSwitcher::Private::~Private()
{
    destroyVisualRepresentation();
}

void DirectSwitcher::Private::createVisualRepresentation()
{
    if (rootItem) {
        return; // Already created
    }

    // Create a root item for the switcher UI
    rootItem = std::make_unique<Item>();

    // Get the current window list based on mode
    bool includeMinimized = true; // Simplified for this example
    bool currentDesktopOnly = (currentMode == Mode::Windows || currentMode == Mode::CurrentAppWindows);
    bool currentActivityOnly = true; // Simplified

    currentWindows = windowList.getWindowList(includeMinimized, currentDesktopOnly, currentActivityOnly);

    if (currentWindows.isEmpty()) {
        return;
    }

    // Initialize selection to the first window
    currentIndex = 0;

    // Calculate layout positions for thumbnails
    const QRect outputGeometry = output ? output->geometry() : QRect(0, 0, 1920, 1080); // fallback
    const int thumbnailWidth = outputGeometry.width() / 4;
    const int thumbnailHeight = outputGeometry.height() / 3;
    const int spacing = 20;

    int x = spacing;
    int y = outputGeometry.height() / 2 - thumbnailHeight / 2;

    // Create thumbnail items for each window
    for (int i = 0; i < currentWindows.size(); ++i) {
        Window *window = currentWindows[i];

        // Create a surface item to represent the window thumbnail
        auto thumbnailItem = std::make_unique<SurfaceItem>(rootItem.get());

        // Position the thumbnail
        thumbnailItem->setPos(QPointF(x, y));
        thumbnailItem->setSize(QSizeF(thumbnailWidth, thumbnailHeight));

        // For now, we'll just draw a placeholder - in a real implementation
        // we would capture the window content and scale it down
        renderThumbnail(window, QRect(x, y, thumbnailWidth, thumbnailHeight));

        thumbnailItems.push_back(std::move(thumbnailItem));

        // Move to next position
        x += thumbnailWidth + spacing;

        // Wrap to next row if needed
        if (x + thumbnailWidth > outputGeometry.width() - spacing) {
            x = spacing;
            y += thumbnailHeight + spacing;

            // Stop if we run out of vertical space
            if (y + thumbnailHeight > outputGeometry.height() - spacing) {
                break;
            }
        }
    }
}

void DirectSwitcher::Private::destroyVisualRepresentation()
{
    thumbnailItems.clear();
    selectedItem.reset();
    rootItem.reset();
    currentIndex = -1;
}

void DirectSwitcher::Private::updateVisualRepresentation()
{
    if (!rootItem || currentIndex < 0 || currentIndex >= currentWindows.size()) {
        return;
    }

    // Highlight the currently selected item
    // In a real implementation, we would adjust the appearance of the selected thumbnail
    // For now, we'll just ensure the selected window is visually distinct
}

void DirectSwitcher::Private::renderThumbnail(Window *window, const QRect &rect)
{
    // This is a placeholder implementation
    // In a real implementation, we would:
    // 1. Capture the window's content
    // 2. Scale it down to fit the rect
    // 3. Apply any visual effects (drop shadow, etc.)
    Q_UNUSED(window);
    Q_UNUSED(rect);
}

void DirectSwitcher::Private::renderText(const QString &text, const QRect &rect, const QColor &color)
{
    // This is a placeholder for rendering text
    // In a real implementation, we would render the window caption
    Q_UNUSED(text);
    Q_UNUSED(rect);
    Q_UNUSED(color);
}

DirectSwitcher::DirectSwitcher(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
}

DirectSwitcher::~DirectSwitcher() = default;

void DirectSwitcher::show(Mode mode)
{
    if (d->visible) {
        return; // Already visible
    }

    d->currentMode = mode;
    d->createVisualRepresentation();
    d->visible = true;

    // In a real implementation, we would add the root item to the compositor's scene
    // and ensure it's rendered on top of other windows

    Q_EMIT visibilityChanged(true);
    Q_EMIT selectionChanged(d->currentIndex >= 0 && d->currentIndex < d->currentWindows.size()
                                ? d->currentWindows[d->currentIndex]
                                : nullptr);
}

void DirectSwitcher::hide()
{
    if (!d->visible) {
        return;
    }

    d->destroyVisualRepresentation();
    d->visible = false;

    Q_EMIT visibilityChanged(false);
}

void DirectSwitcher::selectNext()
{
    if (d->currentWindows.isEmpty()) {
        return;
    }

    d->currentIndex = (d->currentIndex + 1) % d->currentWindows.size();
    d->updateVisualRepresentation();

    Q_EMIT selectionChanged(d->currentWindows[d->currentIndex]);
}

void DirectSwitcher::selectPrevious()
{
    if (d->currentWindows.isEmpty()) {
        return;
    }

    if (d->currentIndex <= 0) {
        d->currentIndex = d->currentWindows.size() - 1;
    } else {
        d->currentIndex--;
    }

    d->updateVisualRepresentation();

    Q_EMIT selectionChanged(d->currentWindows[d->currentIndex]);
}

void DirectSwitcher::accept()
{
    if (d->currentIndex >= 0 && d->currentIndex < d->currentWindows.size()) {
        Window *selectedWindow = d->currentWindows[d->currentIndex];
        if (selectedWindow) {
            Workspace::self()->activateWindow(selectedWindow);
        }
    }

    hide();
}

bool DirectSwitcher::isVisible() const
{
    return d->visible;
}

void DirectSwitcher::setOutput(Output *output)
{
    d->output = output;
}

Window *DirectSwitcher::currentSelection() const
{
    if (d->currentIndex >= 0 && d->currentIndex < d->currentWindows.size()) {
        return d->currentWindows[d->currentIndex];
    }
    return nullptr;
}

} // namespace KWin