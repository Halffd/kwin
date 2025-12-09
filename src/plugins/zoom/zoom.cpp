/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010 Sebastian Sauer <sebsauer@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "zoom.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glutils.h"
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

    const double initialZoom = ZoomConfig::initialZoom();
    if (initialZoom > 1.0) {
        // Apply initial zoom to the active screen
        setTargetZoom(effects->activeScreen(), initialZoom);
        effects->addRepaintFull();
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
            if (state.targetZoom > state.zoom) {
                state.zoom = std::min(state.zoom + ((zoomDist * time) / animationTime(std::chrono::milliseconds(int(150 * m_zoomFactor)))), state.targetZoom);
            } else {
                state.zoom = std::max(state.zoom - ((zoomDist * time) / animationTime(std::chrono::milliseconds(int(150 * m_zoomFactor)))), state.targetZoom);
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
    const QSize nativeSize = renderTarget.size();

    OffscreenData &data = m_offscreenData[effects->waylandDisplay() ? screen : nullptr];
    data.viewport = viewport.renderRect();
    data.color = renderTarget.colorDescription();

    const GLenum textureFormat = renderTarget.colorDescription() == ColorDescription::sRGB ? GL_RGBA8 : GL_RGBA16F;
    if (!data.texture || data.texture->size() != nativeSize || data.texture->internalFormat() != textureFormat) {
        data.texture = GLTexture::allocate(textureFormat, nativeSize);
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

void ZoomEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen)
{
    OffscreenData *offscreenData = ensureOffscreenData(renderTarget, viewport, screen);
    if (!offscreenData) {
        return;
    }

    // Render the scene in an offscreen texture and then upscale it.
    RenderTarget offscreenRenderTarget(offscreenData->framebuffer.get(), renderTarget.colorDescription());
    RenderViewport offscreenViewport(viewport.renderRect(), viewport.scale(), offscreenRenderTarget);
    GLFramebuffer::pushFramebuffer(offscreenData->framebuffer.get());
    effects->paintScreen(offscreenRenderTarget, offscreenViewport, mask, region, screen);
    GLFramebuffer::popFramebuffer();

    const auto scale = viewport.scale();
    ZoomScreenState *s = stateForScreen(screen);
    const QRect geo = screen->geometry();

    // mouse-tracking allows navigation of the zoom-area using the mouse.
    qreal xTranslation = 0;
    qreal yTranslation = 0;

    switch (m_mouseTracking) {
    case MouseTrackingProportional:
        xTranslation = -int(s->focusPoint.x() * (s->zoom - 1.0));
        yTranslation = -int(s->focusPoint.y() * (s->zoom - 1.0));
        s->prevPoint = s->focusPoint;
        break;
    case MouseTrackingCentered:
        s->prevPoint = s->focusPoint;
        // fall through
    case MouseTrackingDisabled:
        // Center the view on s->prevPoint
        // T = Center - prevPoint * z
        // But we want to clamp so we don't look outside the screen.
        // Valid range for T is [geo.right() * (1 - z), geo.x() * (1 - z)]
        {
            int tX = int(geo.center().x() - s->prevPoint.x() * s->zoom);
            int tY = int(geo.center().y() - s->prevPoint.y() * s->zoom);

            int minX = int((geo.x() + geo.width()) * (1.0 - s->zoom));
            int maxX = int(geo.x() * (1.0 - s->zoom));

            int minY = int((geo.y() + geo.height()) * (1.0 - s->zoom));
            int maxY = int(geo.y() * (1.0 - s->zoom));

            xTranslation = std::clamp(tX, minX, maxX);
            yTranslation = std::clamp(tY, minY, maxY);
        }
        break;
    case MouseTrackingPush: {
        // touching an edge of the screen moves the zoom-area in that direction.
        const int x = s->focusPoint.x() * s->zoom - s->prevPoint.x() * (s->zoom - 1.0);
        const int y = s->focusPoint.y() * s->zoom - s->prevPoint.y() * (s->zoom - 1.0);
        const int threshold = 4;

        // Bounds of the current screen
        const int screenTop = geo.top();
        const int screenLeft = geo.left();
        const int screenRight = geo.left() + geo.width();
        const int screenBottom = geo.top() + geo.height();

        s->xMove = s->yMove = 0;
        if (x < screenLeft + threshold) {
            s->xMove = (x - threshold - screenLeft) / s->zoom;
        } else if (x > screenRight - threshold) {
            s->xMove = (x + threshold - screenRight) / s->zoom;
        }
        if (y < screenTop + threshold) {
            s->yMove = (y - threshold - screenTop) / s->zoom;
        } else if (y > screenBottom - threshold) {
            s->yMove = (y + threshold - screenBottom) / s->zoom;
        }
        if (s->xMove) {
            s->prevPoint.setX(s->prevPoint.x() + s->xMove);
        }
        if (s->yMove) {
            s->prevPoint.setY(s->prevPoint.y() + s->yMove);
        }
        xTranslation = -int(s->prevPoint.x() * (s->zoom - 1.0));
        yTranslation = -int(s->prevPoint.y() * (s->zoom - 1.0));
        break;
    }
    }

    // use the focusPoint if focus tracking is enabled
    if (isFocusTrackingEnabled() || isTextCaretTrackingEnabled()) {
        bool acceptFocus = true;
        if (m_mouseTracking != MouseTrackingDisabled && m_focusDelay > 0) {
            // Wait some time for the mouse before doing the switch. This serves as threshold
            // to prevent the focus from jumping around to much while working with the mouse.
            const int msecs = m_lastMouseEvent.msecsTo(m_lastFocusEvent);
            acceptFocus = msecs > m_focusDelay;
        }
        if (acceptFocus) {
            xTranslation = -int(s->focusPoint.x() * (s->zoom - 1.0));
            yTranslation = -int(s->focusPoint.y() * (s->zoom - 1.0));
            s->prevPoint = s->focusPoint;
        }
    }

    // Ensure that for proportional mode and others we strictly clamp to avoid black borders
    if (m_mouseTracking != MouseTrackingDisabled && m_mouseTracking != MouseTrackingCentered) {
        int minX = int((geo.x() + geo.width()) * (1.0 - s->zoom));
        int maxX = int(geo.x() * (1.0 - s->zoom));
        int minY = int((geo.y() + geo.height()) * (1.0 - s->zoom));
        int maxY = int(geo.y() * (1.0 - s->zoom));

        xTranslation = std::clamp(int(xTranslation), minX, maxX);
        yTranslation = std::clamp(int(yTranslation), minY, maxY);
    }

    // Render transformed offscreen texture.
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    GLShader *shader = shaderForZoom(s->zoom);
    ShaderManager::instance()->pushShader(shader);

    QMatrix4x4 matrix;
    matrix.translate(xTranslation * scale, yTranslation * scale);
    matrix.scale(s->zoom, s->zoom);
    matrix.translate(offscreenData->viewport.x() * scale, offscreenData->viewport.y() * scale);

    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix() * matrix);
    shader->setUniform(GLShader::IntUniform::TextureWidth, offscreenData->texture->width());
    shader->setUniform(GLShader::IntUniform::TextureHeight, offscreenData->texture->height());
    shader->setColorspaceUniforms(offscreenData->color, renderTarget.colorDescription(), RenderingIntent::Perceptual);

    offscreenData->texture->render(offscreenData->viewport.size() * scale);

    ShaderManager::instance()->popShader();

    if (m_mousePointer != MousePointerHide && s->zoom != 1.0) {
        // Draw the mouse-texture at the position matching to zoomed-in image of the desktop. Hiding the
        // previous mouse-cursor and drawing our own fake mouse-cursor is needed to be able to scale the
        // mouse-cursor up and to re-position those mouse-cursor to match to the chosen zoom-level.

        GLTexture *cursorTexture = ensureCursorTexture();
        if (cursorTexture) {
            const auto cursor = effects->cursorImage();
            QSizeF cursorSize = QSizeF(cursor.image().size()) / cursor.image().devicePixelRatio();
            if (m_mousePointer == MousePointerScale) {
                cursorSize *= s->zoom;
            }

            const QPointF p = (effects->cursorPos() - cursor.hotSpot()) * s->zoom + QPoint(xTranslation, yTranslation);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            auto shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
            shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
            QMatrix4x4 mvp = viewport.projectionMatrix();
            mvp.translate(p.x() * scale, p.y() * scale);
            shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
            cursorTexture->render(cursorSize * scale);
            ShaderManager::instance()->popShader();
            glDisable(GL_BLEND);
        }
    }
}

void ZoomEffect::postPaintScreen()
{
    bool anyZooming = false;
    for (const auto &[screen, state] : m_states) {
        if (state.zoom != state.targetZoom) {
            anyZooming = true;
            break;
        }
    }

    if (!anyZooming) {
        m_lastPresentTime = std::chrono::milliseconds::zero();
    }

    if (anyZooming || isActive()) {
        // Either animation is running or the zoom effect has stopped.
        effects->addRepaintFull();
    }

    effects->postPaintScreen();
}

void ZoomEffect::zoomIn()
{
    zoomTo(-1.0);
}

void ZoomEffect::zoomTo(double to)
{
    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);

    s->sourceZoom = s->zoom;
    if (to < 0.0) {
        setTargetZoom(screen, s->targetZoom * m_zoomFactor);
    } else {
        setTargetZoom(screen, to);
    }
    s->focusPoint = effects->cursorPos().toPoint();
    if (m_mouseTracking == MouseTrackingDisabled) {
        s->prevPoint = s->focusPoint;
    }
    effects->addRepaintFull();
}

void ZoomEffect::zoomTo14()
{
    zoomTo(1.4);
}

void ZoomEffect::zoomOut()
{
    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);

    s->sourceZoom = s->zoom;
    setTargetZoom(screen, s->targetZoom / m_zoomFactor);
    if ((m_zoomFactor > 1 && s->targetZoom < 1.01) || (m_zoomFactor < 1 && s->targetZoom > 0.99)) {
        setTargetZoom(screen, 1);
    }
    if (m_mouseTracking == MouseTrackingDisabled) {
        s->prevPoint = effects->cursorPos().toPoint();
    }
    effects->addRepaintFull();
}

void ZoomEffect::actualSize()
{
    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    s->sourceZoom = s->zoom;
    setTargetZoom(screen, 1);
    effects->addRepaintFull();
}

void ZoomEffect::timelineFrameChanged(int /* frame */)
{
    for (auto &[screen, state] : m_states) {
        QRect geo = screen->geometry();
        state.prevPoint.setX(std::max(geo.x(), std::min(geo.x() + geo.width(), state.prevPoint.x() + state.xMove)));
        state.prevPoint.setY(std::max(geo.y(), std::min(geo.y() + geo.height(), state.prevPoint.y() + state.yMove)));
        state.focusPoint = state.prevPoint;
    }
    effects->addRepaintFull();
}

void ZoomEffect::moveZoom(int x, int y)
{
    if (m_timeline.state() == QTimeLine::Running) {
        m_timeline.stop();
    }

    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    QRect geo = screen->geometry();

    if (x < 0) {
        s->xMove = -std::max(1.0, geo.width() / s->zoom / m_moveFactor);
    } else if (x > 0) {
        s->xMove = std::max(1.0, geo.width() / s->zoom / m_moveFactor);
    } else {
        s->xMove = 0;
    }

    if (y < 0) {
        s->yMove = -std::max(1.0, geo.height() / s->zoom / m_moveFactor);
    } else if (y > 0) {
        s->yMove = std::max(1.0, geo.height() / s->zoom / m_moveFactor);
    } else {
        s->yMove = 0;
    }

    m_timeline.start();
}

void ZoomEffect::moveZoomLeft()
{
    moveZoom(-1, 0);
}

void ZoomEffect::moveZoomRight()
{
    moveZoom(1, 0);
}

void ZoomEffect::moveZoomUp()
{
    moveZoom(0, -1);
}

void ZoomEffect::moveZoomDown()
{
    moveZoom(0, 1);
}

void ZoomEffect::moveMouseToFocus()
{
    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);

    if (effects->waylandDisplay() || !ZoomEffect::isActive()) {
        const auto window = effects->activeWindow();
        if (!window) {
            return;
        }
        const auto center = window->frameGeometry().center();
        QCursor::setPos(center.x(), center.y());
    } else {
        QCursor::setPos(s->focusPoint.x(), s->focusPoint.y());
    }
}

void ZoomEffect::moveMouseToCenter()
{
    const QRect r = effects->activeScreen()->geometry();
    QCursor::setPos(r.x() + r.width() / 2, r.y() + r.height() / 2);
}

void ZoomEffect::slotMouseChanged(const QPointF &pos, const QPointF &old)
{
    Output *screen = effects->screenAt(pos.toPoint());
    if (screen) {
        ZoomScreenState *s = stateForScreen(screen);
        if (s->zoom != 1.0) {
            s->focusPoint = pos.toPoint();
            if (pos != old) {
                m_lastMouseEvent = QTime::currentTime();
                effects->addRepaintFull();
            }
        }
    }
}

void ZoomEffect::slotWindowAdded(EffectWindow *w)
{
    connect(w, &EffectWindow::windowDamaged, this, &ZoomEffect::slotWindowDamaged);
}

void ZoomEffect::slotWindowDamaged()
{
    if (isActive()) {
        effects->addRepaintFull();
    }
}

void ZoomEffect::slotScreenRemoved(Output *screen)
{
    if (auto it = m_offscreenData.find(screen); it != m_offscreenData.end()) {
        effects->makeOpenGLContextCurrent();
        m_offscreenData.erase(it);
    }
    m_states.erase(screen);
}

void ZoomEffect::moveFocus(const QPoint &point)
{
    Output *screen = effects->screenAt(point);
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    if (s->zoom == 1.0) {
        return;
    }
    s->focusPoint = point;
    m_lastFocusEvent = QTime::currentTime();
    effects->addRepaintFull();
}

bool ZoomEffect::isActive() const
{
    for (const auto &[screen, state] : m_states) {
        if (state.zoom != 1.0 || state.targetZoom != 1.0) {
            return true;
        }
    }
    return false;
}

int ZoomEffect::requestedEffectChainPosition() const
{
    return 10;
}

qreal ZoomEffect::configuredZoomFactor() const
{
    return m_zoomFactor;
}

int ZoomEffect::configuredMousePointer() const
{
    return m_mousePointer;
}

int ZoomEffect::configuredMouseTracking() const
{
    return m_mouseTracking;
}

int ZoomEffect::configuredFocusDelay() const
{
    return m_focusDelay;
}

qreal ZoomEffect::configuredMoveFactor() const
{
    return m_moveFactor;
}

qreal ZoomEffect::targetZoom() const
{
    if (auto *screen = effects->activeScreen()) {
        return stateForScreen(screen)->targetZoom;
    }
    return 1.0;
}

bool ZoomEffect::screenExistsAt(const QPoint &point) const
{
    const Output *output = effects->screenAt(point);
    return output && output->geometry().contains(point);
}

ZoomEffect::ZoomScreenState *ZoomEffect::stateForScreen(Output *output)
{
    auto it = m_states.find(output);
    if (it == m_states.end()) {
        it = m_states.emplace(output, ZoomScreenState()).first;
        // Initialize focus point to current cursor if on this screen, otherwise center
        if (output->geometry().contains(effects->cursorPos().toPoint())) {
            it->second.focusPoint = effects->cursorPos().toPoint();
        } else {
            it->second.focusPoint = output->geometry().center();
        }
        it->second.prevPoint = it->second.focusPoint;
    }
    return &it->second;
}

const ZoomEffect::ZoomScreenState *ZoomEffect::stateForScreen(Output *output) const
{
    auto it = m_states.find(output);
    if (it == m_states.end()) {
        // Should not happen in const context if properly managed, but fallback
        static ZoomScreenState defaultState;
        return &defaultState;
    }
    return &it->second;
}

void ZoomEffect::setTargetZoom(Output *output, double value)
{
    value = std::min(value, 100.0);
    ZoomScreenState *s = stateForScreen(output);
    const bool newActive = value != 1.0;
    const bool oldActive = s->targetZoom != 1.0;

    // Check if any screen was active before
    bool anyOldActive = false;
    for (const auto &[scr, state] : m_states) {
        if (state.targetZoom != 1.0) {
            anyOldActive = true;
            break;
        }
    }

    s->targetZoom = value;

    // Check if any screen is active now
    bool anyNewActive = false;
    for (const auto &[scr, state] : m_states) {
        if (state.targetZoom != 1.0) {
            anyNewActive = true;
            break;
        }
    }

    if (anyNewActive && !anyOldActive) {
        connect(effects, &EffectsHandler::mouseChanged, this, &ZoomEffect::slotMouseChanged);
    } else if (!anyNewActive && anyOldActive) {
        disconnect(effects, &EffectsHandler::mouseChanged, this, &ZoomEffect::slotMouseChanged);
    }
}

} // namespace

#include "moc_zoom.cpp"