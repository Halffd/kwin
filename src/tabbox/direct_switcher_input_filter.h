/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "../input_event_spy.h"
#include "direct_switcher.h"

#include <QAction>
#include <QKeySequence>
#include <QList>

class KConfig;

namespace KWin
{
namespace TabBox
{
class TabBoxInputFilter;
}

class DirectSwitcherInputFilter : public InputEventSpy
{
    Q_OBJECT

public:
    explicit DirectSwitcherInputFilter(QObject *parent = nullptr);

    // Keyboard event handling
    void keyboardKey(KeyboardKeyEvent *event) override;

    // Register the switcher actions with global shortcuts
    void initShortcuts();

    // Switch between old and new switcher implementations
    void setUseNewSwitcher(bool useNew);
    bool useNewSwitcher() const;

    // Reload configuration from kwinrc
    void reloadConfiguration();

    // Check if we should use the new switcher or fall back to old tabbox
    bool shouldUseNewSwitcher() const;

private Q_SLOTS:
    void slotWalkThroughWindows();
    void slotWalkBackThroughWindows();
    void slotWalkThroughCurrentAppWindows();
    void slotWalkBackThroughCurrentAppWindows();
    void slotWalkThroughWindowsAlternative();
    void slotWalkBackThroughWindowsAlternative();
    void slotWalkThroughCurrentAppWindowsAlternative();
    void slotWalkBackThroughCurrentAppWindowsAlternative();

private:
    void navigateThroughWindows(bool forward, const QList<QKeySequence> &shortcut, DirectSwitcher::Mode mode);
    bool areModKeysDepressed(const QList<QKeySequence> &seq) const;
    void loadConfiguration();
    void handleOldTabboxEvent(KeyboardKeyEvent *event);

    DirectSwitcher m_directSwitcher;
    std::unique_ptr<TabBox::TabBoxInputFilter> m_oldTabboxFilter;
    bool m_useNewSwitcher;
    bool m_switcherActive;
    bool m_grabActive;

    // Key sequences for different switcher modes
    QList<QKeySequence> m_cutWalkThroughWindows;
    QList<QKeySequence> m_cutWalkThroughWindowsReverse;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindows;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindowsReverse;
    QList<QKeySequence> m_cutWalkThroughWindowsAlternative;
    QList<QKeySequence> m_cutWalkThroughWindowsAlternativeReverse;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindowsAlternative;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindowsAlternativeReverse;
};

/**
 * Input filter for the direct switcher that handles Alt+Tab and related shortcuts.
 * This filter intercepts keyboard events to control the fast switcher.
 */
class DirectSwitcherInputFilter : public InputEventSpy
{
    Q_OBJECT

public:
    explicit DirectSwitcherInputFilter(QObject *parent = nullptr);

    // Keyboard event handling
    void keyboardKey(KeyboardKeyEvent *event) override;

    // Register the switcher actions with global shortcuts
    void initShortcuts();

private Q_SLOTS:
    void slotWalkThroughWindows();
    void slotWalkBackThroughWindows();
    void slotWalkThroughCurrentAppWindows();
    void slotWalkBackThroughCurrentAppWindows();
    void slotWalkThroughWindowsAlternative();
    void slotWalkBackThroughWindowsAlternative();
    void slotWalkThroughCurrentAppWindowsAlternative();
    void slotWalkBackThroughCurrentAppWindowsAlternative();

    // Reload configuration from kwinrc
    void reloadConfiguration();

    // Check if we should use the new switcher or fall back to old tabbox
    bool shouldUseNewSwitcher() const;

private:
    void navigateThroughWindows(bool forward, const QList<QKeySequence> &shortcut, DirectSwitcher::Mode mode);
    bool areModKeysDepressed(const QList<QKeySequence> &seq) const;
    void loadConfiguration();

    DirectSwitcher m_directSwitcher;
    bool m_switcherActive;
    bool m_grabActive;

    // Key sequences for different switcher modes
    QList<QKeySequence> m_cutWalkThroughWindows;
    QList<QKeySequence> m_cutWalkThroughWindowsReverse;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindows;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindowsReverse;
    QList<QKeySequence> m_cutWalkThroughWindowsAlternative;
    QList<QKeySequence> m_cutWalkThroughWindowsAlternativeReverse;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindowsAlternative;
    QList<QKeySequence> m_cutWalkThroughCurrentAppWindowsAlternativeReverse;
};

} // namespace KWin