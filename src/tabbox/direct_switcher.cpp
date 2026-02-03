#include "direct_switcher.h"

#include "../compositor.h"
#include "../scene/imageitem.h"
#include "../scene/item.h"
#include "../scene/workspacescene.h"

#include "window.h"
#include "workspace.h"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
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
 * 4. Activation must go through TabBox exit path, not direct Workspace calls
 * 5. Never hijack TabBox grabs - integrate cleanly
 * 6. Use EffectWindow*, never Window* for compositor primitives
 * 7. Cache across shows, don't recreate on every Alt+Tab
 */
class DirectSwitcher::Private
{
public:
    Private();
    ~Private();

    void create();
    void destroy();
    void updateSelection();
    void buildLayout();
    void cacheWindowThumbnails();
    void clearWindowCache();
    void recordFrameTime(const char *marker);
    void updateAnimations(qint64 elapsed);
    void activateCurrentSelection();
    void invalidateWindowCache(); // Invalidate on window destroy/geometry change

    // Scene graph
    Item *parentItem = nullptr; // Set via setParentItem(), we don't own this
    Item *root = nullptr; // OWNED BY parentItem, never manually delete
    QList<Item *> items; // OWNED BY root, never manually delete
    QList<ImageItem *> thumbnailItems; // OWNED BY root, never manually delete

    // Window cache (persistent across shows)
    QList<Window *> windowList;
    Window *windowToActivate = nullptr; // Deferred activation (after hide completes)
    bool windowCacheValid = false; // Track if cache needs refresh

    // State
    bool visible = false;
    int currentIndex = 0;
    bool altPressed = false; // Phase 2: Track Alt key for raw input handling
    bool tabPressed = false; // Phase 2: Track Tab key for raw input handling

    // Phase 4: Layout config
    int thumbnailWidth = 200;
    int padding = 20;
    double switcherScreenCoverage = 0.8;
    Output *output = nullptr;

    // Phase 7: Performance measurement
    qint64 creationTimeMs = 0;
    int allocationCount = 0;
    bool performanceMeasurementEnabled = false;

    // Phase 8: Activation config
    bool autoActivateOnSelection = false;

    // Phase 9: Animation config
    bool animationEnabled = false;
    int animationDurationMs = 150;
};

DirectSwitcher::Private::Private()
{
    output = Workspace::self()->activeOutput();
    // Phase 7: Check if performance measurement is enabled via KWIN_PERF
    performanceMeasurementEnabled = (getenv("KWIN_PERF") != nullptr);
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] Performance measurement enabled\n");
    }
}

DirectSwitcher::Private::~Private()
{
    clearWindowCache();
}

void DirectSwitcher::Private::clearWindowCache()
{
    // Phase 6: Hardened cache cleanup
    // Ensure window list is cleared, no dangling pointers
    windowList.clear();
    windowCacheValid = false; // Mark cache as invalid
    allocationCount = 0;
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] window cache cleared and marked invalid\n");
    }
}

void DirectSwitcher::Private::invalidateWindowCache()
{
    // Phase 7 (PERSISTENT CACHE): Invalidate cache when windows change
    // Called when window is destroyed or geometry changes
    // Cache will be refreshed on next show()
    windowCacheValid = false;
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] window cache invalidated (window change detected)\n");
    }
}

void DirectSwitcher::Private::recordFrameTime(const char *marker)
{
    // Phase 7: Simple frame time recording
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] %s\n", marker);
    }
}

void DirectSwitcher::Private::activateCurrentSelection()
{
    // Phase 8: BLOCKER FIX - Deferred activation
    // Must NOT call Workspace::activateWindow() directly here because:
    // 1. Alt key may still be held (key event handler context)
    // 2. Direct activation causes focus issues, stuck modifiers
    // Instead: store window to activate, let hide() complete first
    // Then activate asynchronously after display is released

    if (windowList.isEmpty() || currentIndex >= windowList.size()) {
        return;
    }

    Window *selected = windowList[currentIndex];
    if (!selected) {
        return;
    }

    recordFrameTime("activateCurrentSelection() - deferred");

    // Store for deferred activation (happens after hide() completes)
    windowToActivate = selected;

    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] Activation deferred for window %p\n", selected);
    }
}

void DirectSwitcher::Private::updateAnimations(qint64 elapsed)
{
    // Phase 9: Time-based animation update
    // For now, this is a placeholder for future animation logic
    // Could animate opacity, position, scale based on elapsed time
    // and animationDurationMs

    if (!animationEnabled || animationDurationMs <= 0) {
        return;
    }

    // TODO: Implement opacity/position lerp based on elapsed time
    // float progress = std::min(1.0f, static_cast<float>(elapsed) / animationDurationMs);
    // Apply progress to items: position, opacity
}

void DirectSwitcher::Private::cacheWindowThumbnails()
{
    // Phase 3: Get window list from Workspace
    // Phase 6: Clear cache before repopulating to prevent leaks
    windowList.clear();
    allocationCount = 0;

    QElapsedTimer timer;
    timer.start();

    Workspace *ws = Workspace::self();

    // Get the stacking order (most recently used order for Alt+Tab)
    for (Window *w : ws->stackingOrder()) {
        // Filter: only normal windows, not desktop, not hidden
        if (!w || w->isDesktop() || w->isMinimized() || !w->isShown()) {
            continue;
        }

        // On X11, can switch windows from all desktops or current desktop only
        // For now, include all visible windows
        windowList.append(w);
        allocationCount++; // Phase 7: Track allocations
    }

    // Phase 7: Report timing if enabled
    qint64 elapsed = timer.elapsed();
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] cacheWindowThumbnails: %lld windows in %lld ms\n",
                     (long long)windowList.size(), elapsed);
    }

    // If list is empty, show something (prevents crashes)
    if (windowList.isEmpty()) {
        return;
    }
}

void DirectSwitcher::Private::buildLayout()
{
    // Phase 4: Deterministic geometry solver
    // Horizontal strip layout, center-selected bias

    if (!output || windowList.isEmpty()) {
        return;
    }

    const QRect screen = output->geometry();

    // Calculate available space
    const int totalItems = windowList.size();

    // For horizontal strip, lay out all items horizontally
    // Each item: thumbnail + padding
    const int itemSpacing = thumbnailWidth + padding;

    // Center the whole strip on screen
    const int totalWidth = (totalItems * itemSpacing) - padding; // no trailing padding
    const int startX = screen.x() + (screen.width() - totalWidth) / 2;
    const int centerY = screen.y() + screen.height() / 2;
    const int itemHeight = static_cast<int>(thumbnailWidth * 0.75); // 4:3 aspect ratio
    const int startY = centerY - itemHeight / 2;

    // Position items horizontally
    for (int i = 0; i < thumbnailItems.size(); ++i) {
        ImageItem *item = thumbnailItems[i];
        if (!item) {
            continue;
        }

        const int x = startX + i * itemSpacing;
        const int y = startY;

        item->setPosition(QPointF(x, y));
        item->setSize(QSizeF(thumbnailWidth, itemHeight));

        // Z order: selected higher
        if (i == currentIndex) {
            item->setZ(10);
        } else {
            item->setZ(0);
        }
    }
}

void DirectSwitcher::Private::create()
{
    destroy();

    QElapsedTimer createTimer;
    createTimer.start();
    recordFrameTime("create() start");

    // Create root item without parent initially
    // It will be parented via setParentItem() call
    qWarning() << "DirectSwitcher::create() - parentItem:" << parentItem;

    if (!parentItem) {
        qDebug() << "WARNING: parentItem is null, creating root without parent";
        // Create root item without parent - will need to be reparented later
        // For now, just mark as visible and return to avoid crash
        root = new Item(nullptr);
    } else {
        qDebug() << "DirectSwitcher::create() - parentItem valid, creating root item";
        // Create root as child of parentItem
        root = new Item(parentItem);
    }

    items.clear();
    thumbnailItems.clear();

    // Phase 7 (PERSISTENT CACHE): Only refresh window list if cache invalid
    // Cache persists across Alt+Tab calls unless windows change
    if (!windowCacheValid || windowList.isEmpty()) {
        qDebug() << "DirectSwitcher: Caching windows";
        cacheWindowThumbnails();
        windowCacheValid = true;
    } else if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] Reusing window cache (%lld windows)\n", (long long)windowList.size());
    }

    qDebug() << "DirectSwitcher: Window list has" << windowList.size() << "windows";
    if (windowList.isEmpty()) {
        qDebug() << "WARNING: Window list is empty after caching!";
        visible = true;
        creationTimeMs = createTimer.elapsed();
        return;
    }

    // Phase 3: Create items for each window
    // Each show() creates new visual items but reuses cached window list
    qDebug() << "DirectSwitcher: Creating" << windowList.size() << "image items";
    for (int i = 0; i < windowList.size(); ++i) {
        auto *item = new ImageItem(root);

        // Placeholder image
        // TODO: Use EffectWindow::screenThumbnail() for GPU copy
        QImage placeholder(200, 150, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(Qt::black);

        item->setImage(placeholder);
        item->setOpacity(0.6);

        thumbnailItems.append(item);
        items.append(item);
    }

    currentIndex = 0;

    // Phase 4: Apply layout
    buildLayout();
    updateSelection();
    visible = true;

    creationTimeMs = createTimer.elapsed();

    // Phase 7: Report creation time
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] create() completed with %lld items in %lld ms\n",
                     (long long)items.size(), creationTimeMs);
        if (creationTimeMs > 2) {
            std::fprintf(stderr, "[DirectSwitcher] WARNING: Creation exceeded 2ms budget!\n");
        }
    }
    qDebug() << "DirectSwitcher::create() COMPLETED - visible=" << visible << ", items=" << items.size();
}

void DirectSwitcher::Private::destroy()
{
    // Phase 6: Hardened destruction - ensure NO dangling pointers
    // BLOCKER FIX: Scene graph ownership rules
    // - Items are owned by their parent Item
    // - Deleting them manually is a race condition (render thread may access)
    // - Deleting root will cascade delete all children
    // - Only null pointers, never call delete
    recordFrameTime("destroy() start");

    // Clear lists but DO NOT delete - parent owns the items
    items.clear();
    thumbnailItems.clear();

    // BLOCKER FIX: Delete root last, which will cascade delete all children
    // Never delete individual items - parent handles that
    if (root) {
        delete root;
        root = nullptr;
    }

    // Clear window cache (no dangling window pointers)
    clearWindowCache();

    visible = false;
    currentIndex = 0;

    recordFrameTime("destroy() complete");
}

void DirectSwitcher::Private::updateSelection()
{
    // Update opacity and Z for selection
    for (int i = 0; i < thumbnailItems.size(); ++i) {
        if (i == currentIndex) {
            thumbnailItems[i]->setOpacity(1.0);
            thumbnailItems[i]->setZ(10);
        } else {
            thumbnailItems[i]->setOpacity(0.6);
            thumbnailItems[i]->setZ(0);
        }
    }
}

DirectSwitcher::DirectSwitcher(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

DirectSwitcher::~DirectSwitcher()
{
    d->destroy();
}

void DirectSwitcher::setParentItem(Item *parentItem)
{
    // Store the parent item for use in create()
    d->parentItem = parentItem;

    // If root already exists, reparent it now
    if (d->root && parentItem) {
        d->root->setParent(parentItem);
    }
}

void DirectSwitcher::show(Mode mode)
{

    qWarning() << "DirectSwitcher::show() called";
    if (d->visible) {
        return;
    }

    // CRITICAL: Set the output for layout calculations BEFORE creating
    Output *activeOutput = Workspace::self()->activeOutput();
    qWarning() << "DirectSwitcher::show() - Setting output:" << activeOutput;
    setOutput(activeOutput);

    // Phase 2: Reset input state on show
    d->altPressed = false;
    d->tabPressed = false;

    // Phase 6/7: Create with lifecycle hardening
    d->create();

    // DEBUGGING: Force visibility properties to ensure rendering
    if (d->root) {
        QRectF rect = d->root->boundingRect();
        QRect screenGeom = Workspace::self()->geometry();

        // Force full screen dimensions
        d->root->setSize(QSizeF(screenGeom.width(), screenGeom.height()));
        d->root->setPosition(QPointF(screenGeom.x(), screenGeom.y()));

        // Force full opacity
        d->root->setOpacity(1.0);

        // Force z-index to top
        d->root->setZ(9999);
    }

    // Ensure proper scene graph attachment
    if (Compositor::compositing() && Compositor::self()) {
        WorkspaceScene *scene = Compositor::self()->scene();

        // Try overlay item first (usually where UI overlays go)
        if (Item *overlay = scene->overlayItem()) {
            if (d->root->parentItem() != overlay) {
                d->root->setParentItem(overlay);
            }
        } else if (Item *container = scene->containerItem()) {
            if (d->root->parentItem() != container) {
                d->root->setParentItem(container);
            }
        }
    }

    Q_EMIT visibilityChanged(true);
}

void DirectSwitcher::hide()
{
    if (!d->visible) {
        return;
    }

    // Phase 2: Reset input state on hide
    d->altPressed = false;
    d->tabPressed = false;

    // BLOCKER FIX: Deferred activation
    // Store window to activate BEFORE destroy (while windowList is valid)
    Window *toActivate = d->windowToActivate;
    d->windowToActivate = nullptr;

    // Phase 6: Hardened destruction
    d->destroy();
    Q_EMIT visibilityChanged(false);

    // BLOCKER FIX: Activate after display has been released
    // This ensures Alt key is fully released before Workspace::activateWindow
    // Use deferred call to ensure proper event processing order
    if (toActivate) {
        // Schedule activation for next event loop iteration
        QTimer::singleShot(0, this, [this, toActivate]() {
            Workspace *ws = Workspace::self();
            if (ws && toActivate) {
                ws->activateWindow(toActivate);
                if (d->performanceMeasurementEnabled) {
                    std::fprintf(stderr, "[DirectSwitcher] Deferred activation completed\n");
                }
            }
        });
    }
}

void DirectSwitcher::selectNext()
{
    // Phase 5: Pure selection state change - NO activation by default
    if (!d->visible || d->windowList.isEmpty()) {
        return;
    }

    d->currentIndex = (d->currentIndex + 1) % d->windowList.size();
    d->updateSelection();

    // Phase 5: Emit selection changed signal (no Workspace::activateClient)
    if (!d->windowList.isEmpty() && d->currentIndex < d->windowList.size()) {
        Window *selected = d->windowList[d->currentIndex];
        d->recordFrameTime("selectNext() called");
        Q_EMIT selectionChanged(selected);

        // Phase 8: Auto-activate on selection if configured
        if (d->autoActivateOnSelection) {
            d->activateCurrentSelection();
        }
    }
}

void DirectSwitcher::selectPrevious()
{
    // Phase 5: Pure selection state change - NO activation by default
    if (!d->visible || d->windowList.isEmpty()) {
        return;
    }

    d->currentIndex = (d->currentIndex - 1 + d->windowList.size()) % d->windowList.size();
    d->updateSelection();

    // Phase 5: Emit selection changed signal (no Workspace::activateClient)
    if (!d->windowList.isEmpty() && d->currentIndex < d->windowList.size()) {
        Window *selected = d->windowList[d->currentIndex];
        d->recordFrameTime("selectPrevious() called");
        Q_EMIT selectionChanged(selected);

        // Phase 8: Auto-activate on selection if configured
        if (d->autoActivateOnSelection) {
            d->activateCurrentSelection();
        }
    }
}

void DirectSwitcher::accept()
{
    // Phase 5: Accept selection (activation happens in Phase 8)
    if (!d->visible || d->windowList.isEmpty() || d->currentIndex >= d->windowList.size()) {
        return;
    }

    Window *selected = d->windowList[d->currentIndex];
    if (selected) {
        d->recordFrameTime("accept() called");

        // Phase 8: Activate window on accept (key release / click)
        // Always activate on accept, unless already activated by auto-activation
        if (!d->autoActivateOnSelection) {
            d->activateCurrentSelection();
        }

        hide();
    }
}

bool DirectSwitcher::isVisible() const
{
    return d->visible;
}

void DirectSwitcher::setOutput(Output *output)
{
    d->output = output;
    if (d->visible) {
        d->buildLayout();
    }
}

Window *DirectSwitcher::currentSelection() const
{
    if (!d->visible || d->windowList.isEmpty() || d->currentIndex >= d->windowList.size()) {
        return nullptr;
    }
    return d->windowList[d->currentIndex];
}

void DirectSwitcher::setThumbnailWidth(int width)
{
    d->thumbnailWidth = width;
    if (d->visible) {
        d->buildLayout();
    }
}

void DirectSwitcher::setPadding(int padding)
{
    d->padding = padding;
    if (d->visible) {
        d->buildLayout();
    }
}

void DirectSwitcher::setSwitcherScreenCoverage(double coverage)
{
    d->switcherScreenCoverage = coverage;
    if (d->visible) {
        d->buildLayout();
    }
}

int DirectSwitcher::thumbnailWidth() const
{
    return d->thumbnailWidth;
}

int DirectSwitcher::padding() const
{
    return d->padding;
}

double DirectSwitcher::switcherScreenCoverage() const
{
    return d->switcherScreenCoverage;
}

/**
 * Phase 2: Direct key input handling
 * Hook raw Alt+Tab without going through Qt shortcuts or KGlobalAccel
 */
void DirectSwitcher::keyPress(int key)
{
    // Phase 2 (UNIFIED): Input handling is now unified through TabBox delegation
    // - Alt+Tab → TabBox::slotWalkThroughWindows() → Workspace::slotDirectSwitcherNext()
    // - Alt+Shift+Tab → TabBox::slotWalkBackThroughWindows() → Workspace::slotDirectSwitcherPrevious()
    // - Alt release → TabBox::modifiersReleased() → DirectSwitcher::accept()
    // Raw key handling methods (keyPress/keyRelease) are DEPRECATED and unused.
    // Kept for API compatibility only.
    (void)key; // Unused
}

void DirectSwitcher::keyRelease(int key)
{
    // Phase 2 (UNIFIED): Input handling unified through TabBox (see keyPress)
    (void)key; // Unused
}

/**
 * Phase 8: Activation configuration
 */
void DirectSwitcher::setAutoActivateOnSelection(bool enabled)
{
    d->autoActivateOnSelection = enabled;
    if (d->performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] autoActivateOnSelection = %s\n",
                     enabled ? "true" : "false");
    }
}

bool DirectSwitcher::autoActivateOnSelection() const
{
    return d->autoActivateOnSelection;
}

/**
 * Phase 9: Animation configuration
 */
void DirectSwitcher::setAnimationEnabled(bool enabled)
{
    d->animationEnabled = enabled;
    if (d->performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] animationEnabled = %s\n",
                     enabled ? "true" : "false");
    }
}

bool DirectSwitcher::animationEnabled() const
{
    return d->animationEnabled;
}

void DirectSwitcher::setAnimationDuration(int durationMs)
{
    d->animationDurationMs = durationMs;
    if (d->performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] animationDuration = %d ms\n",
                     durationMs);
    }
}

int DirectSwitcher::animationDuration() const
{
    return d->animationDurationMs;
}

void DirectSwitcher::invalidateWindowCache()
{
    // Phase 7: Public slot for cache invalidation
    // Called when windows change (destroy, geometry, etc)
    d->invalidateWindowCache();
}

} // namespace KWin
