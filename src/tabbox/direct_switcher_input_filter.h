/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "../input.h" // For InputEventFilter
#include "direct_switcher.h" // For Mode enum in slot methods

#include <QAction>
#include <QKeySequence>
#include <QList>

// Include the input event structures
#include "../input_event.h"
#include "../utils/common.h" // For logging

namespace KWin
{

class DirectSwitcher;
class DirectSwitcherEffect;

class DirectSwitcherInputFilter : public QObject, public InputEventFilter
{
    Q_OBJECT

public:
    explicit DirectSwitcherInputFilter(QObject *parent = nullptr);

    // Keyboard event handling
    bool keyboardKey(KeyboardKeyEvent *event) override;

    // InputEventFilter overrides
    bool pointerMotion(PointerMotionEvent *event) override;
    bool pointerButton(PointerButtonEvent *event) override;
    bool pointerFrame() override;
    bool pointerAxis(PointerAxisEvent *event) override;
    bool touchDown(qint32 id, const QPointF &pos, std::chrono::microseconds time) override;
    bool touchMotion(qint32 id, const QPointF &pos, std::chrono::microseconds time) override;
    bool touchUp(qint32 id, std::chrono::microseconds time) override;
    bool touchCancel() override;
    bool touchFrame() override;

    // Register the switcher actions with global shortcuts
    void initShortcuts();

    // Reload configuration from kwinrc
    void reloadConfiguration();

    // Check if we should use the new switcher or fall back to old tabbox
    bool shouldUseNewSwitcher() const;

    // Switch between old and new switcher implementations
    void setUseNewSwitcher(bool useNew);
    bool useNewSwitcher() const;

    // Get the switcher from the effect (single source of truth)
    DirectSwitcher *switcher();

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

    DirectSwitcherEffect *m_effect = nullptr;
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

} // namespace KWin