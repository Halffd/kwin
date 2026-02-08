/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 KWin Team

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QVariantMap>

namespace KWin
{

class Window;
class Workspace;

/**
 * @brief DBus interface for comprehensive window management operations.
 *
 * This interface provides methods for:
 * - Getting window properties (title, class, id, pid, executable, position, size, state)
 * - Listing and finding windows
 * - Window actions (activate, close, maximize, minimize, fullscreen, always-on-top)
 * - Window geometry operations (move, resize, send to monitor)
 */
class WindowManagerDBusInterface : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.WindowManager")

public:
    explicit WindowManagerDBusInterface(QObject *parent);
    ~WindowManagerDBusInterface() override;

public Q_SLOTS:
    // Window listing and finding
    QVariantList listWindows();
    QVariantMap getWindowInfo(const QString &windowId);
    QVariantList findWindowsByClass(const QString &windowClass);
    QVariantList findWindowsByTitle(const QString &title);
    QVariantList findWindowsByPid(quint32 pid);
    QVariantMap getActiveWindow();

    // Window properties
    QString getWindowTitle(const QString &windowId);
    QString getWindowClass(const QString &windowId);
    QString getWindowId(const QString &windowId);
    quint32 getWindowPid(const QString &windowId);
    QString getWindowExecutable(const QString &windowId);
    QVariantMap getWindowPosition(const QString &windowId);
    QVariantMap getWindowSize(const QString &windowId);
    QVariantMap getWindowGeometry(const QString &windowId);
    QVariantMap getWindowState(const QString &windowId);
    bool getWindowTransparency(const QString &windowId);

    // Window actions
    bool activateWindow(const QString &windowId);
    bool closeWindow(const QString &windowId);
    bool maximizeWindow(const QString &windowId);
    bool unmaximizeWindow(const QString &windowId);
    bool minimizeWindow(const QString &windowId);
    bool unminimizeWindow(const QString &windowId);
    bool fullscreenWindow(const QString &windowId);
    bool unfullscreenWindow(const QString &windowId);
    bool setAlwaysOnTop(const QString &windowId, bool enabled);
    bool setWindowOpacity(const QString &windowId, double opacity);

    // Window geometry operations
    bool moveWindow(const QString &windowId, int x, int y);
    bool resizeWindow(const QString &windowId, int width, int height);
    bool moveAndResizeWindow(const QString &windowId, int x, int y, int width, int height);
    bool centerWindow(const QString &windowId);
    bool sendWindowToMonitor(const QString &windowId, int monitor);
    bool sendWindowToDesktop(const QString &windowId, int desktop);

    // Advanced operations
    bool toggleMaximizeWindow(const QString &windowId);
    bool toggleFullscreenWindow(const QString &windowId);
    bool toggleMinimizeWindow(const QString &windowId);
    bool toggleAlwaysOnTop(const QString &windowId);
    QVariantMap getMonitorInfo(int monitor);
    QVariantList listMonitors();

private:
    Window *findWindowById(const QString &windowId);
    QVariantMap windowToVariantMap(Window *window);
    QVariantMap windowStateToVariantMap(Window *window);

    Workspace *m_workspace;
};

}
