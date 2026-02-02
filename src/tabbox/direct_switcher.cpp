#include "direct_switcher.h"

#include "../scene/imageitem.h"
#include "../scene/item.h"

#include "window.h"
#include "workspace.h"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <cmath>
#include <cstdio>

namespace KWin
{

/**
 * Phase 2-9 implementation:
 * - Phase 2: Direct Alt+Tab input handling
 * - Phase 3: Window thumbnail acquisition
 * - Phase 4: Deterministic geometry layout
 * - Phase 5: Pure selection state (NO activation)
 * - Phase 6: Hardened show/hide lifecycle
 * - Phase 7: Performance measurement
 * - Phase 8: Window activation (on accept or auto on selection)
 * - Phase 9: Optional animations (time-based, configurable)
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
    void activateCurrentSelection();
    void updateAnimations(qint64 elapsed);

    // Phase 2: Input state
    bool altPressed = false;
    bool tabPressed = false;

    // Phase 3/4: Window list and layout
    Item *root = nullptr;
    QList<Item *> items;
    QList<Window *> windowList;
    QList<ImageItem *> thumbnailItems;

    bool visible = false;
    int currentIndex = 0;

    // Phase 4: Layout config
    int thumbnailWidth = 200;
    int padding = 20;
    double switcherScreenCoverage = 0.8; // 80% of screen
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
    allocationCount = 0;
    if (performanceMeasurementEnabled) {
        std::fprintf(stderr, "[DirectSwitcher] window cache cleared\n");
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
    // Phase 8: Activate the currently selected window
    // This is called either:
    // - On accept() (key release / click) - default
    // - On selectNext/selectPrevious - if autoActivateOnSelection enabled

    if (windowList.isEmpty() || currentIndex >= windowList.size()) {
        return;
    }

    Window *selected = windowList[currentIndex];
    if (!selected) {
        return;
    }

    recordFrameTime("activateCurrentSelection()");

    // Phase 8: Use Workspace to activate the window
    // This is the actual X11/Wayland focus/activation call
    Workspace *ws = Workspace::self();
    if (ws) {
        ws->activateWindow(selected);
        if (performanceMeasurementEnabled) {
            std::fprintf(stderr, "[DirectSwitcher] Window activated via Workspace\n");
        }
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
        std::fprintf(stderr, "[DirectSwitcher] cacheWindowThumbnails: %d windows in %lld ms\n",
                     windowList.size(), elapsed);
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

    // Position items
    for (int i = 0; i < thumbnailItems.size(); ++i) {
        ImageItem *item = thumbnailItems[i];
        const int x = startX + i * itemSpacing;
        const int y = startY;

        item->setPosition(QPoint(x, y));
        item->setSize(QSize(thumbnailWidth, itemHeight));

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

    root = new Item();
    items.clear();
    thumbnailItems.clear();

    // Phase 3: Cache thumbnails from actual windows
    cacheWindowThumbnails();

    if (windowList.isEmpty()) {
        // Fallback: show placeholder
        currentIndex = 0;
        updateSelection();
        visible = true;
        creationTimeMs = createTimer.elapsed();
        if (performanceMeasurementEnabled) {
            std::fprintf(stderr, "[DirectSwitcher] create() completed (empty list) in %lld ms\n", creationTimeMs);
        }
        return;
    }

    // Phase 3: Create items for each window
    // For now, use black placeholders. Real implementation will grab from EffectWindow
    for (int i = 0; i < windowList.size(); ++i) {
        auto *item = new ImageItem(root);

        // Create a placeholder image (real: would use screenThumbnail)
        // Phase 3 TODO: Use EffectWindow::screenThumbnail() for GPU copy
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
        std::fprintf(stderr, "[DirectSwitcher] create() completed with %d items in %lld ms\n",
                     items.size(), creationTimeMs);
        if (creationTimeMs > 2) {
            std::fprintf(stderr, "[DirectSwitcher] WARNING: Creation exceeded 2ms budget!\n");
        }
    }
}

void DirectSwitcher::Private::destroy()
{
    // Phase 6: Hardened destruction - ensure NO dangling pointers
    // Phase 7: Report teardown
    recordFrameTime("destroy() start");

    // Delete items first (they own GL resources)
    for (Item *item : items) {
        delete item;
    }
    items.clear();
    thumbnailItems.clear();

    // Delete root item (will cascade delete children if not already done)
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
    if (!parentItem || !d->root) {
        return;
    }

    // Reparent the root item to the given parent (typically overlay item from scene)
    d->root->setParent(parentItem);
}

void DirectSwitcher::show(Mode mode)
{
    if (d->visible) {
        return;
    }

    // Phase 2: Reset input state on show
    d->altPressed = false;
    d->tabPressed = false;

    // Phase 6/7: Create with lifecycle hardening
    d->create();
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

    // Phase 6: Hardened destruction
    d->destroy();
    Q_EMIT visibilityChanged(false);
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
    // Phase 2 TODO: Wire this from TabBox::keyPress(KeyboardKeyEvent)
    if (key == Qt::Key_Alt) {
        d->altPressed = true;
    } else if (key == Qt::Key_Tab && d->altPressed) {
        if (!d->visible) {
            show();
        } else {
            selectNext();
        }
        d->tabPressed = true;
    } else if (key == Qt::Key_Backtab && d->altPressed) {
        // Alt+Shift+Tab
        if (!d->visible) {
            show();
        } else {
            selectPrevious();
        }
    }
}

void DirectSwitcher::keyRelease(int key)
{
    // Phase 2: On Alt release, accept selection and hide
    if (key == Qt::Key_Alt && d->altPressed) {
        d->altPressed = false;
        d->tabPressed = false;

        if (d->visible) {
            accept();
        }
    }
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

} // namespace KWin
