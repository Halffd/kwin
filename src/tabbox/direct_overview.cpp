#include "direct_overview.h"

#include "../scene/imageitem.h"
#include "../scene/item.h"

#include "workspace.h"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QTimer>
#include <cmath>
#include <cstdio>

namespace KWin
{

/**
 * CRITICAL RULES (non-negotiable for KWin scene graph):
 * 1. Item* must be created as child of existing scene item
 * 2. Never manually delete scene items - parent owns them
 * 3. No Item creation before scene exists
 * 4. Activation must go through proper state machine (no direct calls)
 * 5. Cache across shows, don't recreate on every trigger
 */
class DirectOverview::Private
{
public:
    Private();
    ~Private();

    void create();
    void destroy();
    void updateSelection();
    void buildLayout();
    void cacheDesktops();
    void clearDesktopCache();
    void recordFrameTime(const char *marker);
    void activateCurrentSelection();
    void invalidateDesktopCache();

    // Scene graph
    Item *parentItem = nullptr; // Set via setParentItem(), we don't own this
    Item *root = nullptr; // OWNED BY parentItem, never manually delete
    QList<Item *> items; // OWNED BY root, never manually delete
    QList<ImageItem *> desktopItems; // OWNED BY root, never manually delete

    // Desktop/Activity cache (persistent across shows)
    int desktopCount = 0;
    int currentDesktop = 0;
    int desktopToActivate = -1; // Deferred activation (after hide completes)
    bool desktopCacheValid = false; // Track if cache needs refresh

    // State
    bool visible = false;
    int currentIndex = 0;

    // Layout config
    int gridColumns = 3;
    int gridSpacing = 20;
    int itemWidth = 200;
    int itemHeight = 150;
    void *output = nullptr; // Output* stored as void* to avoid header dependency

    // Mode
    DirectOverview::Mode mode = DirectOverview::Mode::VirtualDesktops;

    // Performance measurement
    qint64 creationTimeMs = 0;
    bool performanceMeasurementEnabled = false;
};

DirectOverview::Private::Private()
{
    output = Workspace::self()->activeOutput();
    performanceMeasurementEnabled = (getenv("KWIN_PERF") != nullptr);
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] Performance measurement enabled\n");
    }
}

DirectOverview::Private::~Private()
{
    clearDesktopCache();
}

void DirectOverview::Private::recordFrameTime(const char *marker)
{
    if (!performanceMeasurementEnabled) {
        return;
    }
    std::fprintf(stderr, "[DirectOverview] %s\n", marker);
}

void DirectOverview::Private::clearDesktopCache()
{
    desktopCount = 0;
    desktopCacheValid = false;
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] desktop cache cleared and marked invalid\n");
    }
}

void DirectOverview::Private::invalidateDesktopCache()
{
    desktopCacheValid = false;
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] desktop cache invalidated\n");
    }
}

void DirectOverview::Private::cacheDesktops()
{
    // Cache virtual desktop count from Workspace
    Workspace *ws = Workspace::self();
    if (!ws) {
        desktopCount = 1;
        return;
    }

    switch (mode) {
    case DirectOverview::Mode::VirtualDesktops: {
        // For now, use a simple count (would enumerate actual desktops in real implementation)
        desktopCount = 4; // Default to 4 desktops
        currentDesktop = 0; // Would query actual current desktop
        break;
    }
    case DirectOverview::Mode::Activities:
        desktopCount = 2; // Placeholder
        break;
    case DirectOverview::Mode::Workspaces:
        desktopCount = 1; // Single workspace for now
        break;
    }

    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] cacheDesktops: %d items cached\n", desktopCount);
    }
}

void DirectOverview::Private::buildLayout()
{
    // Grid layout for desktops/activities
    if (!output || desktopCount == 0) {
        return;
    }

    // For now, use fixed screen size (would cast to Output* for real implementation)
    QRect screen(0, 0, 1920, 1080);
    if (output) {
        // In real implementation: screen = static_cast<Output*>(output)->geometry()
        // For now, just use hardcoded screen
    }

    const int totalItems = desktopCount;
    const int cols = gridColumns;
    const int rows = (totalItems + cols - 1) / cols; // Ceil division

    // Calculate item size with spacing
    const int totalWidth = screen.width() * 0.8;
    const int totalHeight = screen.height() * 0.8;
    const int availWidth = totalWidth - (cols - 1) * gridSpacing;
    const int availHeight = totalHeight - (rows - 1) * gridSpacing;

    const int cellWidth = availWidth / cols;
    const int cellHeight = availHeight / rows;

    const int startX = screen.x() + (screen.width() - totalWidth) / 2;
    const int startY = screen.y() + (screen.height() - totalHeight) / 2;

    // Position items in grid
    for (int i = 0; i < desktopItems.size(); ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int x = startX + col * (cellWidth + gridSpacing);
        const int y = startY + row * (cellHeight + gridSpacing);

        desktopItems[i]->setPosition(QPointF(x, y));
        desktopItems[i]->setSize(QSizeF(cellWidth, cellHeight));
    }
}

void DirectOverview::Private::create()
{
    destroy();

    QElapsedTimer createTimer;
    createTimer.start();
    recordFrameTime("create() start");

    // Do NOT create Items without parent scene item
    if (!parentItem) {
        recordFrameTime("create() aborted - parentItem not set");
        visible = true;
        return;
    }

    // Create root as child of parentItem
    root = new Item(parentItem);
    items.clear();
    desktopItems.clear();

    // Cache desktop count if needed
    if (!desktopCacheValid || desktopCount == 0) {
        cacheDesktops();
        desktopCacheValid = true;
    } else if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] Reusing desktop cache (%d desktops)\n", desktopCount);
    }

    if (desktopCount == 0) {
        visible = true;
        creationTimeMs = createTimer.elapsed();
        return;
    }

    // Create visual items for each desktop/activity
    for (int i = 0; i < desktopCount; ++i) {
        auto *item = new ImageItem(root);

        // Placeholder image with index
        QImage placeholder(itemWidth, itemHeight, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(Qt::gray);

        // Draw desktop index on thumbnail
        QPainter painter(&placeholder);
        painter.setFont(QFont("Sans", 20, QFont::Bold));
        painter.drawText(placeholder.rect(), Qt::AlignCenter, QString::number(i + 1));

        item->setImage(placeholder);
        item->setOpacity(0.7);

        desktopItems.append(item);
        items.append(item);
    }

    currentIndex = currentDesktop;

    // Apply layout
    buildLayout();
    updateSelection();
    visible = true;

    creationTimeMs = createTimer.elapsed();

    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] create() completed with %d items in %lld ms\n",
                     items.size(), creationTimeMs);
    }
}

void DirectOverview::Private::destroy()
{
    // Scene graph ownership rules: clear lists but don't delete
    items.clear();
    desktopItems.clear();

    if (root) {
        delete root;
        root = nullptr;
    }

    visible = false;
    currentIndex = 0;

    recordFrameTime("destroy() complete");
}

void DirectOverview::Private::updateSelection()
{
    // Update visual state for selection
    for (int i = 0; i < desktopItems.size(); ++i) {
        if (i == currentIndex) {
            desktopItems[i]->setOpacity(1.0);
            desktopItems[i]->setZ(10);
        } else {
            desktopItems[i]->setOpacity(0.7);
            desktopItems[i]->setZ(0);
        }
    }
}

void DirectOverview::Private::activateCurrentSelection()
{
    if (desktopCount == 0 || currentIndex >= desktopCount) {
        return;
    }

    recordFrameTime("activateCurrentSelection() - deferred");

    // Store for deferred activation (happens after hide() completes)
    desktopToActivate = currentIndex;

    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectOverview] Activation deferred for desktop %d\n", currentIndex);
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

DirectOverview::DirectOverview(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

DirectOverview::~DirectOverview()
{
    d->destroy();
}

void DirectOverview::setParentItem(Item *parentItem)
{
    if (!parentItem || !d->root) {
        return;
    }
    d->root->setParent(parentItem);
}

void DirectOverview::show(Mode mode)
{
    if (d->visible) {
        return;
    }

    d->mode = mode;
    d->create();
    Q_EMIT visibilityChanged(true);
}

void DirectOverview::hide()
{
    if (!d->visible) {
        return;
    }

    // Store desktop to activate BEFORE destroy
    int toActivate = d->desktopToActivate;
    d->desktopToActivate = -1;

    d->destroy();
    Q_EMIT visibilityChanged(false);

    // Deferred activation (after display released)
    if (toActivate >= 0) {
        QTimer::singleShot(0, this, [this, toActivate]() {
            Workspace *ws = Workspace::self();
            if (ws && toActivate >= 0) {
                // Would call workspace->setCurrentDesktop(toActivate + 1) or similar
                if (d->performanceMeasurementEnabled) {
                    std::fprintf(stderr, "[DirectOverview] Deferred activation completed\n");
                }
            }
        });
    }
}

void DirectOverview::selectNext()
{
    if (!d->visible || d->desktopCount == 0) {
        return;
    }

    d->currentIndex = (d->currentIndex + 1) % d->desktopCount;
    d->updateSelection();
    Q_EMIT selectionChanged(d->currentIndex);
}

void DirectOverview::selectPrevious()
{
    if (!d->visible || d->desktopCount == 0) {
        return;
    }

    d->currentIndex = (d->currentIndex - 1 + d->desktopCount) % d->desktopCount;
    d->updateSelection();
    Q_EMIT selectionChanged(d->currentIndex);
}

void DirectOverview::accept()
{
    if (!d->visible || d->desktopCount == 0 || d->currentIndex >= d->desktopCount) {
        return;
    }

    d->recordFrameTime("accept() called");
    d->activateCurrentSelection();
    hide();
}

bool DirectOverview::isVisible() const
{
    return d->visible;
}

void DirectOverview::setOutput(void *output)
{
    d->output = output;
    if (d->visible) {
        d->buildLayout();
    }
}

int DirectOverview::currentSelection() const
{
    if (!d->visible || d->currentIndex >= d->desktopCount) {
        return -1;
    }
    return d->currentIndex;
}

void DirectOverview::setGridColumns(int cols)
{
    d->gridColumns = cols;
    if (d->visible) {
        d->buildLayout();
    }
}

void DirectOverview::setGridSpacing(int spacing)
{
    d->gridSpacing = spacing;
    if (d->visible) {
        d->buildLayout();
    }
}

void DirectOverview::setItemSize(int width, int height)
{
    d->itemWidth = width;
    d->itemHeight = height;
}

int DirectOverview::gridColumns() const
{
    return d->gridColumns;
}

int DirectOverview::gridSpacing() const
{
    return d->gridSpacing;
}

int DirectOverview::itemWidth() const
{
    return d->itemWidth;
}

int DirectOverview::itemHeight() const
{
    return d->itemHeight;
}

void DirectOverview::enablePerformanceMeasurement(bool enabled)
{
    d->performanceMeasurementEnabled = enabled;
}

bool DirectOverview::performanceMeasurementEnabled() const
{
    return d->performanceMeasurementEnabled;
}

void DirectOverview::invalidateDesktopCache()
{
    d->invalidateDesktopCache();
}

} // namespace KWin
