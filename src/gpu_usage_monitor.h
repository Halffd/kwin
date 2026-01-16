/*
    SPDX-FileCopyrightText: 2023 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMutex>
#include <QObject>
#include <chrono>

#include "tabbox/tabboxconfig.h"

namespace KWin
{

class GpuUsageMonitor : public QObject
{
    Q_OBJECT

public:
    explicit GpuUsageMonitor(QObject *parent = nullptr);
    ~GpuUsageMonitor() override;

    // Check if we should use thumbnails based on current GPU state
    bool shouldUseThumbnails() const;

    // Get optimal config based on GPU capabilities
    TabBox::TabBoxConfig getOptimalConfig() const;

    // Set base config to modify
    void setBaseConfig(const TabBox::TabBoxConfig &config);

private:
    struct GpuInfo
    {
        int availableVramMB = 0;
        int totalVramMB = 0;
        bool isValid = false;
    };

    mutable QMutex m_cacheMutex;
    GpuInfo m_cachedInfo;
    std::chrono::steady_clock::time_point m_lastQuery;
    TabBox::TabBoxConfig m_baseConfig;

    GpuInfo queryGpuState() const;
    int getVramThresholdMB() const;
};

} // namespace KWin