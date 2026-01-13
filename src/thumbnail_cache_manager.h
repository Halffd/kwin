#ifndef KWIN_THUMBNAIL_CACHE_MANAGER_H
#define KWIN_THUMBNAIL_CACHE_MANAGER_H

#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

namespace KWin
{

class Window;
class WindowThumbnailSource;

/**
 * Manages thumbnail pre-rendering and caching for fast Alt+Tab display
 */
class ThumbnailCacheManager : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCacheManager(QObject *parent = nullptr);
    ~ThumbnailCacheManager() override;

    // Pre-render thumbnails for the most likely windows
    void warmupCache(const QList<Window *> &windows);

    // Get a pre-rendered thumbnail (or trigger render if not cached)
    WindowThumbnailSource *getThumbnail(Window *window);

    // Clear cache (e.g., on screen resolution change)
    void clearCache();

    // Enable/disable background pre-rendering
    void setPreRenderingEnabled(bool enabled);

private Q_SLOTS:
    void onWindowClosed(Window *window);
    void renderNextThumbnail();

private:
    struct CacheEntry
    {
        WindowThumbnailSource *source = nullptr;
        bool isRendered = false;
        qint64 lastAccessTime = 0;
    };

    QHash<Window *, CacheEntry> m_cache;
    QList<Window *> m_renderQueue;
    QTimer *m_renderTimer;
    bool m_preRenderingEnabled = true;

    static constexpr int MAX_CACHE_SIZE = 50;
    static constexpr int RENDER_INTERVAL_MS = 50; // 20 FPS background rendering

    void pruneCache();
};

} // namespace KWin

#endif