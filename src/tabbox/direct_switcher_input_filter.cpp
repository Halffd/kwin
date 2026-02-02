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
#include "../main.h" // For kwinApp()
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
    : QObject(parent)
    , InputEventFilter(InputFilterOrder::TabBox) // Using TabBox order
    , m_useNewSwitcher(true) // Default to new switcher
    , m_switcherActive(false)
    , m_grabActive(false)
{
    qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter constructor called";

    // Load configuration to determine which switcher to use
    loadConfiguration();

    qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter configuration loaded. Use new switcher:" << m_useNewSwitcher;

    initShortcuts();

    // Connect to configuration change signals to reload settings
    // Note: KConfig doesn't have a configChanged signal directly.
    // This would need to be connected to a KConfigWatcher in a real implementation.
    // For now, we'll comment this out to avoid compilation error.
    // connect(kwinApp()->config(), &KConfig::configChanged, this, &DirectSwitcherInputFilter::reloadConfiguration);
}

void DirectSwitcherInputFilter::loadConfiguration()
{
    // Load configuration to determine which switcher to use
    KConfigGroup config(kwinApp()->config(), "TabBox");
    m_useNewSwitcher = config.readEntry("UseNewSwitcher", true); // Default to new switcher

    // Load direct switcher configuration
    KConfigGroup dsConfig(kwinApp()->config(), "DirectSwitcher");
    const int thumbnailWidth = dsConfig.readEntry("ThumbnailWidth", 600);
    const int padding = dsConfig.readEntry("ThumbnailPadding", 3);
    const double screenCoverage = dsConfig.readEntry("ScreenCoverage", 0.9);

    m_directSwitcher.setThumbnailWidth(thumbnailWidth);
    m_directSwitcher.setPadding(padding);
    m_directSwitcher.setSwitcherScreenCoverage(screenCoverage);
}

void DirectSwitcherInputFilter::setUseNewSwitcher(bool useNew)
{
    m_useNewSwitcher = useNew;
}

bool DirectSwitcherInputFilter::useNewSwitcher() const
{
    return m_useNewSwitcher;
}

bool DirectSwitcherInputFilter::shouldUseNewSwitcher() const
{
    // Check the configuration to see if we should use the new switcher
    KConfigGroup config(kwinApp()->config(), "TabBox");
    bool useNew = config.readEntry("UseNewSwitcher", true); // Default to new switcher
    qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter::shouldUseNewSwitcher returning:" << useNew;
    return useNew;
}

void DirectSwitcherInputFilter::handleOldTabboxEvent(KeyboardKeyEvent *event)
{
    // Forward the event to the old tabbox filter if needed
    // This would be called when using the old tabbox implementation
    Q_UNUSED(event);
    // In a real implementation, we would call m_oldTabboxFilter->keyboardKey(event)
    // but we need to access the workspace tabbox for this to work properly
}

void DirectSwitcherInputFilter::reloadConfiguration()
{
    // Reload configuration from kwinrc
    KConfigGroup config(kwinApp()->config(), "DirectSwitcher");
    const int thumbnailWidth = config.readEntry("ThumbnailWidth", 600);
    const int padding = config.readEntry("ThumbnailPadding", 3);
    const double screenCoverage = config.readEntry("ScreenCoverage", 0.9);

    m_directSwitcher.setThumbnailWidth(thumbnailWidth);
    m_directSwitcher.setPadding(padding);
    m_directSwitcher.setSwitcherScreenCoverage(screenCoverage);
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

bool DirectSwitcherInputFilter::keyboardKey(KeyboardKeyEvent *event)
{
    qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter::keyboardKey called, event state:" << (int)event->state;

    // Check current configuration to determine which switcher to use
    if (!shouldUseNewSwitcher()) {
        qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: not using new switcher, falling back";
        // Use the old tabbox implementation
        // In a real implementation, we would delegate to the old tabbox filter
        // For now, we'll just return false to let other filters handle it
        return false;
    }

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

                // Set the output for the switcher to appear on the active output
                if (workspace() && workspace()->activeOutput()) {
                    m_directSwitcher.setOutput(workspace()->activeOutput());
                }

                // Start the switcher
                m_directSwitcher.show(mode);
                m_switcherActive = true;
                m_grabActive = true;

                qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: Switcher activated with mode:" << static_cast<int>(mode);

                // Consume the event to prevent it from being processed elsewhere
                return true;
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
                return true; // Consume the event
            } else if (event->key == Qt::Key_Escape) {
                m_directSwitcher.hide();
                m_switcherActive = false;
                m_grabActive = false;
                qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: Switcher deactivated (Escape)";
                return true; // Consume the event
            } else if (event->key == Qt::Key_Return || event->key == Qt::Key_Enter || event->key == Qt::Key_Space) {
                m_directSwitcher.accept();
                m_switcherActive = false;
                m_grabActive = false;
                qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: Switcher accepted and deactivated";
                return true; // Consume the event
            }
        } else if (event->state == KeyboardKeyState::Released) {
            // Check if the modifier keys are released to close the switcher
            if (!areModKeysDepressed(m_cutWalkThroughWindows) && !areModKeysDepressed(m_cutWalkThroughCurrentAppWindows) && !areModKeysDepressed(m_cutWalkThroughWindowsAlternative) && !areModKeysDepressed(m_cutWalkThroughCurrentAppWindowsAlternative)) {

                // Only close if we're not consuming other keys (like Tab)
                if (event->key != Qt::Key_Tab && event->key != Qt::Key_Shift) {
                    m_directSwitcher.accept(); // Accept current selection
                    m_switcherActive = false;
                    m_grabActive = false;
                    qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: Switcher closed by releasing modifiers";
                }
            }
        }
    }

    // If switcher is active, consume most keyboard events to maintain focus
    if (m_switcherActive) {
        // Don't consume modifier key releases as they're used to close the switcher
        if (event->key != Qt::Key_Alt && event->key != Qt::Key_Meta && event->key != Qt::Key_Control && event->key != Qt::Key_Shift) {
            return true; // Consume the event
        }
    }

    return false; // Don't consume the event, let others handle it
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
        qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: navigateThroughWindows started switcher with mode:" << static_cast<int>(mode);
    } else {
        // Navigate within the existing switcher
        if (forward) {
            m_directSwitcher.selectNext();
            qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: navigateThroughWindows selecting next";
        } else {
            m_directSwitcher.selectPrevious();
            qCDebug(KWIN_CORE) << "DirectSwitcherInputFilter: navigateThroughWindows selecting previous";
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

bool DirectSwitcherInputFilter::pointerMotion(PointerMotionEvent *event)
{
    Q_UNUSED(event);
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::pointerButton(PointerButtonEvent *event)
{
    Q_UNUSED(event);
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::pointerFrame()
{
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::pointerAxis(PointerAxisEvent *event)
{
    Q_UNUSED(event);
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::touchDown(qint32 id, const QPointF &pos, std::chrono::microseconds time)
{
    Q_UNUSED(id);
    Q_UNUSED(pos);
    Q_UNUSED(time);
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::touchMotion(qint32 id, const QPointF &pos, std::chrono::microseconds time)
{
    Q_UNUSED(id);
    Q_UNUSED(pos);
    Q_UNUSED(time);
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::touchUp(qint32 id, std::chrono::microseconds time)
{
    Q_UNUSED(id);
    Q_UNUSED(time);
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::touchCancel()
{
    // This filter only handles keyboard events for switcher
    return false;
}

bool DirectSwitcherInputFilter::touchFrame()
{
    // This filter only handles keyboard events for switcher
    return false;
}

} // namespace KWin