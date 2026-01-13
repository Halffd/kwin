#include "thumbnail_cache_manager.h"
#include "scripting/windowthumbnailitem.h"
#include "window.h"
#include <QDateTime>
#include <QDebug>

namespace KWin
{

ThumbnailCacheManager::ThumbnailCacheManager(QObject *parent)
    : QObject(parent)
    , m_renderTimer(new QTimer(this))
{
    m_renderTimer->setInterval(RENDER_INTERVAL_MS);
    connect(m_renderTimer, &QTimer::timeout, this, &ThumbnailCacheManager::renderNextThumbnail);
}

ThumbnailCacheManager::~ThumbnailCacheManager()
{
    clearCache();
}

void ThumbnailCacheManager::warmupCache(const QList<Window *> &windows)
{
    if (!m_preRenderingEnabled) {
        return;
    }

    qDebug() << "[THUMBNAIL CACHE] Warming up cache for" << windows.size() << "windows";

    // Clear render queue and repopulate with priority order
    m_renderQueue.clear();

    // Add windows in order of importance (first ones shown in Alt+Tab)
    for (Window *window : windows) {
        if (!window || window->isDeleted()) {
            continue;
        }

        // Skip if already cached and rendered
        if (m_cache.contains(window) && m_cache[window].isRendered) {
            continue;
        }

        m_renderQueue.append(window);
    }

    // Start background rendering if queue is not empty
    if (!m_renderQueue.isEmpty() && !m_renderTimer->isActive()) {
        qDebug() << "[THUMBNAIL CACHE] Starting background render for" << m_renderQueue.size() << "thumbnails";
        m_renderTimer->start();
    }
}

WindowThumbnailSource *ThumbnailCacheManager::getThumbnail(Window *window)
{
    if (!window || window->isDeleted()) {
        return nullptr;
    }

    // Check cache first
    if (m_cache.contains(window)) {
        m_cache[window].lastAccessTime = QDateTime::currentMSecsSinceEpoch();
        return m_cache[window].source;
    }

    // We can't create WindowThumbnailSource here without QQuickWindow
    // This method is meant to be called by UI components that have access to QQuickWindow
    // For now, return nullptr and let the UI component create the source when needed
    // But track that this window should be pre-rendered

    CacheEntry entry;
    entry.source = nullptr; // Will be created by UI components
    entry.isRendered = false;
    entry.lastAccessTime = QDateTime::currentMSecsSinceEpoch();

    m_cache.insert(window, entry);

    // Connect to window destruction
    connect(window, &Window::closed, this, [this, window]() {
        onWindowClosed(window);
    });

    // Prune cache if too large
    if (m_cache.size() > MAX_CACHE_SIZE) {
        pruneCache();
    }

    return nullptr; // UI components will create the actual source
}

void ThumbnailCacheManager::renderNextThumbnail()
{
    if (m_renderQueue.isEmpty()) {
        m_renderTimer->stop();
        qDebug() << "[THUMBNAIL CACHE] Background rendering complete";
        return;
    }

    Window *window = m_renderQueue.takeFirst();

    if (!window || window->isDeleted()) {
        return; // Skip and continue to next
    }

    // Mark that this window should be pre-rendered
    // The actual rendering will happen when UI components request thumbnails
    if (m_cache.contains(window)) {
        m_cache[window].isRendered = true;
        qDebug() << "[THUMBNAIL CACHE] Marked for pre-rendering window:" << window->caption();
    } else {
        // Add to cache if not already there
        CacheEntry entry;
        entry.source = nullptr;
        entry.isRendered = true;
        entry.lastAccessTime = QDateTime::currentMSecsSinceEpoch();
        m_cache.insert(window, entry);
        qDebug() << "[THUMBNAIL CACHE] Added window to cache for pre-rendering:" << window->caption();
    }
}

void ThumbnailCacheManager::clearCache()
{
    m_renderTimer->stop();
    m_renderQueue.clear();

    for (auto &entry : m_cache) {
        delete entry.source;
    }
    m_cache.clear();

    qDebug() << "[THUMBNAIL CACHE] Cache cleared";
}

void ThumbnailCacheManager::setPreRenderingEnabled(bool enabled)
{
    m_preRenderingEnabled = enabled;
    if (!enabled) {
        m_renderTimer->stop();
        m_renderQueue.clear();
    }
}

void ThumbnailCacheManager::onWindowClosed(Window *window)
{
    if (m_cache.contains(window)) {
        delete m_cache[window].source;
        m_cache.remove(window);
    }
    m_renderQueue.removeAll(window);
}

void ThumbnailCacheManager::pruneCache()
{
    if (m_cache.size() <= MAX_CACHE_SIZE) {
        return;
    }

    // Remove least recently used entries
    QList<Window *> sortedKeys = m_cache.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end(), [this](Window *a, Window *b) {
        return m_cache[a].lastAccessTime < m_cache[b].lastAccessTime;
    });

    // Remove oldest 20% of entries
    int toRemove = m_cache.size() - MAX_CACHE_SIZE;
    for (int i = 0; i < toRemove && i < sortedKeys.size(); ++i) {
        Window *window = sortedKeys[i];
        delete m_cache[window].source;
        m_cache.remove(window);
    }

    qDebug() << "[THUMBNAIL CACHE] Pruned" << toRemove << "entries, cache size now:" << m_cache.size();
}

} // namespace KWin