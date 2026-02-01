/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 Joseph <author@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QDebug>
#include <QString>
#include <chrono>

namespace KWin
{

/**
 * @brief Simple RAII-style performance timer for measuring execution time.
 *
 * This class provides a convenient way to measure the execution time of code blocks.
 * It starts a timer when constructed and logs the elapsed time when destroyed.
 *
 * Usage:
 * ```
 * {
 *     PerformanceTimer timer("Operation name");
 *     // Code to measure
 * } // Timer automatically logs elapsed time when destroyed
 * ```
 */
class KWIN_EXPORT PerformanceTimer
{
public:
    /**
     * @brief Construct a new Performance Timer object
     *
     * @param operation Name of the operation being timed
     * @param logLevel Logging level (0=debug, 1=info, 2=warning)
     */
    explicit PerformanceTimer(const QString &operation, int logLevel = 0)
        : m_operation(operation)
        , m_logLevel(logLevel)
        , m_startTime(std::chrono::high_resolution_clock::now())
    {
    }

    /**
     * @brief Destroy the Performance Timer object and log elapsed time
     */
    ~PerformanceTimer()
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);

        QString message = QStringLiteral("Performance: %1 took %2 microseconds").arg(m_operation).arg(duration.count());

        switch (m_logLevel) {
        case 0:
            qDebug() << message;
            break;
        case 1:
            qInfo() << message;
            break;
        case 2:
            qWarning() << message;
            break;
        default:
            qDebug() << message;
            break;
        }
    }

private:
    QString m_operation;
    int m_logLevel;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
};

} // namespace KWin
