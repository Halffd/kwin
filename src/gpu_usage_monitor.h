/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 The KWin Team <kwin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_GPU_USAGE_MONITOR_H
#define KWIN_GPU_USAGE_MONITOR_H

#include "tabbox/tabboxconfig.h"

#include <QObject>
#include <QTimer>
#include <memory>

namespace KWin
{

class GpuUsageMonitor : public QObject
{
    Q_OBJECT

public:
    explicit GpuUsageMonitor(QObject *parent = nullptr);
    ~GpuUsageMonitor() override;

    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const;

    // Get current GPU usage percentage (0-100)
    int currentGpuUsage() const;

    void setTabBoxConfig(const TabBox::TabBoxConfig &config);
    TabBox::TabBoxConfig currentTabBoxConfig() const;

Q_SIGNALS:
    void gpuUsageChanged(int usage);
    void tabBoxConfigChanged(const KWin::TabBox::TabBoxConfig &config);

private Q_SLOTS:
    void updateGpuUsage();

private:
    enum class State {
        Normal, // Low GPU usage mode (thumbnail grid, highlight disabled)
        HighUsage // High GPU usage mode (big icons, highlight enabled)
    };

    void setupTimer();
    void setupHysteresisTimers();
    int parseGpuUsage(const QString &output) const;
    void updateHysteresisState(int gpuUsage);
    void switchToHighUsageMode();
    void switchToLowUsageMode();

    QTimer *m_timer;
    int m_currentGpuUsage;
    bool m_isMonitoring;
    TabBox::TabBoxConfig m_currentConfig;
    TabBox::TabBoxConfig m_highUsageConfig;
    TabBox::TabBoxConfig m_lowUsageConfig;

    // Hysteresis timers to prevent flickering
    QTimer *m_stableHighUsageTimer;
    QTimer *m_stableLowUsageTimer;
    State m_currentState;

    // Temporary storage for new reading during update
    int m_newGpuUsage;
    bool m_isUpdating;
};

} // namespace KWin

#endif // KWIN_GPU_USAGE_MONITOR_H