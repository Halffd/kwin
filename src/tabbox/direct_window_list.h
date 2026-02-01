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

class Window;

/**
 * A direct interface for managing window lists without Qt Model/View abstractions.
 * This class provides fast access to window lists for compositor primitives like Alt+Tab.
 */
class DirectWindowList : public QObject
{
    Q_OBJECT

public:
    explicit DirectWindowList(QObject *parent = nullptr);
    ~DirectWindowList() override;

    /**
     * Gets the current list of windows based on the specified criteria.
     * This is a direct, fast path that bypasses Qt Model/View.
     */
    QList<Window *> getWindowList(bool includeMinimized = true, bool currentDesktopOnly = true, bool currentActivityOnly = true);

    /**
     * Gets the current active window.
     */
    Window *getActiveWindow() const;

    /**
     * Gets the next window in the focus chain.
     */
    Window *getNextWindow(Window *current) const;

    /**
     * Gets the previous window in the focus chain.
     */
    Window *getPreviousWindow(Window *current) const;

    /**
     * Creates a snapshot of the current window state for fast switching.
     */
    struct WindowSnapshot
    {
        Window *window;
        QRect geometry;
        bool isMinimized;
        bool isOnCurrentDesktop;
        bool isOnCurrentActivity;
        QString caption;
        // Add other properties as needed for fast rendering
    };

    QList<WindowSnapshot> createSnapshot(bool includeMinimized = true, bool currentDesktopOnly = true, bool currentActivityOnly = true);

private:
    // Internal implementation details
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace KWin