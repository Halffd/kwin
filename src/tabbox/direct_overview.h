/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <memory>

namespace KWin
{

class Item;
class ImageItem;

/**
 * A fast, direct overview implementation that bypasses Qt Model/View and QML.
 * Renders virtual desktops/activities as a grid of thumbnails using the compositor scene graph.
 * Similar architecture to DirectSwitcher but for overview mode (no Alt+Tab, triggered via button/key).
 */
class DirectOverview : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Activities,
        VirtualDesktops,
        Workspaces
    };

    explicit DirectOverview(QObject *parent = nullptr);
    ~DirectOverview() override;

    /**
     * Set the parent item for the overview's scene graph node.
     * This should be called with the WorkspaceScene's overlayItem().
     */
    void setParentItem(Item *parentItem);

    /**
     * Shows the overview with the given mode.
     */
    void show(Mode mode = Mode::VirtualDesktops);

    /**
     * Hides the overview.
     */
    void hide();

    /**
     * Moves selection to the next item (desktop/activity).
     */
    void selectNext();

    /**
     * Moves selection to the previous item.
     */
    void selectPrevious();

    /**
     * Accepts the current selection and switches to that desktop/activity.
     */
    void accept();

    /**
     * Checks if the overview is currently visible.
     */
    bool isVisible() const;

    /**
     * Sets the output where the overview should appear.
     */
    void setOutput(void *output); // void* to avoid forward declaring Output

    /**
     * Gets the currently selected item.
     */
    int currentSelection() const;

    /**
     * Sets the grid configuration for the overview layout.
     */
    void setGridColumns(int cols);
    void setGridSpacing(int spacing);
    void setItemSize(int width, int height);

    /**
     * Gets the grid configuration.
     */
    int gridColumns() const;
    int gridSpacing() const;
    int itemWidth() const;
    int itemHeight() const;

    /**
     * Performance measurement via KWIN_PERF environment variable.
     */
    void enablePerformanceMeasurement(bool enabled);
    bool performanceMeasurementEnabled() const;

    /**
     * Invalidate desktop cache (called on desktop count/order changes).
     */
    void invalidateDesktopCache();

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void selectionChanged(int index);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace KWin
