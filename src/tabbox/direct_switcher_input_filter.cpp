/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "direct_switcher_input_filter.h"
#include "../focuschain.h"
#include "../globalshortcuts.h"
#include "../input.h"
#include "../keyboard_input.h"
#include "../virtualdesktops.h"
#include "../window.h"
#include "../workspace.h"

#include <KConfig>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>

#include <QAction>
#include <QKeyEvent>
#include <QKeySequence>

namespace KWin
{

DirectSwitcherInputFilter::DirectSwitcherInputFilter(QObject *parent)
    : InputEventSpy(parent)
    , m_switcherActive(false)
    , m_grabActive(false)
{
    initShortcuts();
}

void DirectSwitcherInputFilter::initShortcuts()
{
    // Define the key sequences for the new switcher
    // Using similar shortcuts as the original tabbox but mapped to our new implementation
    m_cutWalkThroughWindows = {Qt::AltModifier | Qt::Key_Tab, Qt::MetaModifier | Qt::Key_Tab};
    m_cutWalkThroughWindowsReverse = {Qt::AltModifier | Qt::ShiftModifier | Qt::Key_Tab, Qt::MetaModifier | Qt::ShiftModifier | Qt::Key_Tab};
    m_cutWalkThroughCurrentAppWindows = {Qt::AltModifier | Qt::Key_QuoteLeft, Qt::MetaModifier | Qt::Key_QuoteLeft};
    m_cutWalkThroughCurrentAppWindowsReverse = {Qt::AltModifier | Qt::Key_AsciiTilde, Qt::MetaModifier | Qt::Key_AsciiTilde};
    m_cutWalkThroughWindowsAlternative = {};
    m_cutWalkThroughWindowsAlternativeReverse = {};
    m_cutWalkThroughCurrentAppWindowsAlternative = {};
    m_cutWalkThroughCurrentAppWindowsAlternativeReverse = {};
}

void DirectSwitcherInputFilter::keyboardKey(KeyboardKeyEvent *event)
{
    if (!m_switcherActive) {
        // Check if this is a key press that should start the switcher
        if (event->state == KeyboardKeyState::Pressed) {
            // Check for forward navigation shortcuts
            if (areModKeysDepressed(m_cutWalkThroughWindows) || areModKeysDepressed(m_cutWalkThroughCurrentAppWindows) || areModKeysDepressed(m_cutWalkThroughWindowsAlternative) || areModKeysDepressed(m_cutWalkThroughCurrentAppWindowsAlternative)) {

                // Determine which mode to use based on the pressed keys
                DirectSwitcher::Mode mode = DirectSwitcher::Mode::Windows;

                if (areModKeysDepressed(m_cutWalkThroughCurrentAppWindows)) {
                    mode = DirectSwitcher::Mode::CurrentAppWindows;
                } else if (areModKeysDepressed(m_cutWalkThroughWindowsAlternative)) {
                    mode = DirectSwitcher::Mode::WindowsAlternative;
                } else if (areModKeysDepressed(m_cutWalkThroughCurrentAppWindowsAlternative)) {
                    mode = DirectSwitcher::Mode::CurrentAppWindowsAlternative;
                }

                // Start the switcher
                m_directSwitcher.show(mode);
                m_switcherActive = true;
                m_grabActive = true;

                // Consume the event to prevent it from being processed elsewhere
                return;
            }
        }
    } else {
        // Switcher is active, handle navigation
        if (event->state == KeyboardKeyState::Pressed) {
            // Check for navigation keys
            if (event->key == Qt::Key_Tab) {
                if (event->modifiers & Qt::ShiftModifier) {
                    m_directSwitcher.selectPrevious();
                } else {
                    m_directSwitcher.selectNext();
                }
                return; // Consume the event
            } else if (event->key == Qt::Key_Escape) {
                m_directSwitcher.hide();
                m_switcherActive = false;
                m_grabActive = false;
                return; // Consume the event
            } else if (event->key == Qt::Key_Return || event->key == Qt::Key_Enter || event->key == Qt::Key_Space) {
                m_directSwitcher.accept();
                m_switcherActive = false;
                m_grabActive = false;
                return; // Consume the event
            }
        } else if (event->state == KeyboardKeyState::Released) {
            // Check if the modifier keys are released to close the switcher
            if (!areModKeysDepressed(m_cutWalkThroughWindows) && !areModKeysDepressed(m_cutWalkThroughCurrentAppWindows) && !areModKeysDepressed(m_cutWalkThroughWindowsAlternative) && !areModKeysDepressed(m_cutWalkThroughCurrentAppWindowsAlternative)) {

                // Only close if we're not consuming other keys (like Tab)
                if (event->key != Qt::Key_Tab && event->key != Qt::Key_Shift) {
                    m_directSwitcher.accept(); // Accept current selection
                    m_switcherActive = false;
                    m_grabActive = false;
                }
            }
        }
    }

    // If switcher is active, consume most keyboard events to maintain focus
    if (m_switcherActive) {
        // Don't consume modifier key releases as they're used to close the switcher
        if (event->key != Qt::Key_Alt && event->key != Qt::Key_Meta && event->key != Qt::Key_Control && event->key != Qt::Key_Shift) {
            return; // Consume the event
        }
    }
}

void DirectSwitcherInputFilter::slotWalkThroughWindows()
{
    navigateThroughWindows(true, m_cutWalkThroughWindows, DirectSwitcher::Mode::Windows);
}

void DirectSwitcherInputFilter::slotWalkBackThroughWindows()
{
    navigateThroughWindows(false, m_cutWalkThroughWindowsReverse, DirectSwitcher::Mode::Windows);
}

void DirectSwitcherInputFilter::slotWalkThroughCurrentAppWindows()
{
    navigateThroughWindows(true, m_cutWalkThroughCurrentAppWindows, DirectSwitcher::Mode::CurrentAppWindows);
}

void DirectSwitcherInputFilter::slotWalkBackThroughCurrentAppWindows()
{
    navigateThroughWindows(false, m_cutWalkThroughCurrentAppWindowsReverse, DirectSwitcher::Mode::CurrentAppWindows);
}

void DirectSwitcherInputFilter::slotWalkThroughWindowsAlternative()
{
    navigateThroughWindows(true, m_cutWalkThroughWindowsAlternative, DirectSwitcher::Mode::WindowsAlternative);
}

void DirectSwitcherInputFilter::slotWalkBackThroughWindowsAlternative()
{
    navigateThroughWindows(false, m_cutWalkThroughWindowsAlternativeReverse, DirectSwitcher::Mode::WindowsAlternative);
}

void DirectSwitcherInputFilter::slotWalkThroughCurrentAppWindowsAlternative()
{
    navigateThroughWindows(true, m_cutWalkThroughCurrentAppWindowsAlternative, DirectSwitcher::Mode::CurrentAppWindowsAlternative);
}

void DirectSwitcherInputFilter::slotWalkBackThroughCurrentAppWindowsAlternative()
{
    navigateThroughWindows(false, m_cutWalkThroughCurrentAppWindowsAlternativeReverse, DirectSwitcher::Mode::CurrentAppWindowsAlternative);
}

void DirectSwitcherInputFilter::navigateThroughWindows(bool forward, const QList<QKeySequence> &shortcut, DirectSwitcher::Mode mode)
{
    Q_UNUSED(shortcut); // We handle this differently in our input filter

    if (!m_switcherActive) {
        // Start the switcher
        m_directSwitcher.show(mode);
        m_switcherActive = true;
        m_grabActive = true;
    } else {
        // Navigate within the existing switcher
        if (forward) {
            m_directSwitcher.selectNext();
        } else {
            m_directSwitcher.selectPrevious();
        }
    }
}

bool DirectSwitcherInputFilter::areModKeysDepressed(const QList<QKeySequence> &seq) const
{
    if (seq.isEmpty()) {
        return false;
    }

    const Qt::KeyboardModifiers mods = input()->keyboardModifiers();

    for (const QKeySequence &sequence : seq) {
        const Qt::KeyboardModifiers mod = sequence[sequence.count() - 1].keyboardModifiers();

        // Check if all required modifiers are pressed
        if ((mod & Qt::ShiftModifier) && !(mods & Qt::ShiftModifier))
            continue;
        if ((mod & Qt::ControlModifier) && !(mods & Qt::ControlModifier))
            continue;
        if ((mod & Qt::AltModifier) && !(mods & Qt::AltModifier))
            continue;
        if ((mod & Qt::MetaModifier) && !(mods & Qt::MetaModifier))
            continue;

        // Check if no extra modifiers are pressed (unless they're part of the sequence)
        const Qt::KeyboardModifiers extraMods = mods & ~(Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (extraMods != (mod & extraMods))
            continue;

        return true;
    }

    return false;
}

} // namespace KWin