/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "direct_window_list.h"
#include "../focuschain.h"
#include "../window.h"
#include "../workspace.h"

#include <QRect>

namespace KWin
{

class DirectWindowList::Private
{
public:
    Private(DirectWindowList *q);
    ~Private();

    /**
     * Helper method to check if a window should be included based on criteria
     */
    bool shouldIncludeWindow(Window *window, bool includeMinimized, bool currentDesktopOnly, bool currentActivityOnly) const;

    DirectWindowList *q;
};

DirectWindowList::Private::Private(DirectWindowList *q)
    : q(q)
{
}

DirectWindowList::Private::~Private()
{
}

bool DirectWindowList::Private::shouldIncludeWindow(Window *window, bool includeMinimized, bool currentDesktopOnly, bool currentActivityOnly) const
{
    if (!window || window->isDeleted()) {
        return false;
    }

    // Basic checks
    if (!window->wantsTabFocus() || window->skipSwitcher()) {
        return false;
    }

    // Check desktop
    if (currentDesktopOnly && !window->isOnCurrentDesktop()) {
        return false;
    }

    // Check activity
    if (currentActivityOnly && !window->isOnCurrentActivity()) {
        return false;
    }

    // Check minimized
    if (!includeMinimized && window->isMinimized()) {
        return false;
    }

    // Check modal dialogs
    Window *modal = window->findModal();
    if (modal && modal != window) {
        // If modal is already in the list, don't add the parent
        // This is a simplified check - in practice you'd need to check the full list
        return false;
    }

    return true;
}

DirectWindowList::DirectWindowList(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
}

DirectWindowList::~DirectWindowList() = default;

QList<Window *> DirectWindowList::getWindowList(bool includeMinimized, bool currentDesktopOnly, bool currentActivityOnly)
{
    QList<Window *> result;

    // Get the focus chain which represents the logical ordering of windows
    const auto focusChain = Workspace::self()->focusChain();
    if (!focusChain) {
        return result;
    }

    // Iterate through the focus chain to get windows in the correct order
    for (Window *window : focusChain->allMostRecentlyUsed()) {
        if (d->shouldIncludeWindow(window, includeMinimized, currentDesktopOnly, currentActivityOnly)) {
            result.append(window);
        }
    }

    return result;
}

Window *DirectWindowList::getActiveWindow() const
{
    return Workspace::self()->activeWindow();
}

Window *DirectWindowList::getNextWindow(Window *current) const
{
    if (!current) {
        // If no current window, get the first in the focus chain
        const auto focusChain = Workspace::self()->focusChain();
        return focusChain ? focusChain->firstMostRecentlyUsed() : nullptr;
    }

    const auto focusChain = Workspace::self()->focusChain();
    return focusChain ? focusChain->nextMostRecentlyUsed(current) : nullptr;
}

Window *DirectWindowList::getPreviousWindow(Window *current) const
{
    if (!current) {
        // If no current window, get the first in the focus chain
        const auto focusChain = Workspace::self()->focusChain();
        return focusChain ? focusChain->firstMostRecentlyUsed() : nullptr;
    }

    // To get the previous window, we need to iterate through the MRU list
    const auto mruList = Workspace::self()->focusChain()->allMostRecentlyUsed();
    int currentIndex = mruList.indexOf(current);

    if (currentIndex == -1) {
        // Current window not in the list, return first
        return mruList.isEmpty() ? nullptr : mruList.first();
    }

    // Get the previous item, wrapping around if needed
    int prevIndex = (currentIndex == 0) ? mruList.size() - 1 : currentIndex - 1;
    return (prevIndex >= 0 && prevIndex < mruList.size()) ? mruList[prevIndex] : nullptr;
}

QList<DirectWindowList::WindowSnapshot> DirectWindowList::createSnapshot(bool includeMinimized, bool currentDesktopOnly, bool currentActivityOnly)
{
    QList<WindowSnapshot> snapshots;
    const auto windows = getWindowList(includeMinimized, currentDesktopOnly, currentActivityOnly);

    for (Window *window : windows) {
        WindowSnapshot snapshot;
        snapshot.window = window;
        snapshot.geometry = window->frameGeometry().toAlignedRect();
        snapshot.isMinimized = window->isMinimized();
        snapshot.isOnCurrentDesktop = window->isOnCurrentDesktop();
        snapshot.isOnCurrentActivity = window->isOnCurrentActivity();
        snapshot.caption = window->caption();

        snapshots.append(snapshot);
    }

    return snapshots;
}

} // namespace KWin