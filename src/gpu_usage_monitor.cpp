/*
    SPDX-FileCopyrightText: 2023 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gpu_usage_monitor.h"
#include "opengl/glutils.h"
#include "tabbox/tabboxconfig.h"

#include <QCoreApplication>
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent>

namespace KWin
{

GpuUsageMonitor::GpuUsageMonitor(QObject *parent)
    : QObject(parent)
{
    // Initialize with safe defaults - assume low-end GPU
    m_cachedInfo.availableVramMB = 1000; // Conservative default
    m_cachedInfo.totalVramMB = 2048;
    m_cachedInfo.gpuUtilization = 0;
    m_cachedInfo.isValid = true; // Mark as valid to avoid repeated queries
    m_lastQuery = std::chrono::steady_clock::now();

    // Start background query AFTER construction completes
    QTimer::singleShot(0, this, [this]() {
        startBackgroundQuery();
    });
}

GpuUsageMonitor::~GpuUsageMonitor()
{
    // Destructor will automatically clean up the unique_ptr
}

void GpuUsageMonitor::startBackgroundQuery()
{
    // Use QtConcurrent to run query in background thread pool
    QFuture<GpuInfo> future = QtConcurrent::run([this]() {
        return queryGpuState();
    });

    // Connect watcher to update cached info when done
    QFutureWatcher<GpuInfo> *watcher = new QFutureWatcher<GpuInfo>(this);
    connect(watcher, &QFutureWatcher<GpuInfo>::finished, this, [this, watcher]() {
        QMutexLocker locker(&m_cacheMutex);
        m_cachedInfo = watcher->result();
        m_lastQuery = std::chrono::steady_clock::now();
        watcher->deleteLater();

        qDebug() << "[GPU MONITOR] Updated: VRAM" << m_cachedInfo.availableVramMB
                 << "MB available, GPU util" << m_cachedInfo.gpuUtilization << "%";
    });
    watcher->setFuture(future);
}

int GpuUsageMonitor::getVramThresholdMB() const
{
    // Use the threshold from base config
    return m_baseConfig.vramThresholdMB();
}

GpuUsageMonitor::GpuInfo GpuUsageMonitor::queryGpuState() const
{
    qDebug() << "[GPU MONITOR] Starting query...";

    GpuInfo info;
    info.isValid = false;
    info.availableVramMB = 1000; // Safe default
    info.totalVramMB = 2048;
    info.gpuUtilization = 0;

    // **CRITICAL: Use QProcess with event loop, not waitForFinished**
    // This prevents UI freezes

    QProcess *gpuProc = new QProcess();
    gpuProc->setProgram("nvidia-smi");
    gpuProc->setArguments({"--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"});

    // Set up timeout to kill hung processes
    QTimer *timeout = new QTimer();
    timeout->setSingleShot(true);
    timeout->setInterval(200); // 200ms max

    bool finished = false;
    QString output;

    connect(gpuProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [&](int exitCode, QProcess::ExitStatus status) {
        if (exitCode == 0 && status == QProcess::NormalExit) {
            output = gpuProc->readAllStandardOutput().trimmed();
        }
        finished = true;
        timeout->stop();
    });

    connect(timeout, &QTimer::timeout, [&]() {
        gpuProc->kill();
        finished = true;
    });

    gpuProc->start();
    timeout->start();

    // **SAFE EVENT LOOP** - process events but don't block main UI
    while (!finished) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
        QThread::msleep(1); // Yield to other threads
    }

    if (!output.isEmpty()) {
        bool ok;
        int util = output.toInt(&ok);
        if (ok) {
            info.gpuUtilization = qBound(0, util, 100);
        }
    }

    delete gpuProc;
    delete timeout;

    // Try OpenGL for VRAM (faster than nvidia-smi)
    if (tryGetVramFromGL(info)) {
        info.isValid = true;
        qDebug() << "[GPU MONITOR] Query complete: VRAM" << info.availableVramMB << "MB";
        return info;
    }

    // Fallback to nvidia-smi for VRAM (with same timeout pattern)
    if (!tryGetVramFromNvidiaSmi(info)) {
        // Use conservative defaults if all queries fail
        info.availableVramMB = 1000;
        info.totalVramMB = 2048;
    }

    info.isValid = true;

    qDebug() << "[GPU MONITOR] Query complete: VRAM" << info.availableVramMB << "MB";
    return info;
}

bool GpuUsageMonitor::tryGetVramFromGL(GpuInfo &info) const
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        return false;
    }

    QOpenGLFunctions *funcs = ctx->functions();
    if (!funcs) {
        return false;
    }

    // Try NVIDIA extension (fastest)
    const char *extensions = reinterpret_cast<const char *>(funcs->glGetString(GL_EXTENSIONS));
    if (!extensions) {
        return false;
    }

    QString extStr = QString::fromLatin1(extensions);

    if (extStr.contains("NVX_gpu_memory_info")) {
        GLint totalMemKb = 0;
        GLint curAvailMemKb = 0;

        funcs->glGetIntegerv(0x9048, &totalMemKb); // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
        funcs->glGetIntegerv(0x9049, &curAvailMemKb); // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX

        if (totalMemKb > 0) {
            info.totalVramMB = totalMemKb / 1024;
            info.availableVramMB = curAvailMemKb / 1024;
            return true;
        }
    }

    // Try AMD extension
    if (extStr.contains("ATI_meminfo")) {
        GLint memInfo[4] = {0};
        funcs->glGetIntegerv(0x87FC, memInfo); // GL_VBO_FREE_MEMORY_ATI

        if (memInfo[0] > 0) {
            info.availableVramMB = memInfo[0] / 1024;
            info.totalVramMB = info.availableVramMB * 2; // Estimate
            return true;
        }
    }

    return false;
}

bool GpuUsageMonitor::tryGetVramFromNvidiaSmi(GpuInfo &info) const
{
    // Similar non-blocking pattern as above
    QProcess *proc = new QProcess();
    proc->setProgram("nvidia-smi");
    proc->setArguments({"--query-gpu=memory.total,memory.free", "--format=csv,noheader,nounits"});

    QTimer *timeout = new QTimer();
    timeout->setSingleShot(true);
    timeout->setInterval(200);

    bool finished = false;
    QString output;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [&](int exitCode, QProcess::ExitStatus status) {
        if (exitCode == 0 && status == QProcess::NormalExit) {
            output = proc->readAllStandardOutput().trimmed();
        }
        finished = true;
        timeout->stop();
    });

    connect(timeout, &QTimer::timeout, [&]() {
        proc->kill();
        finished = true;
    });

    proc->start();
    timeout->start();

    while (!finished) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
        QThread::msleep(1);
    }

    delete timeout;

    if (!output.isEmpty()) {
        QStringList values = output.split(',');
        if (values.size() >= 2) {
            bool totalOk, freeOk;
            int total = values[0].trimmed().toInt(&totalOk);
            int free = values[1].trimmed().toInt(&freeOk);

            if (totalOk && freeOk) {
                info.totalVramMB = total;
                info.availableVramMB = free;
                delete proc;
                return true;
            }
        }
    }

    delete proc;
    return false;
}

bool GpuUsageMonitor::shouldUseThumbnails() const
{
    QMutexLocker locker(&m_cacheMutex);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastQuery).count();

    // Update cache in background if stale (>5 seconds, not 2 to reduce overhead)
    if (elapsed > 5000) {
        // Schedule background update WITHOUT blocking
        // Use QTimer to defer to next event loop iteration
        QTimer::singleShot(0, const_cast<GpuUsageMonitor *>(this), [this]() {
            const_cast<GpuUsageMonitor *>(this)->startBackgroundQuery();
        });
    }

    // Use cached value immediately (never block)
    switch (m_baseConfig.switcherMode()) {
    case TabBox::TabBoxConfig::Vram:
        return m_cachedInfo.availableVramMB >= getVramThresholdMB();

    case TabBox::TabBoxConfig::Gpu:
        return m_cachedInfo.gpuUtilization < m_baseConfig.gpuThreshold();

    case TabBox::TabBoxConfig::GpuOrVram:
        return (m_cachedInfo.availableVramMB >= getVramThresholdMB())
            && (m_cachedInfo.gpuUtilization < m_baseConfig.gpuThreshold());

    case TabBox::TabBoxConfig::Auto:
    case TabBox::TabBoxConfig::Thumbnail:
        return m_cachedInfo.availableVramMB >= getVramThresholdMB();

    case TabBox::TabBoxConfig::Compact:
        return false;
    }

    return true; // Safe default
}

TabBox::TabBoxConfig GpuUsageMonitor::getOptimalConfig() const
{
    TabBox::TabBoxConfig config = m_baseConfig;

    if (!shouldUseThumbnails()) {
        // Use low VRAM layout when GPU conditions indicate low resources
        config.setLayoutName(m_baseConfig.lowVramLayout());
        qDebug() << "[GPU MONITOR] Switching to low VRAM layout:" << m_baseConfig.lowVramLayout();
    } else {
        qDebug() << "[GPU MONITOR] Using normal layout:" << m_baseConfig.layoutName();
    }

    return config;
}

void GpuUsageMonitor::setBaseConfig(const TabBox::TabBoxConfig &config)
{
    m_baseConfig = config;
}

} // namespace KWin