/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 KWin Team

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "windowmanagerdbusinterface.h"
#include "core/backendoutput.h"
#include "core/output.h"
#include "core/outputbackend.h"
#include "main.h"
#include "virtualdesktops.h"
#include "window.h"
#include "workspace.h"

#include <QDBusConnection>
#include <QPointer>
#include <QUuid>

namespace KWin
{

WindowManagerDBusInterface::WindowManagerDBusInterface(QObject *parent)
    : QObject(parent)
    , m_workspace(Workspace::self())
{
    qDebug() << "WindowManagerDBusInterface: Initializing and registering on DBus";

    QDBusConnection dbus = QDBusConnection::sessionBus();

    // Register the object WITH explicit interface export
    bool success = dbus.registerObject(
        QStringLiteral("/WindowManager"),
        QStringLiteral("org.kde.KWin.WindowManager"),
        this,
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);

    qDebug() << "WindowManagerDBusInterface: Registration" << (success ? "SUCCESS" : "FAILED");

    if (!success) {
        qDebug() << "DBus error:" << dbus.lastError().message();
    }

    // Connect to workspace signals for window state changes
    connect(m_workspace, &Workspace::windowAdded, this, [this](Window *w) {
        Q_EMIT windowAdded(w->internalId().toString());
    });

    connect(m_workspace, &Workspace::windowRemoved, this, [this](Window *w) {
        Q_EMIT windowRemoved(w->internalId().toString());
    });

    connect(m_workspace, &Workspace::windowActivated, this, [this](Window *w) {
        if (w) {
            Q_EMIT windowActivated(w->internalId().toString());
        }
    });
}

WindowManagerDBusInterface::~WindowManagerDBusInterface()
{
}

Window *WindowManagerDBusInterface::findWindowById(const QString &windowId)
{
    // Try as UUID first
    QUuid uuid = QUuid::fromString(windowId);
    if (!uuid.isNull()) {
        return m_workspace->findWindow(uuid);
    }

    // For now, just return nullptr for non-UUID IDs
    // In a full implementation, we might need additional lookup methods
    return nullptr;
}

QVariantMap WindowManagerDBusInterface::windowToVariantMap(Window *window)
{
    if (!window) {
        return {};
    }

    QVariantMap map;
    map[QStringLiteral("id")] = window->internalId().toString();
    map[QStringLiteral("title")] = window->captionNormal();
    map[QStringLiteral("resourceClass")] = window->resourceClass();
    map[QStringLiteral("resourceName")] = window->resourceName();
    map[QStringLiteral("desktopFile")] = window->desktopFileName();
    map[QStringLiteral("role")] = window->windowRole();
    map[QStringLiteral("clientMachine")] = window->wmClientMachine(true);
    map[QStringLiteral("localhost")] = window->isLocalhost();
    map[QStringLiteral("type")] = static_cast<int>(window->windowType());
    map[QStringLiteral("x")] = window->x();
    map[QStringLiteral("y")] = window->y();
    map[QStringLiteral("width")] = window->width();
    map[QStringLiteral("height")] = window->height();
    map[QStringLiteral("desktops")] = window->desktopIds();
    map[QStringLiteral("minimized")] = window->isMinimized();
    map[QStringLiteral("fullscreen")] = window->isFullScreen();
    map[QStringLiteral("keepAbove")] = window->keepAbove();
    map[QStringLiteral("keepBelow")] = window->keepBelow();
    map[QStringLiteral("noBorder")] = window->noBorder();
    map[QStringLiteral("skipTaskbar")] = window->skipTaskbar();
    map[QStringLiteral("skipPager")] = window->skipPager();
    map[QStringLiteral("skipSwitcher")] = window->skipSwitcher();
    map[QStringLiteral("maximizeHorizontal")] = window->maximizeMode() & MaximizeHorizontal;
    map[QStringLiteral("maximizeVertical")] = window->maximizeMode() & MaximizeVertical;
    map[QStringLiteral("opacity")] = window->opacity();
    map[QStringLiteral("transparency")] = window->opacity() < 1.0;
#if KWIN_BUILD_ACTIVITIES
    map[QStringLiteral("activities")] = window->activities();
#endif
    map[QStringLiteral("layer")] = window->layer();

    // Add PID if available
    if (window->pid() > 0) {
        map[QStringLiteral("pid")] = window->pid();
    }

    return map;
}

QVariantMap WindowManagerDBusInterface::windowStateToVariantMap(Window *window)
{
    if (!window) {
        return {};
    }

    QVariantMap state;
    state[QStringLiteral("minimized")] = window->isMinimized();
    state[QStringLiteral("maximized")] = (window->maximizeMode() & MaximizeMode::MaximizeFull) == MaximizeMode::MaximizeFull;
    state[QStringLiteral("fullscreen")] = window->isFullScreen();
    state[QStringLiteral("alwaysOnTop")] = window->keepAbove();
    state[QStringLiteral("alwaysOnBottom")] = window->keepBelow();
    state[QStringLiteral("active")] = window == m_workspace->activeWindow();
    state[QStringLiteral("decorated")] = !window->noBorder();
    state[QStringLiteral("movable")] = window->isMovable();
    state[QStringLiteral("resizable")] = window->isResizable();
    state[QStringLiteral("closeable")] = window->isCloseable();
    state[QStringLiteral("minimizable")] = window->isMinimizable();
    state[QStringLiteral("maximizable")] = window->isMaximizable();
    state[QStringLiteral("fullscreenable")] = window->isFullScreenable();
    state[QStringLiteral("transparency")] = window->opacity() < 1.0;

    return state;
}

// Window listing and finding methods
QVariantList WindowManagerDBusInterface::listWindows()
{
    QVariantList windows;
    const auto windowsList = m_workspace->windows();

    for (Window *window : windowsList) {
        if (window && window->isClient()) {
            windows.append(windowToVariantMap(window));
        }
    }

    return windows;
}

QVariantMap WindowManagerDBusInterface::getWindowInfo(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return windowToVariantMap(window);
}

QVariantList WindowManagerDBusInterface::findWindowsByClass(const QString &windowClass)
{
    QVariantList windows;
    const auto windowsList = m_workspace->windows();

    for (Window *window : windowsList) {
        if (window && window->isClient() && window->resourceClass() == windowClass) {
            windows.append(windowToVariantMap(window));
        }
    }

    return windows;
}

QVariantList WindowManagerDBusInterface::findWindowsByTitle(const QString &title)
{
    QVariantList windows;
    const auto windowsList = m_workspace->windows();

    for (Window *window : windowsList) {
        if (window && window->isClient() && window->captionNormal().contains(title, Qt::CaseInsensitive)) {
            windows.append(windowToVariantMap(window));
        }
    }

    return windows;
}

QVariantList WindowManagerDBusInterface::findWindowsByPid(quint32 pid)
{
    QVariantList windows;
    const auto windowsList = m_workspace->windows();

    for (Window *window : windowsList) {
        if (window && window->isClient() && static_cast<quint32>(window->pid()) == pid) {
            windows.append(windowToVariantMap(window));
        }
    }

    return windows;
}

QVariantMap WindowManagerDBusInterface::getActiveWindow()
{
    Window *active = m_workspace->activeWindow();
    return windowToVariantMap(active);
}

// Window property getters
QString WindowManagerDBusInterface::getWindowTitle(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return window ? window->captionNormal() : QString();
}

QString WindowManagerDBusInterface::getWindowClass(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return window ? window->resourceClass() : QString();
}

QString WindowManagerDBusInterface::getWindowId(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return window ? window->internalId().toString() : QString();
}

quint32 WindowManagerDBusInterface::getWindowPid(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return window ? window->pid() : 0;
}

QString WindowManagerDBusInterface::getWindowExecutable(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return window ? window->desktopFileName() : QString();
}

QVariantMap WindowManagerDBusInterface::getWindowPosition(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return {};
    }

    QVariantMap position;
    position[QStringLiteral("x")] = window->x();
    position[QStringLiteral("y")] = window->y();
    return position;
}

QVariantMap WindowManagerDBusInterface::getWindowSize(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return {};
    }

    QVariantMap size;
    size[QStringLiteral("width")] = window->width();
    size[QStringLiteral("height")] = window->height();
    return size;
}

QVariantMap WindowManagerDBusInterface::getWindowGeometry(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return {};
    }

    QVariantMap geometry;
    geometry[QStringLiteral("x")] = window->x();
    geometry[QStringLiteral("y")] = window->y();
    geometry[QStringLiteral("width")] = window->width();
    geometry[QStringLiteral("height")] = window->height();
    return geometry;
}

QVariantMap WindowManagerDBusInterface::getWindowState(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return windowStateToVariantMap(window);
}

bool WindowManagerDBusInterface::getWindowTransparency(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    return window ? window->opacity() < 1.0 : false;
}

// Window action methods
bool WindowManagerDBusInterface::activateWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    m_workspace->activateWindow(window);
    return true;
}

bool WindowManagerDBusInterface::closeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isCloseable()) {
        return false;
    }

    // Use QPointer to ensure window is still valid after closeWindow()
    QPointer<Window> windowPtr(window);
    window->closeWindow();
    // Don't access window after closeWindow() - it might be deleted
    return windowPtr != nullptr;
}

bool WindowManagerDBusInterface::maximizeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMaximizable()) {
        return false;
    }

    window->maximize(MaximizeMode::MaximizeFull);
    return true;
}

bool WindowManagerDBusInterface::unmaximizeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    window->setMaximize(false, false);
    return true;
}

bool WindowManagerDBusInterface::minimizeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMinimizable()) {
        return false;
    }

    window->setMinimized(true);
    return true;
}

bool WindowManagerDBusInterface::unminimizeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    window->setMinimized(false);
    return true;
}

bool WindowManagerDBusInterface::fullscreenWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isFullScreenable()) {
        return false;
    }

    window->setFullScreen(true);
    return true;
}

bool WindowManagerDBusInterface::unfullscreenWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    window->setFullScreen(false);
    return true;
}

bool WindowManagerDBusInterface::setAlwaysOnTop(const QString &windowId, bool enabled)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    window->setKeepAbove(enabled);
    return true;
}

bool WindowManagerDBusInterface::setWindowOpacity(const QString &windowId, double opacity)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    opacity = std::clamp(opacity, 0.1, 1.0);
    window->setOpacity(opacity);
    return true;
}

// Window geometry operations
bool WindowManagerDBusInterface::moveWindow(const QString &windowId, int x, int y)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMovable()) {
        return false;
    }

    window->move(QPointF(x, y));
    return true;
}

bool WindowManagerDBusInterface::resizeWindow(const QString &windowId, int width, int height)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isResizable()) {
        return false;
    }

    window->resize(QSizeF(width, height));
    return true;
}

bool WindowManagerDBusInterface::moveAndResizeWindow(const QString &windowId, int x, int y, int width, int height)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMovable() || !window->isResizable()) {
        return false;
    }

    window->moveResize(QRectF(x, y, width, height));
    return true;
}

bool WindowManagerDBusInterface::centerWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMovable()) {
        return false;
    }

    // Get current output/monitor
    LogicalOutput *output = window->output();
    if (!output) {
        return false;
    }

    QRectF outputRect = QRectF(output->geometry().x(), output->geometry().y(),
                               output->geometry().width(), output->geometry().height());
    QRectF windowRect = window->frameGeometry();

    QPointF centerPos(
        outputRect.center().x() - windowRect.width() / 2,
        outputRect.center().y() - windowRect.height() / 2);

    window->move(centerPos);
    return true;
}

bool WindowManagerDBusInterface::sendWindowToMonitor(const QString &windowId, int monitor)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMovable()) {
        return false;
    }

    const auto backendOutputs = kwinApp()->outputBackend()->outputs();
    if (monitor < 0 || monitor >= backendOutputs.count()) {
        return false;
    }

    BackendOutput *backendOutput = backendOutputs[monitor];
    LogicalOutput *output = m_workspace->findOutput(backendOutput);
    if (!output) {
        return false;
    }

    QRectF outputRect = QRectF(output->geometry().x(), output->geometry().y(),
                               output->geometry().width(), output->geometry().height());
    QRectF windowRect = window->frameGeometry();

    // Center window on the new monitor
    QPointF newPos(
        outputRect.x() + (outputRect.width() - windowRect.width()) / 2,
        outputRect.y() + (outputRect.height() - windowRect.height()) / 2);

    window->move(newPos);
    return true;
}

bool WindowManagerDBusInterface::sendWindowToMonitorByName(const QString &windowId, const QString &monitorName)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMovable()) {
        return false;
    }

    const auto outputs = m_workspace->outputs();
    LogicalOutput *targetOutput = nullptr;
    for (auto *output : outputs) {
        if (output->name() == monitorName) {
            targetOutput = output;
            break;
        }
    }

    if (!targetOutput) {
        return false;
    }

    QRectF outputRect = QRectF(targetOutput->geometry().x(), targetOutput->geometry().y(),
                               targetOutput->geometry().width(), targetOutput->geometry().height());
    QRectF windowRect = window->frameGeometry();

    // Center window on new monitor
    QPointF newPos(
        outputRect.x() + (outputRect.width() - windowRect.width()) / 2,
        outputRect.y() + (outputRect.height() - windowRect.height()) / 2);

    window->move(newPos);
    return true;
}

bool WindowManagerDBusInterface::sendWindowToDesktop(const QString &windowId, int desktop)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForX11Id(desktop);
    if (!vd) {
        return false;
    }

    window->setDesktops({vd});
    return true;
}

// Advanced operations
bool WindowManagerDBusInterface::toggleMaximizeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMaximizable()) {
        return false;
    }

    bool isMaximized = (window->maximizeMode() & MaximizeMode::MaximizeFull) == MaximizeMode::MaximizeFull;
    if (isMaximized) {
        return unmaximizeWindow(windowId);
    } else {
        return maximizeWindow(windowId);
    }
}

bool WindowManagerDBusInterface::toggleFullscreenWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isFullScreenable()) {
        return false;
    }

    bool isFullscreen = window->isFullScreen();
    if (isFullscreen) {
        return unfullscreenWindow(windowId);
    } else {
        return fullscreenWindow(windowId);
    }
}

bool WindowManagerDBusInterface::toggleMinimizeWindow(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window || !window->isMinimizable()) {
        return false;
    }

    bool isMinimized = window->isMinimized();
    if (isMinimized) {
        return unminimizeWindow(windowId);
    } else {
        return minimizeWindow(windowId);
    }
}

bool WindowManagerDBusInterface::toggleAlwaysOnTop(const QString &windowId)
{
    Window *window = findWindowById(windowId);
    if (!window) {
        return false;
    }

    bool isAlwaysOnTop = window->keepAbove();
    return setAlwaysOnTop(windowId, !isAlwaysOnTop);
}

QVariantMap WindowManagerDBusInterface::getMonitorInfo(int monitor)
{
    const auto backendOutputs = kwinApp()->outputBackend()->outputs();
    if (monitor < 0 || monitor >= backendOutputs.count()) {
        return {};
    }

    BackendOutput *backendOutput = backendOutputs[monitor];
    LogicalOutput *output = m_workspace->findOutput(backendOutput);
    if (!output) {
        return {};
    }

    QVariantMap info;
    info[QStringLiteral("index")] = monitor;
    info[QStringLiteral("name")] = output->name();
    info[QStringLiteral("x")] = output->geometry().x();
    info[QStringLiteral("y")] = output->geometry().y();
    info[QStringLiteral("width")] = output->geometry().width();
    info[QStringLiteral("height")] = output->geometry().height();
    info[QStringLiteral("refreshRate")] = output->refreshRate();
    info[QStringLiteral("scale")] = output->scale();
    info[QStringLiteral("enabled")] = backendOutput->isEnabled();

    return info;
}

QVariantList WindowManagerDBusInterface::listMonitors()
{
    QVariantList monitors;
    const auto outputs = kwinApp()->outputBackend()->outputs();

    for (int i = 0; i < outputs.count(); ++i) {
        monitors.append(getMonitorInfo(i));
    }

    return monitors;
}

}
