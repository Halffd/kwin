/*
    SPDX-FileCopyrightText: 2023 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpu_usage_monitor.h"
#include "opengl/glutils.h"
#include "tabbox/tabboxconfig.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcess>
#include <QRegularExpression>

namespace KWin
{

GpuUsageMonitor::GpuUsageMonitor(QObject *parent)
    : QObject(parent)
{
    // Initialize with default values
    m_cachedInfo.availableVramMB = 0;
    m_cachedInfo.totalVramMB = 0;
    m_cachedInfo.isValid = false;
    m_lastQuery = std::chrono::steady_clock::now();
}

GpuUsageMonitor::~GpuUsageMonitor()
{
}

int GpuUsageMonitor::getVramThresholdMB() const
{
    // Use the threshold from base config, default to 300MB
    return m_baseConfig.vramThresholdMB() > 0 ? m_baseConfig.vramThresholdMB() : 300;
}

GpuUsageMonitor::GpuInfo GpuUsageMonitor::queryGpuState() const
{
    GpuInfo info;
    info.isValid = false;
    info.availableVramMB = 0;
    info.totalVramMB = 0;

    // Try to get VRAM info from OpenGL
    if (QOpenGLContext *ctx = QOpenGLContext::currentContext()) {
        QOpenGLFunctions *funcs = ctx->functions();
        if (funcs) {
            // Try to get vendor-specific extensions for VRAM info
            QString extensions = QString::fromLocal8Bit(
                reinterpret_cast<const char *>(funcs->glGetString(GL_EXTENSIONS)));

            // For NVIDIA cards, try to get VRAM info
            if (extensions.contains(QLatin1String("NVX_gpu_memory_info"))) {
                GLint totalMemKb = 0;
                GLint curAvailMemKb = 0;

                funcs->glGetIntegerv(0x9048 /*GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX*/, &totalMemKb);
                funcs->glGetIntegerv(0x9049 /*GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX*/, &curAvailMemKb);

                if (totalMemKb > 0) {
                    info.totalVramMB = totalMemKb / 1024;
                    info.availableVramMB = curAvailMemKb / 1024;
                    info.isValid = true;
                }
            }
            // For AMD/ATI cards, try to get VRAM info
            else if (extensions.contains(QLatin1String("ATI_meminfo"))) {
                GLint memInfo[4] = {0};
                funcs->glGetIntegerv(0x87FC /*GL_VBO_FREE_MEMORY_ATI*/, memInfo);

                if (memInfo[0] > 0) {
                    info.totalVramMB = 0; // ATI doesn't provide total, just available
                    info.availableVramMB = memInfo[0] / 1024;
                    info.isValid = true;
                }
            }
        }
    }

    // If OpenGL didn't provide info, try system commands as fallback
    if (!info.isValid) {
        // Try parsing from system files or commands
        QProcess proc;
        proc.start("nvidia-smi", QStringList() << "--query-gpu=memory.total,memory.free" << "--format=csv,noheader,nounits");
        proc.waitForFinished(1000); // Wait up to 1 second

        if (proc.exitCode() == 0) {
            QString output = proc.readAllStandardOutput();
            QStringList lines = output.split('\n');
            if (!lines.isEmpty() && !lines[0].trimmed().isEmpty()) {
                QStringList values = lines[0].split(',');
                if (values.size() >= 2) {
                    bool totalOk = false, freeOk = false;
                    int total = values[0].trimmed().toInt(&totalOk);
                    int free = values[1].trimmed().toInt(&freeOk);

                    if (totalOk && freeOk) {
                        info.totalVramMB = total;
                        info.availableVramMB = free;
                        info.isValid = true;
                    }
                }
            }
        }
    }

    // If still no info, try to get from lspci
    if (!info.isValid) {
        QProcess proc;
        proc.start("lspci", QStringList() << "-v");
        proc.waitForFinished(1000);

        if (proc.exitCode() == 0) {
            QString output = proc.readAllStandardOutput();
            QRegularExpression vramRegex(R"(VRAM=[\d]+M|Memory at [^ ]+ \(.* ([\d]+)([MG])\)|\[size=([0-9]+)([MG])\])");
            QRegularExpressionMatch match = vramRegex.match(output);

            if (match.hasMatch()) {
                // Extract VRAM size - this is a simplified approach
                // In practice, you'd need more sophisticated parsing
                QRegularExpression sizeRegex(R"(([0-9]+)([MG]))");
                QRegularExpressionMatchIterator iter = sizeRegex.globalMatch(output);

                while (iter.hasNext()) {
                    QRegularExpressionMatch sizeMatch = iter.next();
                    QString sizeStr = sizeMatch.captured(1);
                    QString unit = sizeMatch.captured(2);

                    bool ok = false;
                    int size = sizeStr.toInt(&ok);
                    if (ok) {
                        if (unit == QLatin1String("G")) {
                            size *= 1024; // Convert GB to MB
                        }
                        info.totalVramMB = size;
                        info.availableVramMB = size; // Assume all is available initially
                        info.isValid = true;
                        break;
                    }
                }
            }
        }
    }

    // If still no info, use conservative defaults
    if (!info.isValid) {
        info.totalVramMB = 2048; // Assume 2GB as default
        info.availableVramMB = 1500; // Assume 1.5GB available
        info.isValid = true;
    }

    return info;
}

bool GpuUsageMonitor::shouldUseThumbnails() const
{
    QMutexLocker locker(&m_cacheMutex);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastQuery).count();

    // Update cache if stale (older than 1 second)
    if (elapsed > 1000) {
        // Cast away const to update cached values
        auto *nonConstThis = const_cast<GpuUsageMonitor *>(this);
        nonConstThis->m_lastQuery = now;
        nonConstThis->m_cachedInfo = nonConstThis->queryGpuState();
    }

    // If we have valid VRAM info, use it to decide
    if (m_cachedInfo.isValid) {
        int threshold = getVramThresholdMB();
        return m_cachedInfo.availableVramMB >= threshold;
    }

    // Default to true if we can't determine VRAM
    return true;
}

TabBox::TabBoxConfig GpuUsageMonitor::getOptimalConfig() const
{
    TabBox::TabBoxConfig config = m_baseConfig;

    if (!shouldUseThumbnails()) {
        // Use compact layout when VRAM is low
        config.setLayoutName(m_baseConfig.lowVramLayout());
    }

    return config;
}

void GpuUsageMonitor::setBaseConfig(const TabBox::TabBoxConfig &config)
{
    m_baseConfig = config;
}

} // namespace KWin