/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010 Sebastian Sauer <sebsauer@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glutils.h"
#include "zoom.h"
#include "zoomconfig.h"

#if HAVE_ACCESSIBILITY
#include "accessibilityintegration.h"
#endif

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KStandardActions>

#include <QAction>

using namespace std::chrono_literals;

static void ensureResources()
{
    // Must initialize resources manually because the effect is a static lib.
    Q_INIT_RESOURCE(zoom);
}

namespace KWin
{

ZoomEffect::ZoomEffect()
{
    ensureResources();

    ZoomConfig::instance(effects->config());
    QAction *a = nullptr;
    a = KStandardActions::zoomIn(this, &ZoomEffect::zoomIn, this);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Plus) << (Qt::META | Qt::Key_Equal));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Plus) << (Qt::META | Qt::Key_Equal));
    effects->registerAxisShortcut(Qt::ControlModifier | Qt::MetaModifier, PointerAxisUp, a);

    a = KStandardActions::zoomOut(this, &ZoomEffect::zoomOut, this);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Minus));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Minus));
    effects->registerAxisShortcut(Qt::ControlModifier | Qt::MetaModifier, PointerAxisDown, a);

    a = KStandardActions::actualSize(this, &ZoomEffect::actualSize, this);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_0));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_0));

    a = new QAction(this);
    a->setObjectName(QStringLiteral("ZoomTo1.4"));
    a->setText(i18n("Zoom to 1.4"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::CTRL | Qt::SHIFT | Qt::Key_Slash));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::CTRL | Qt::SHIFT | Qt::Key_Slash));
    connect(a, &QAction::triggered, this, &ZoomEffect::zoomTo14);

    a = new QAction(this);
    a->setObjectName(QStringLiteral("MoveZoomLeft"));
    a->setText(i18n("Move Zoomed Area to Left"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>());
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>());
    connect(a, &QAction::triggered, this, &ZoomEffect::moveZoomLeft);

    a = new QAction(this);
    a->setObjectName(QStringLiteral("MoveZoomRight"));
    a->setText(i18n("Move Zoomed Area to Right"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>());
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>());
    connect(a, &QAction::triggered, this, &ZoomEffect::moveZoomRight);

    a = new QAction(this);
    a->setObjectName(QStringLiteral("MoveZoomUp"));
    a->setText(i18n("Move Zoomed Area Upwards"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>());
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>());
    connect(a, &QAction::triggered, this, &ZoomEffect::moveZoomUp);

    a = new QAction(this);
    a->setObjectName(QStringLiteral("MoveZoomDown"));
    a->setText(i18n("Move Zoomed Area Downwards"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>());
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>());
    connect(a, &QAction::triggered, this, &ZoomEffect::moveZoomDown);

    // TODO: these two actions don't belong into the effect. They need to be moved into KWin core
    a = new QAction(this);
    a->setObjectName(QStringLiteral("MoveMouseToFocus"));
    a->setText(i18n("Move Mouse to Focus"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_F5));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_F5));
    connect(a, &QAction::triggered, this, &ZoomEffect::moveMouseToFocus);

    a = new QAction(this);
    a->setObjectName(QStringLiteral("MoveMouseToCenter"));
    a->setText(i18n("Move Mouse to Center"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_F6));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_F6));
    connect(a, &QAction::triggered, this, &ZoomEffect::moveMouseToCenter);

    m_timeline.setDuration(350);
    m_timeline.setFrameRange(0, 100);
    connect(&m_timeline, &QTimeLine::frameChanged, this, &ZoomEffect::timelineFrameChanged);
    connect(effects, &EffectsHandler::windowAdded, this, &ZoomEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::screenRemoved, this, &ZoomEffect::slotScreenRemoved);

#if HAVE_ACCESSIBILITY
    if (!effects->waylandDisplay()) {
        // on Wayland, the accessibility integration can cause KWin to hang
        m_accessibilityIntegration = new ZoomAccessibilityIntegration(this);
        connect(m_accessibilityIntegration, &ZoomAccessibilityIntegration::focusPointChanged, this, &ZoomEffect::moveFocus);
    }
#endif

    const auto windows = effects->stackingOrder();
    for (EffectWindow *w : windows) {
        slotWindowAdded(w);
    }

    reconfigure(ReconfigureAll);

    // Initialize state for all screens
    const auto screens = effects->screens();
    for (Output *screen : screens) {
        auto &state = m_states[screen];
        state.zoom = 1.0;
        state.targetZoom = 1.0;
        state.sourceZoom = 1.0;
        state.focusPoint = screen->geometry().center();
        state.prevPoint = state.focusPoint;
    }

    const double initialZoom = ZoomConfig::initialZoom();
    if (initialZoom > 1.0) {
        setTargetZoom(effects->activeScreen(), initialZoom);
    }
}

ZoomEffect::~ZoomEffect()
{
    // switch off and free resources
    showCursor();
    // Save the zoom value.
    if (auto *screen = effects->activeScreen()) {
        ZoomConfig::setInitialZoom(stateForScreen(screen)->targetZoom);
    }
    ZoomConfig::self()->save();
}

bool ZoomEffect::isFocusTrackingEnabled() const
{
#if HAVE_ACCESSIBILITY
    return m_accessibilityIntegration && m_accessibilityIntegration->isFocusTrackingEnabled();
#else
    return false;
#endif
}

bool ZoomEffect::isTextCaretTrackingEnabled() const
{
#if HAVE_ACCESSIBILITY
    return m_accessibilityIntegration && m_accessibilityIntegration->isTextCaretTrackingEnabled();
#else
    return false;
#endif
}

GLTexture *ZoomEffect::ensureCursorTexture()
{
    if (!m_cursorTexture || m_cursorTextureDirty) {
        m_cursorTexture.reset();
        m_cursorTextureDirty = false;
        const auto cursor = effects->cursorImage();
        if (!cursor.image().isNull()) {
            m_cursorTexture = GLTexture::upload(cursor.image());
            if (!m_cursorTexture) {
                return nullptr;
            }
            m_cursorTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        }
    }
    return m_cursorTexture.get();
}

void ZoomEffect::markCursorTextureDirty()
{
    m_cursorTextureDirty = true;
}

void ZoomEffect::showCursor()
{
    if (m_isMouseHidden) {
        disconnect(effects, &EffectsHandler::cursorShapeChanged, this, &ZoomEffect::markCursorTextureDirty);
        // show the previously hidden mouse-pointer again and free the loaded texture/picture.
        effects->showCursor();
        m_cursorTexture.reset();
        m_isMouseHidden = false;
    }
}

void ZoomEffect::hideCursor()
{
    if (m_mouseTracking == MouseTrackingProportional && m_mousePointer == MousePointerKeep) {
        return; // don't replace the actual cursor by a static image for no reason.
    }
    if (!m_isMouseHidden) {
        // try to load the cursor-theme into a OpenGL texture and if successful then hide the mouse-pointer
        GLTexture *texture = nullptr;
        if (effects->isOpenGLCompositing()) {
            texture = ensureCursorTexture();
        }
        if (texture) {
            effects->hideCursor();
            connect(effects, &EffectsHandler::cursorShapeChanged, this, &ZoomEffect::markCursorTextureDirty);
            m_isMouseHidden = true;
        }
    }
}

void ZoomEffect::reconfigure(ReconfigureFlags)
{
    ZoomConfig::self()->read();
    // On zoom-in and zoom-out change the zoom by the defined zoom-factor.
    m_zoomFactor = std::max(0.1, ZoomConfig::zoomFactor());
    m_pixelGridZoom = ZoomConfig::pixelGridZoom();
    // Visibility of the mouse-pointer.
    m_mousePointer = MousePointerType(ZoomConfig::mousePointer());
    // Track moving of the mouse.
    m_mouseTracking = MouseTrackingType(ZoomConfig::mouseTracking());
#if HAVE_ACCESSIBILITY
    if (m_accessibilityIntegration) {
        // Enable tracking of the focused location.
        m_accessibilityIntegration->setFocusTrackingEnabled(ZoomConfig::enableFocusTracking());
        // Enable tracking of the text caret.
        m_accessibilityIntegration->setTextCaretTrackingEnabled(ZoomConfig::enableTextCaretTracking());
    }
#endif
    // The time in milliseconds to wait before a focus-event takes away a mouse-move.
    m_focusDelay = std::max(uint(0), ZoomConfig::focusDelay());
    // The factor the zoom-area will be moved on touching an edge on push-mode or using the navigation KAction's.
    m_moveFactor = std::max(0.1, ZoomConfig::moveFactor());
}

void ZoomEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    int time = 0;
    if (m_lastPresentTime.count()) {
        time = (presentTime - m_lastPresentTime).count();
    }
    m_lastPresentTime = presentTime;

    bool anyZoom = false;
    bool activeScreenZoom = false;
    const Output *cursorScreen = effects->screenAt(effects->cursorPos().toPoint());

    for (auto &[screen, state] : m_states) {
        if (state.zoom != state.targetZoom) {
            const float zoomDist = std::abs(state.targetZoom - state.sourceZoom);
            // Use 100ms base duration for snappier animation response
            const int duration = std::max(10, int(100 * m_zoomFactor));
            if (state.targetZoom > state.zoom) {
                state.zoom = std::min(state.zoom + ((zoomDist * time) / animationTime(std::chrono::milliseconds(duration))), state.targetZoom);
            } else {
                state.zoom = std::max(state.zoom - ((zoomDist * time) / animationTime(std::chrono::milliseconds(duration))), state.targetZoom);
            }
        }
        if (state.zoom != 1.0) {
            anyZoom = true;
            if (screen == cursorScreen) {
                activeScreenZoom = true;
            }
        }
    }

    if (anyZoom) {
        data.mask |= PAINT_SCREEN_TRANSFORMED;
    }

    if (!activeScreenZoom) {
        showCursor();
    } else {
        hideCursor();
    }

    effects->prePaintScreen(data, presentTime);
}

ZoomEffect::OffscreenData *ZoomEffect::ensureOffscreenData(const RenderTarget &renderTarget, const RenderViewport &viewport, Output *screen)
{
    OffscreenData &data = m_offscreenData[screen]; // Always use screen as key to ensure per-screen data isolation

    // Set viewport to match the output's size, not the full desktop
    data.viewport = QRect(0, 0, screen->geometry().width(), screen->geometry().height());
    data.color = renderTarget.colorDescription();

    const GLenum textureFormat = renderTarget.colorDescription() == ColorDescription::sRGB ? GL_RGBA8 : GL_RGBA16F;

    // Use output size for texture allocation instead of full desktop size
    const QSize outputSize(screen->geometry().width() * viewport.scale(), screen->geometry().height() * viewport.scale());

    if (!data.texture || data.texture->size() != outputSize || data.texture->internalFormat() != textureFormat) {
        data.texture = GLTexture::allocate(textureFormat, outputSize);
        if (!data.texture) {
            return nullptr;
        }
        data.texture->setFilter(GL_LINEAR);
        data.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        data.framebuffer = std::make_unique<GLFramebuffer>(data.texture.get());
    }

    data.texture->setContentTransform(renderTarget.transform());
    return &data;
}

GLShader *ZoomEffect::shaderForZoom(double zoom)
{
    if (zoom < m_pixelGridZoom) {
        return ShaderManager::instance()->shader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
    } else {
        if (!m_pixelGridShader) {
            m_pixelGridShader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture, QString(), QStringLiteral(":/effects/zoom/shaders/pixelgrid.frag"));
        }
        return m_pixelGridShader.get();
    }
}
