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

#include "direct_window_list.h"

namespace KWin
{

class Window;
class Output;
class SurfaceItem;

/**
 * A fast, direct switcher implementation that bypasses Qt Model/View and QML.
 * This is implemented as a compositor primitive for maximum performance.
 */
class DirectSwitcher : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Windows,
        WindowsAlternative,
        CurrentAppWindows,
        CurrentAppWindowsAlternative
    };

    explicit DirectSwitcher(QObject *parent = nullptr);
    ~DirectSwitcher() override;

    /**
     * Shows the switcher with the current window list.
     */
    void show(Mode mode = Mode::Windows);

    /**
     * Hides the switcher.
     */
    void hide();

    /**
     * Moves selection to the next window.
     */
    void selectNext();

    /**
     * Moves selection to the previous window.
     */
    void selectPrevious();

    /**
     * Accepts the current selection and activates the window.
     */
    void accept();

    /**
     * Checks if the switcher is currently visible.
     */
    bool isVisible() const;

    /**
     * Sets the output where the switcher should appear.
     */
    void setOutput(Output *output);

    /**
     * Gets the currently selected window.
     */
    Window *currentSelection() const;

    /**
     * Sets the configuration parameters for the switcher layout
     */
    void setThumbnailWidth(int width);
    void setPadding(int padding);
    void setSwitcherScreenCoverage(double coverage);

    /**
     * Gets the current configuration parameters
     */
    int thumbnailWidth() const;
    int padding() const;
    double switcherScreenCoverage() const;

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void selectionChanged(Window *window);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace KWin