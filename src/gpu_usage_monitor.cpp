/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 The KWin Team <kwin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpu_usage_monitor.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace KWin
{

GpuUsageMonitor::GpuUsageMonitor(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_currentGpuUsage(0)
    , m_isMonitoring(false)
    , m_stableHighUsageTimer(new QTimer(this))
    , m_stableLowUsageTimer(new QTimer(this))
    , m_currentState(State::Normal)
    , m_newGpuUsage(0)
    , m_isUpdating(false)
{
    setupTimer();
    setupHysteresisTimers();

    // Set up high usage config (big icons, highlight enabled)
    m_highUsageConfig = TabBox::TabBoxConfig();
    m_highUsageConfig.setShowTabBox(true);
    m_highUsageConfig.setHighlightWindows(true);
    m_highUsageConfig.setLayoutName(QStringLiteral("basic_list")); // big icons layout

    // Set up low usage config (thumbnail grid, highlight disabled)
    m_lowUsageConfig = TabBox::TabBoxConfig();
    m_lowUsageConfig.setShowTabBox(true);
    m_lowUsageConfig.setHighlightWindows(false);
    m_lowUsageConfig.setLayoutName(QStringLiteral("thumbnail_grid")); // thumbnail grid layout
}

GpuUsageMonitor::~GpuUsageMonitor()
{
    stopMonitoring();
}

void GpuUsageMonitor::setupTimer()
{
    connect(m_timer, &QTimer::timeout, this, &GpuUsageMonitor::updateGpuUsage);
    m_timer->setInterval(2000); // Check every 2 seconds (not too aggressive)
}

void GpuUsageMonitor::setupHysteresisTimers()
{
    // Timer that triggers when GPU usage stays HIGH (>50%) for a sustained period
    m_stableHighUsageTimer->setSingleShot(true);
    m_stableHighUsageTimer->setInterval(3000); // 3 seconds to switch to high usage mode
    connect(m_stableHighUsageTimer, &QTimer::timeout, this, [this]() {
        if (m_newGpuUsage > 75) { // Use a higher threshold for sustained high
            switchToHighUsageMode();
        }
    });

    // Timer that triggers when GPU usage stays LOW (<=50%) for a sustained period
    m_stableLowUsageTimer->setSingleShot(true);
    m_stableLowUsageTimer->setInterval(5000); // 5 seconds to switch to low usage mode (longer to avoid flickering)
    connect(m_stableLowUsageTimer, &QTimer::timeout, this, [this]() {
        if (m_newGpuUsage <= 40) { // Use a lower threshold for sustained low
            switchToLowUsageMode();
        }
    });
}

void GpuUsageMonitor::startMonitoring()
{
    if (!m_isMonitoring) {
        m_isMonitoring = true;
        m_timer->start();
        updateGpuUsage(); // Initial check
    }
}

void GpuUsageMonitor::stopMonitoring()
{
    if (m_isMonitoring) {
        m_isMonitoring = false;
        m_timer->stop();
        m_stableHighUsageTimer->stop();
        m_stableLowUsageTimer->stop();
    }
}

bool GpuUsageMonitor::isMonitoring() const
{
    return m_isMonitoring;
}

int GpuUsageMonitor::currentGpuUsage() const
{
    return m_currentGpuUsage;
}

void GpuUsageMonitor::updateGpuUsage()
{
    if (m_isUpdating) {
        return; // Skip if already updating to prevent overlapping calls
    }

    m_isUpdating = true;

    // Try different methods to get GPU usage
    QString output;

    // Method 1: Try nvidia-smi for NVIDIA GPUs
    if (QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty()) {
        // For now, we'll simulate GPU usage - in a real implementation,
        // we'd check for other GPU vendors using appropriate tools
        // In a production version, we might also try other tools like:
        // - rocm-smi for AMD GPUs
        // - Intel GPU tools
        // - Reading from /sys/class/drm or other system interfaces
        output = QStringLiteral("0"); // Default to 0% if no tool found
    } else {
        QProcess process;
        process.start(QStringLiteral("nvidia-smi"),
                      QStringList() << QStringLiteral("--query-gpu=utilization.gpu")
                                    << QStringLiteral("--format=csv,noheader,nounits"));
        process.waitForFinished(1000); // Wait up to 1 second

        if (process.exitCode() == 0) {
            output = process.readAllStandardOutput().trimmed();
        } else {
            output = QStringLiteral("0"); // Default to 0% if command failed
        }
    }

    m_newGpuUsage = parseGpuUsage(output);

    // Apply simple hysteresis at the raw reading level to reduce noise
    if (qAbs(m_newGpuUsage - m_currentGpuUsage) > 5) { // Only update if change is significant
        m_currentGpuUsage = m_newGpuUsage;
        Q_EMIT gpuUsageChanged(m_currentGpuUsage);
    }

    // Manage the state transition timers based on current usage
    updateHysteresisState(m_newGpuUsage);

    m_isUpdating = false;
}

void GpuUsageMonitor::updateHysteresisState(int gpuUsage)
{
    // Check if we need to start or stop the hysteresis timers
    if (gpuUsage > 50 && m_currentState != State::HighUsage) {
        // GPU usage is high, but we're not in high usage mode yet
        // Start the high usage timer if not already running
        if (!m_stableHighUsageTimer->isActive()) {
            m_stableHighUsageTimer->start();
        }
        // Stop the low usage timer since we're seeing high usage
        m_stableLowUsageTimer->stop();
    } else if (gpuUsage <= 50 && m_currentState != State::Normal) {
        // GPU usage is low, but we're not in normal (low usage) mode yet
        // Start the low usage timer if not already running
        if (!m_stableLowUsageTimer->isActive()) {
            m_stableLowUsageTimer->start();
        }
        // Stop the high usage timer since we're seeing low usage
        m_stableHighUsageTimer->stop();
    }
}

void GpuUsageMonitor::switchToHighUsageMode()
{
    if (m_currentState != State::HighUsage) {
        m_currentState = State::HighUsage;

        // Use high usage config (big icons, highlight enabled)
        if (m_currentConfig.layoutName() != m_highUsageConfig.layoutName() || m_currentConfig.isHighlightWindows() != m_highUsageConfig.isHighlightWindows()) {
            m_currentConfig = m_highUsageConfig;
            Q_EMIT tabBoxConfigChanged(m_currentConfig);
        }
    }

    // Restart the low usage timer to allow return to normal mode
    m_stableLowUsageTimer->stop();
}

void GpuUsageMonitor::switchToLowUsageMode()
{
    if (m_currentState != State::Normal) {
        m_currentState = State::Normal;

        // Use low usage config (thumbnail grid, highlight disabled)
        if (m_currentConfig.layoutName() != m_lowUsageConfig.layoutName() || m_currentConfig.isHighlightWindows() != m_lowUsageConfig.isHighlightWindows()) {
            m_currentConfig = m_lowUsageConfig;
            Q_EMIT tabBoxConfigChanged(m_currentConfig);
        }
    }

    // Restart the high usage timer to allow return to high usage mode
    m_stableHighUsageTimer->stop();
}

int GpuUsageMonitor::parseGpuUsage(const QString &output) const
{
    if (output.isEmpty()) {
        return 0; // Return 0 if no output
    }

    // Extract numeric value from the output
    QRegularExpression regex(QStringLiteral(R"(\d+)"));
    QRegularExpressionMatch match = regex.match(output);

    if (match.hasMatch()) {
        bool ok;
        int usage = match.captured(0).toInt(&ok);
        if (ok) {
            return qBound(0, usage, 100); // Ensure value is between 0-100
        }
    }

    return 0; // Default to 0 if parsing fails
}

void GpuUsageMonitor::setTabBoxConfig(const TabBox::TabBoxConfig &config)
{
    m_currentConfig = config;
}

TabBox::TabBoxConfig GpuUsageMonitor::currentTabBoxConfig() const
{
    return m_currentConfig;
}

} // namespace KWin