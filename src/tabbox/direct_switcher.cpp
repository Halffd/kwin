#include "direct_switcher.h"

#include "window.h"
#include "workspace.h"

#include <QElapsedTimer>
#include <QList>
#include <QRect>
#include <cstdio>

namespace KWin
{

/**
 * DirectSwitcher now focuses on state management and window selection.
 * Rendering is delegated to the DirectSwitcherEffect via OffscreenQuickScene.
 */
class DirectSwitcher::Private
{
public:
    Private();
    ~Private();

    void create();
    void destroy();
    void updateSelection();
    void cacheWindowThumbnails();
    void clearWindowCache();
    void recordFrameTime(const char *marker);
    void activateCurrentSelection();
    void invalidateWindowCache();

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

// Layout calculations moved to Effect for Quick rendering

void DirectSwitcher::Private::create()
{
    destroy();

    QElapsedTimer createTimer;
    createTimer.start();
    recordFrameTime("create() start");

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

    currentIndex = 0;
    updateSelection();
    visible = true;

    creationTimeMs = createTimer.elapsed();
    qDebug() << "DirectSwitcher::create() COMPLETED - visible=" << visible;
}

void DirectSwitcher::Private::destroy()
{
    // Destroy window cache
    recordFrameTime("destroy() start");
    clearWindowCache();
    visible = false;
    currentIndex = 0;
    recordFrameTime("destroy() complete");
}

void DirectSwitcher::Private::updateSelection()
{
    // Rendering is now handled by Effect - just track state
    qDebug() << "DirectSwitcher: Selection updated to index" << currentIndex;
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

// setParentItem() removed - rendering now delegated to DirectSwitcherEffect via OffscreenQuickScene

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
    // Layout is now handled by Effect via Quick scene
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
    // Layout is now handled by Effect via Quick scene
}

void DirectSwitcher::setPadding(int padding)
{
    d->padding = padding;
    // Layout is now handled by Effect via Quick scene
}

void DirectSwitcher::setSwitcherScreenCoverage(double coverage)
{
    d->switcherScreenCoverage = coverage;
    // Layout is now handled by Effect via Quick scene
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
