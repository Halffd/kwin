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

    // Configuration parameters
    int m_thumbnailWidth = 600; // Default to 600px as previously specified
    int m_padding = 3; // Default to 3px as previously specified
    double m_switcherScreenCoverage = 0.9; // Default to 90% as previously specified
};

DirectSwitcher::Private::Private(DirectSwitcher *q)
    : q(q)
    , currentIndex(-1)
    , currentMode(Mode::Windows)
    , visible(false)
    , output(nullptr)
{
    // Load configuration from KWin settings
    loadConfiguration();
}

void DirectSwitcher::Private::loadConfiguration()
{
    // In a real implementation, this would load from kwinrc
    // For now, we'll keep the defaults but in a real implementation:
    // KConfigGroup config(kwinApp()->config(), "DirectSwitcher");
    // m_thumbnailWidth = config.readEntry("ThumbnailWidth", 600);
    // m_padding = config.readEntry("ThumbnailPadding", 3);
    // m_switcherScreenCoverage = config.readEntry("ScreenCoverage", 0.9);
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
    // Box should cover configurable percentage of the active screen
    const QRect outputGeometry = output ? output->geometry() : QRect(0, 0, 1920, 1080); // fallback
    const QRect switcherBox = QRect(
        outputGeometry.x() + outputGeometry.width() * (1.0 - d->m_switcherScreenCoverage) / 2, // Center horizontally with calculated coverage
        outputGeometry.y() + outputGeometry.height() * (1.0 - d->m_switcherScreenCoverage) / 2, // Center vertically with calculated coverage
        outputGeometry.width() * d->m_switcherScreenCoverage, // Configurable width coverage
        outputGeometry.height() * d->m_switcherScreenCoverage // Configurable height coverage
    );

    const int thumbnailWidth = d->m_thumbnailWidth; // Configurable width
    const int padding = d->m_padding; // Configurable padding
    const int spacing = padding * 2; // Double padding for spacing between items

    // Calculate how many thumbnails we can fit in the switcher box
    const int maxThumbsPerRow = std::max(1, switcherBox.width() / (thumbnailWidth + spacing));
    const int actualThumbWidth = std::min(thumbnailWidth,
                                          (switcherBox.width() - (maxThumbsPerRow - 1) * spacing) / maxThumbsPerRow);

    // Calculate thumbnail height based on aspect ratio preservation
    const int thumbnailHeight = std::min(switcherBox.height() / std::ceil((double)currentWindows.size() / maxThumbsPerRow),
                                         actualThumbWidth * 9 / 16); // Assuming 16:9 aspect ratio as default

    int x = switcherBox.x();
    int y = switcherBox.y();

    // Create thumbnail items for each window
    for (int i = 0; i < currentWindows.size(); ++i) {
        Window *window = currentWindows[i];

        // Create a surface item to represent the window thumbnail
        auto thumbnailItem = std::make_unique<SurfaceItem>(rootItem.get());

        // Position the thumbnail with padding
        thumbnailItem->setPos(QPointF(x + padding, y + padding));
        thumbnailItem->setSize(QSizeF(actualThumbWidth - 2 * padding, thumbnailHeight - 2 * padding));

        // Render the actual thumbnail of the window content
        renderThumbnail(window, QRect(x + padding, y + padding, actualThumbWidth - 2 * padding, thumbnailHeight - 2 * padding));

        thumbnailItems.push_back(std::move(thumbnailItem));

        // Move to next position
        x += actualThumbWidth + spacing;

        // Wrap to next row if needed
        if ((i + 1) % maxThumbsPerRow == 0) {
            x = switcherBox.x();
            y += thumbnailHeight + spacing;

            // Stop if we run out of vertical space
            if (y + thumbnailHeight > switcherBox.y() + switcherBox.height()) {
                break;
            }
        }
    }

    // Create a background rectangle for the switcher box
    auto backgroundItem = std::make_unique<Item>(rootItem.get());
    backgroundItem->setPos(QPointF(switcherBox.x(), switcherBox.y()));
    backgroundItem->setSize(QSizeF(switcherBox.width(), switcherBox.height()));
    // In a real implementation, we would set the background appearance here
    // For now, we'll just store it as a member
    // backgroundItem would typically be stored separately if needed for styling
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
    for (int i = 0; i < thumbnailItems.size(); ++i) {
        if (i == currentIndex) {
            // Highlight the selected thumbnail (e.g., add a border, increase brightness, etc.)
            // This would be implemented using the SurfaceItem's properties in a real implementation
        } else {
            // Ensure other thumbnails are not highlighted
        }
    }
}

void DirectSwitcher::Private::renderThumbnail(Window *window, const QRect &rect)
{
    // In a real implementation, we would capture the window's content and scale it
    // For now, we'll use the window's existing pixmap if available, or create a representation

    if (!window || !window->surface()) {
        return;
    }

    // In a real implementation, we would:
    // 1. Get the window's surface content using the compositor's capture capabilities
    // 2. Scale it to fit the rect dimensions (600px width as specified)
    // 3. Apply any visual effects (borders, drop shadows for selected item, etc.)

    // For the selected window, we might want to apply special styling
    const bool isSelected = (currentIndex >= 0 && currentIndex < currentWindows.size() && currentWindows[currentIndex] == window);

    // In a real implementation, we would use the compositor's rendering capabilities
    // to capture and scale the window content to the thumbnail size
    // This would involve using the window's buffer and creating a scaled texture

    Q_UNUSED(isSelected);
    Q_UNUSED(rect);

    // The actual rendering would happen through the SurfaceItem's texture management
    // which would handle capturing the window content and scaling it appropriately
    // using the compositor's scene graph and OpenGL capabilities
}

void DirectSwitcher::Private::renderText(const QString &text, const QRect &rect, const QColor &color)
{
    // This is a placeholder for rendering text
    // In a real implementation, we would render the window caption
    Q_UNUSED(text);
    Q_UNUSED(rect);
    Q_UNUSED(color);
}

void DirectSwitcher::setThumbnailWidth(int width)
{
    if (width > 0) {
        d->m_thumbnailWidth = width;
    }
}

void DirectSwitcher::setPadding(int padding)
{
    if (padding >= 0) {
        d->m_padding = padding;
    }
}

void DirectSwitcher::setSwitcherScreenCoverage(double coverage)
{
    if (coverage > 0.0 && coverage <= 1.0) {
        d->m_switcherScreenCoverage = coverage;
    }
}

int DirectSwitcher::thumbnailWidth() const
{
    return d->m_thumbnailWidth;
}

int DirectSwitcher::padding() const
{
    return d->m_padding;
}

double DirectSwitcher::switcherScreenCoverage() const
{
    return d->m_switcherScreenCoverage;
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