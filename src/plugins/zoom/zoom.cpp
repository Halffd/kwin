/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010 Sebastian Sauer <sebsauer@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "zoom.h"
// KConfigSkeleton
#include "zoomconfig.h"

#if KWIN_BUILD_QACCESSIBILITYCLIENT
#include "focustracker.h"
#include "textcarettracker.h"
#endif

#include "core/output.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glframebuffer.h"
#include "opengl/glplatform.h"
#include "opengl/gltexture.h"
#include "opengl/glutils.h"
#include "opengl/glvertexbuffer.h"

#include <QAction>
#include <QApplication>
#include <QStyle>
#include <QVector2D>

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KStandardActions>
#include <KWindowSystem>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
namespace KWin
{

ZoomEffect::ZoomEffect()
    : Effect()
    , m_zoomFactor(1.5)
    , m_mouseTracking(MouseTrackingProportional)
    , m_mousePointer(MousePointerScale)
    , m_focusDelay(350)
    , m_moveFactor(20.0)
    , m_lastPresentTime(std::chrono::milliseconds::zero())
{
    ZoomConfig::instance(effects->config());
    QAction *a = nullptr;
    a = KStandardActions::zoomIn(this, &ZoomEffect::zoomIn, this);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Plus) << (Qt::META | Qt::Key_Equal));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Plus) << (Qt::META | Qt::Key_Equal));
    effects->registerAxisShortcut(Qt::ControlModifier | Qt::MetaModifier, PointerAxisDown, a);

    a = KStandardActions::zoomOut(this, &ZoomEffect::zoomOut, this);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Minus));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_Minus));
    effects->registerAxisShortcut(Qt::ControlModifier | Qt::MetaModifier, PointerAxisUp, a);

    a = KStandardActions::actualSize(this, &ZoomEffect::actualSize, this);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_0));
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << (Qt::META | Qt::Key_0));

    // Register DBus interface for zoom operations
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Zoom"),
                                                 QStringLiteral("org.kde.KWin.Effect.Zoom"),
                                                 this,
                                                 QDBusConnection::ExportAllSlots);

    // Add hotkey for specific zoom level 1.4: Ctrl+Shift+/
    a = new QAction(this);
    a->setObjectName(QStringLiteral("ZoomTo14"));
    a->setText(i18n("Zoom to 140%"));
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

    const auto windows = effects->stackingOrder();
    for (EffectWindow *w : windows) {
        slotWindowAdded(w);
    }

    connect(effects, &EffectsHandler::windowAdded, this, &ZoomEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::screenRemoved, this, &ZoomEffect::slotScreenRemoved);

    const QList<LogicalOutput *> screens = effects->screens();
    for (LogicalOutput *screen : screens) {
        OffscreenData &data = m_offscreenData[screen];
        data.viewport = QRectF(screen->geometry());
    }

    reconfigure(ReconfigureAll);
}

ZoomEffect::~ZoomEffect()
{
    // switch off and free resources
    showCursor();
}

bool ZoomEffect::isFocusTrackingEnabled() const
{
#if KWIN_BUILD_QACCESSIBILITYCLIENT
    return m_focusTracker != nullptr;
#else
    return false;
#endif
}

bool ZoomEffect::isTextCaretTrackingEnabled() const
{
#if KWIN_BUILD_QACCESSIBILITYCLIENT
    return m_textCaretTracker != nullptr;
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
#if KWIN_BUILD_QACCESSIBILITYCLIENT
    // Initialize trackers if needed
    if (ZoomConfig::enableFocusTracking() && !m_focusTracker) {
        m_focusTracker = new FocusTracker();
        connect(m_focusTracker, &FocusTracker::moved, this, [this](const QPointF &point) {
            moveFocus(point.toPoint());
        });
    } else if (!ZoomConfig::enableFocusTracking() && m_focusTracker) {
        delete m_focusTracker;
        m_focusTracker = nullptr;
    }

    if (ZoomConfig::enableTextCaretTracking() && !m_textCaretTracker) {
        m_textCaretTracker = new TextCaretTracker();
        connect(m_textCaretTracker, &TextCaretTracker::moved, this, [this](const QPointF &focus) {
            moveFocus(focus.toPoint());
        });
    } else if (!ZoomConfig::enableTextCaretTracking() && m_textCaretTracker) {
        delete m_textCaretTracker;
        m_textCaretTracker = nullptr;
    }
#endif
    // The time in milliseconds to wait before a focus-event takes away a mouse-move.
    m_focusDelay = std::max(uint(0), ZoomConfig::focusDelay());
    // The factor the zoom-area will be moved on touching an edge on push-mode or using the navigation KAction's.
    m_moveFactor = std::max(0.1, ZoomConfig::moveFactor());
}

void ZoomEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    [[maybe_unused]] int time = 0;
    if (m_lastPresentTime.count()) {
        time = (presentTime - m_lastPresentTime).count();
    }
    m_lastPresentTime = presentTime;

    bool anyZoom = false;
    bool activeScreenZoom = false;
    const LogicalOutput *cursorScreen = effects->screenAt(effects->cursorPos().toPoint());

    for (auto &[screen, state] : m_states) {
        if (state.zoom != state.targetZoom) {
            // Smooth animation with configurable speed
            const float step = m_animationSpeed; // Use configurable animation speed

            // Calculate the difference to the target
            const float diff = state.targetZoom - state.zoom;

            // Move toward the target with very fast speed
            if (diff > 0) {
                state.zoom = std::min(state.zoom + step, state.targetZoom);
            } else {
                state.zoom = std::max(state.zoom - step, state.targetZoom);
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

ZoomEffect::OffscreenData *ZoomEffect::ensureOffscreenData(const RenderTarget &renderTarget, const RenderViewport &viewport, LogicalOutput *screen)
{
    const QSize nativeSize = renderTarget.size();

    OffscreenData &data = m_offscreenData[screen];
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
            // Try to load the pixel grid shader with proper traits
            m_pixelGridShader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture, QString(), QStringLiteral(":/effects/zoom/shaders/pixelgrid.frag"));

            // If it's still not valid, there might be issues with the shader itself
            if (!m_pixelGridShader || !m_pixelGridShader->isValid()) {
                qCritical() << "Pixel grid shader failed to load - falling back to basic shader!";
                m_pixelGridShader.reset();
                return ShaderManager::instance()->shader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
            }
        }
        return m_pixelGridShader.get();
    }
}

void ZoomEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const Region &region, LogicalOutput *screen)
{
    ZoomScreenState *state = stateForScreen(screen);

    qDebug() << "=== ZoomEffect::paintScreen START ===";
    qDebug() << "Screen geometry:" << screen->geometry();
    qDebug() << "State zoom:" << state->zoom << "targetZoom:" << state->targetZoom;
    qDebug() << "Viewport scale:" << viewport.scale();
    qDebug() << "RenderTarget size:" << renderTarget.size();
    qDebug() << "Region:" << region;

    // If no zoom on this screen, render normally
    if (state->zoom == 1.0 && state->targetZoom == 1.0) {
        qDebug() << "No zoom active, rendering normally";
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }

    const QRect geo = screen->geometry();
    const double scale = viewport.scale();
    const QSize outputSize = (QSizeF(geo.size()) * scale).toSize();

    qDebug() << "geo:" << geo;
    qDebug() << "scale:" << scale;
    qDebug() << "outputSize:" << outputSize;

    OffscreenData &data = m_offscreenData[screen];

    // Ensure texture with correct format (HDR support if needed)
    const GLenum textureFormat = renderTarget.colorDescription() == ColorDescription::sRGB ? GL_RGBA8 : GL_RGBA16F;
    qDebug() << "textureFormat:" << (textureFormat == GL_RGBA8 ? "GL_RGBA8" : "GL_RGBA16F");

    if (!data.texture || data.texture->size() != outputSize || data.texture->internalFormat() != textureFormat) {
        qDebug() << "Allocating new texture, old size:" << (data.texture ? data.texture->size() : QSize());
        data.texture = GLTexture::allocate(textureFormat, outputSize);
        if (!data.texture) {
            qWarning() << "Failed to allocate offscreen texture!";
            effects->paintScreen(renderTarget, viewport, mask, region, screen);
            return;
        }
        data.texture->setFilter(GL_LINEAR);
        data.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        data.framebuffer = std::make_unique<GLFramebuffer>(data.texture.get());
        qDebug() << "Texture allocated successfully:" << data.texture->size();
    }

    data.color = renderTarget.colorDescription();
    data.texture->setContentTransform(renderTarget.transform());

    // Render to offscreen texture
    qDebug() << "--- Offscreen Rendering ---";
    qDebug() << "Creating RenderViewport with:";
    qDebug() << "  renderRect (geo):" << geo;
    qDebug() << "  scale:" << scale;
    qDebug() << "  renderOffset (geo.topLeft()):" << geo.topLeft();

    RenderTarget offscreenTarget(data.framebuffer.get(), renderTarget.colorDescription());
    RenderViewport offscreenViewport(RectF(geo), scale, offscreenTarget, geo.topLeft());

    qDebug() << "Offscreen viewport created";
    qDebug() << "Setting GL viewport to:" << QRect(0, 0, outputSize.width(), outputSize.height());

    GLFramebuffer::pushFramebuffer(data.framebuffer.get());
    glViewport(0, 0, outputSize.width(), outputSize.height());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // CRITICAL FIX: Convert region to screen's coordinate space
    // The region might be in device coordinates, but paintScreen expects logical
    QRegion logicalRegion = static_cast<QRegion>(region);

    // If region is in device pixels, scale it to logical
    if (scale != 1.0) {
        QRegion scaledRegion;
        for (const QRect &rect : logicalRegion) {
            QRectF logicalRect = QRectF(rect.x() / scale, rect.y() / scale,
                                        rect.width() / scale, rect.height() / scale);
            scaledRegion += logicalRect.toRect();
        }
        logicalRegion = scaledRegion;
        qDebug() << "Scaled region from device to logical:" << logicalRegion;
    }

    // Intersect with screen geometry to only paint what's on this screen
    logicalRegion &= geo;
    qDebug() << "Final logical region for offscreen paint:" << logicalRegion;

    qDebug() << "Calling effects->paintScreen to offscreen";
    effects->paintScreen(offscreenTarget, offscreenViewport, mask, Region(logicalRegion), screen);
    qDebug() << "Offscreen rendering complete";

    GLFramebuffer::popFramebuffer();

    // Calculate zoom pan offsets (logical coordinates relative to output)
    qDebug() << "--- Zoom Translation Calculation ---";
    qDebug() << "state->focusPoint:" << state->focusPoint;
    qDebug() << "state->prevPoint:" << state->prevPoint;

    QPoint localFocus = state->focusPoint - geo.topLeft();
    QPoint localPrev = state->prevPoint - geo.topLeft();

    qDebug() << "localFocus:" << localFocus;
    qDebug() << "localPrev:" << localPrev;
    qDebug() << "m_mouseTracking:" << m_mouseTracking;

    qreal xTranslation = 0;
    qreal yTranslation = 0;

    switch (m_mouseTracking) {
    case MouseTrackingProportional:
        xTranslation = -localFocus.x() * (state->zoom - 1.0);
        yTranslation = -localFocus.y() * (state->zoom - 1.0);
        qDebug() << "MouseTrackingProportional:";
        qDebug() << "  xTranslation:" << xTranslation;
        qDebug() << "  yTranslation:" << yTranslation;
        state->prevPoint = state->focusPoint;
        break;

    case MouseTrackingCentered:
        state->prevPoint = state->focusPoint;
        qDebug() << "MouseTrackingCentered (falling through to Disabled)";
        // fall through

    case MouseTrackingDisabled: {
        qreal tX = geo.width() / 2.0 - localPrev.x() * state->zoom;
        qreal tY = geo.height() / 2.0 - localPrev.y() * state->zoom;
        qreal minX = geo.width() * (1.0 - state->zoom);
        qreal minY = geo.height() * (1.0 - state->zoom);

        qDebug() << "MouseTrackingDisabled:";
        qDebug() << "  tX (unclamped):" << tX << "tY:" << tY;
        qDebug() << "  minX:" << minX << "minY:" << minY;

        xTranslation = std::clamp(tX, minX, 0.0);
        yTranslation = std::clamp(tY, minY, 0.0);

        qDebug() << "  xTranslation (clamped):" << xTranslation;
        qDebug() << "  yTranslation (clamped):" << yTranslation;
    } break;

    case MouseTrackingPush: {
        const qreal x = localFocus.x() * state->zoom - localPrev.x() * (state->zoom - 1.0);
        const qreal y = localFocus.y() * state->zoom - localPrev.y() * (state->zoom - 1.0);
        const qreal threshold = 4;

        qDebug() << "MouseTrackingPush:";
        qDebug() << "  x:" << x << "y:" << y << "threshold:" << threshold;

        state->xMove = state->yMove = 0;

        if (x < threshold) {
            state->xMove = (x - threshold) / state->zoom;
        } else if (x > geo.width() - threshold) {
            state->xMove = (x + threshold - geo.width()) / state->zoom;
        }

        if (y < threshold) {
            state->yMove = (y - threshold) / state->zoom;
        } else if (y > geo.height() - threshold) {
            state->yMove = (y + threshold - geo.height()) / state->zoom;
        }

        qDebug() << "  xMove:" << state->xMove << "yMove:" << state->yMove;

        if (state->xMove) {
            state->prevPoint.setX(state->prevPoint.x() + state->xMove);
        }
        if (state->yMove) {
            state->prevPoint.setY(state->prevPoint.y() + state->yMove);
        }

        qDebug() << "  updated prevPoint:" << state->prevPoint;

        localPrev = state->prevPoint - geo.topLeft();
        xTranslation = -localPrev.x() * (state->zoom - 1.0);
        yTranslation = -localPrev.y() * (state->zoom - 1.0);

        qDebug() << "  final xTranslation:" << xTranslation;
        qDebug() << "  final yTranslation:" << yTranslation;
    } break;
    }

    // Focus tracking
    if (isFocusTrackingEnabled() || isTextCaretTrackingEnabled()) {
        qDebug() << "Focus tracking enabled";
        bool acceptFocus = true;
        if (m_mouseTracking != MouseTrackingDisabled && m_focusDelay > 0) {
            const int msecs = m_lastMouseEvent.msecsTo(m_lastFocusEvent);
            qDebug() << "  msecs since last mouse:" << msecs << "focusDelay:" << m_focusDelay;
            acceptFocus = msecs > m_focusDelay;
        }
        if (acceptFocus) {
            qDebug() << "  Accepting focus, overriding translation";
            xTranslation = -localFocus.x() * (state->zoom - 1.0);
            yTranslation = -localFocus.y() * (state->zoom - 1.0);
            state->prevPoint = state->focusPoint;
            qDebug() << "  new xTranslation:" << xTranslation << "yTranslation:" << yTranslation;
        }
    }

    // Clamp to prevent black borders
    if (m_mouseTracking != MouseTrackingDisabled && m_mouseTracking != MouseTrackingCentered) {
        qreal minX = geo.width() * (1.0 - state->zoom);
        qreal minY = geo.height() * (1.0 - state->zoom);

        qDebug() << "Clamping translations:";
        qDebug() << "  Before clamp - xTranslation:" << xTranslation << "yTranslation:" << yTranslation;
        qDebug() << "  Clamp range - minX:" << minX << "maxX: 0, minY:" << minY << "maxY: 0";

        xTranslation = std::clamp(xTranslation, minX, 0.0);
        yTranslation = std::clamp(yTranslation, minY, 0.0);

        qDebug() << "  After clamp - xTranslation:" << xTranslation << "yTranslation:" << yTranslation;
    }

    // Scissor test uses renderTarget-local coordinates
    qDebug() << "--- Compositing to Screen ---";
    qDebug() << "Setting scissor test:" << QRect(0, 0, renderTarget.size().width(), renderTarget.size().height());
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, renderTarget.size().width(), renderTarget.size().height());

    GLShader *shader = shaderForZoom(state->zoom);
    ShaderManager::instance()->pushShader(shader);
    qDebug() << "Shader pushed for zoom:" << state->zoom;

    // Build transformation matrix - need to scale logical coordinates to device pixels
    QMatrix4x4 matrix = viewport.projectionMatrix();
    qDebug() << "Initial projection matrix:" << matrix;

    // First translate to output position in logical coordinates
    matrix.translate(geo.x(), geo.y());
    qDebug() << "After translate to output position (" << geo.x() << "," << geo.y() << "):" << matrix;

    // Apply zoom pan in logical coordinates
    matrix.translate(xTranslation, yTranslation);
    qDebug() << "After zoom pan translate (" << xTranslation << "," << yTranslation << "):" << matrix;

    // Apply zoom scale in logical coordinates
    matrix.scale(state->zoom, state->zoom);
    qDebug() << "After zoom scale (" << state->zoom << "):" << matrix;

    // CRITICAL: Scale from logical to device pixels to match vertices
    matrix.scale(scale, scale);
    qDebug() << "After scaling to device pixels (" << scale << "):" << matrix;

    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
    shader->setUniform(GLShader::IntUniform::TextureWidth, data.texture->width());
    shader->setUniform(GLShader::IntUniform::TextureHeight, data.texture->height());
    shader->setColorspaceUniforms(data.color, renderTarget.colorDescription(), RenderingIntent::Perceptual);

    qDebug() << "Texture dimensions set - width:" << data.texture->width() << "height:" << data.texture->height();

    glBindTexture(GL_TEXTURE_2D, data.texture->texture());
    qDebug() << "Texture bound:" << data.texture->texture();

    // CRITICAL FIX: Vertices must match texture size in device pixels
    // The texture is in device pixels, so vertices should be too
    const float x1 = 0;
    const float y1 = 0;
    const float x2 = outputSize.width(); // Device pixels
    const float y2 = outputSize.height(); // Device pixels

    qDebug() << "Quad vertices (device coords to match texture):";
    qDebug() << "  Top-left:" << x1 << "," << y1;
    qDebug() << "  Bottom-right:" << x2 << "," << y2;
    qDebug() << "  Texture size:" << outputSize;

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));

    GLVertex2D vertices[6];
    vertices[0] = {{x1, y1}, {0.0f, 1.0f}};
    vertices[1] = {{x2, y1}, {1.0f, 1.0f}};
    vertices[2] = {{x2, y2}, {1.0f, 0.0f}};
    vertices[3] = {{x2, y2}, {1.0f, 0.0f}};
    vertices[4] = {{x1, y2}, {0.0f, 0.0f}};
    vertices[5] = {{x1, y1}, {0.0f, 1.0f}};

    vbo->setVertices(std::span(vertices));
    qDebug() << "Rendering quad with 6 vertices (2 triangles)";
    vbo->render(GL_TRIANGLES);

    ShaderManager::instance()->popShader();
    glDisable(GL_SCISSOR_TEST);
    qDebug() << "Shader popped, scissor disabled";

    // Draw cursor if on this screen
    if (m_mousePointer != MousePointerHide && effects->screenAt(effects->cursorPos().toPoint()) == screen) {
        qDebug() << "--- Drawing Cursor ---";
        qDebug() << "Cursor pos:" << effects->cursorPos();
        qDebug() << "m_mousePointer mode:" << m_mousePointer;

        GLTexture *cursorTexture = ensureCursorTexture();
        if (cursorTexture) {
            const auto cursor = effects->cursorImage();
            QSizeF cursorSize = QSizeF(cursor.image().size()) / cursor.image().devicePixelRatio();
            qDebug() << "Cursor image size:" << cursor.image().size();
            qDebug() << "Cursor devicePixelRatio:" << cursor.image().devicePixelRatio();

            qDebug() << "Cursor logical size:" << cursorSize;

            QPointF hotspotOffset = cursor.hotSpot();
            qDebug() << "Cursor hotspot (before scaling):" << hotspotOffset;

            if (m_mousePointer == MousePointerScale) {
                cursorSize *= state->zoom;
                hotspotOffset *= state->zoom;
                qDebug() << "Cursor scaled by zoom:" << state->zoom;
                qDebug() << "  New size:" << cursorSize;
                qDebug() << "  New hotspot:" << hotspotOffset;
            }

            const QPointF globalP = effects->cursorPos() - hotspotOffset;
            qDebug() << "Cursor render position (global logical):" << globalP;

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            auto cursorShader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
            cursorShader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);

            QMatrix4x4 mvp = viewport.projectionMatrix();
            qDebug() << "Cursor initial projection matrix:" << mvp;

            mvp.translate(globalP.x(), globalP.y());
            qDebug() << "Cursor after translate to" << globalP << ":" << mvp;

            cursorShader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
            qDebug() << "Rendering cursor texture, size:" << cursorSize;
            cursorTexture->render(cursorSize);

            ShaderManager::instance()->popShader();
            glDisable(GL_BLEND);
            qDebug() << "Cursor rendered";
        } else {
            qWarning() << "Failed to get cursor texture!";
        }
    } else {
        if (m_mousePointer == MousePointerHide) {
            qDebug() << "Cursor hidden (MousePointerHide mode)";
        } else {
            qDebug() << "Cursor not on this screen";
            qDebug() << "  Cursor screen:" << (effects->screenAt(effects->cursorPos().toPoint()) ? effects->screenAt(effects->cursorPos().toPoint())->name() : "null");
            qDebug() << "  This screen:" << screen->name();
        }
    }

    qDebug() << "=== ZoomEffect::paintScreen END ===\n";
}
void ZoomEffect::postPaintScreen()
{
    // Call base implementation first to maintain proper effect chain
    effects->postPaintScreen();

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
        // Either animation is running or the zoom effect is active
        effects->addRepaintFull();
    }
}

void ZoomEffect::zoomIn()
{
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    setTargetZoom(screen, s->targetZoom + m_customZoomStep);
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
}

void ZoomEffect::zoomInStep()
{
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    setTargetZoom(screen, s->targetZoom + m_customZoomStep);
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
}

void ZoomEffect::zoomTo(double to)
{
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
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
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
}

void ZoomEffect::zoomTo14()
{
    zoomTo(1.4);
}

void ZoomEffect::zoomOut()
{
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);

    s->sourceZoom = s->zoom;
    setTargetZoom(screen, s->targetZoom - m_customZoomStep);
    if ((m_customZoomStep > 1 && s->targetZoom < 1.01) || (m_customZoomStep < 1 && s->targetZoom > 0.99)) {
        setTargetZoom(screen, 1);
    }
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
}

void ZoomEffect::zoomOutStep()
{
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    s->sourceZoom = s->zoom;
    setTargetZoom(screen, s->targetZoom - m_customZoomStep);
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
}

void ZoomEffect::actualSize()
{
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    s->sourceZoom = s->zoom;
    setTargetZoom(screen, 1);
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
}

void ZoomEffect::zoomInDBus()
{
    zoomIn();
}

void ZoomEffect::zoomOutDBus()
{
    zoomOut();
}

void ZoomEffect::resetZoomDBus()
{
    actualSize();
}

void ZoomEffect::zoomTo140DBus()
{
    zoomTo(1.4);
}

void ZoomEffect::zoomToValueDBus(double value)
{
    // Validate the zoom value - reasonable range is 0.1 to 10.0
    if (value < 0.1 || value > 10.0) {
        // Optionally log an error or send a DBus error
        return;
    }
    zoomTo(value);
}

double ZoomEffect::getZoomLevelDBus()
{
    // Get the zoom level for the active screen
    if (auto *screen = effects->activeScreen()) {
        const ZoomScreenState *state = stateForScreen(screen);
        return state->zoom; // Return current zoom level
    }
    return 1.0; // Default zoom level if no active screen
}

void ZoomEffect::setZoomStepDBus(double step)
{
    // Validate the zoom step - reasonable range is 0.1 to 5.0
    if (step >= 0.1 && step <= 5.0) {
        m_customZoomStep = step;
    }
}

void ZoomEffect::setAnimationSpeedDBus(double speed)
{
    // Validate the animation speed - reasonable range is 0.01 to 1.0
    if (speed >= 0.01 && speed <= 1.0) {
        m_animationSpeed = speed;
    }
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

    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
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
    LogicalOutput *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);

    if (KWindowSystem::isPlatformWayland() || !ZoomEffect::isActive()) {
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
    LogicalOutput *screen = effects->screenAt(pos.toPoint());
    if (screen) {
        ZoomScreenState *s = stateForScreen(screen);
        // Always update focus point when mouse moves
        s->focusPoint = pos.toPoint();

        // Only trigger repaint if zoom is active
        if (s->zoom != 1.0 || s->targetZoom != 1.0) {
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

void ZoomEffect::slotScreenRemoved(LogicalOutput *screen)
{
    if (auto it = m_offscreenData.find(screen); it != m_offscreenData.end()) {
        effects->makeOpenGLContextCurrent();
        m_offscreenData.erase(it);
    }
    m_states.erase(screen);
}

void ZoomEffect::moveFocus(const QPoint &point)
{
    LogicalOutput *screen = effects->screenAt(point);
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
    const LogicalOutput *output = effects->screenAt(point);
    return output && output->geometry().contains(point);
}

ZoomEffect::ZoomScreenState *ZoomEffect::stateForScreen(LogicalOutput *output)
{
    auto it = m_states.find(output);
    if (it == m_states.end()) {
        it = m_states.emplace(output, ZoomScreenState()).first;

        // Initialize ALL zoom values
        it->second.zoom = 1.0;
        it->second.targetZoom = 1.0;
        it->second.sourceZoom = 1.0;

        // Initialize focus point to current cursor if on this screen, otherwise center
        if (output->geometry().contains(effects->cursorPos().toPoint())) {
            it->second.focusPoint = effects->cursorPos().toPoint();
        } else {
            it->second.focusPoint = output->geometry().center();
        }
        it->second.prevPoint = it->second.focusPoint;
        it->second.xMove = 0;
        it->second.yMove = 0;
    }
    return &it->second;
}

const ZoomEffect::ZoomScreenState *ZoomEffect::stateForScreen(LogicalOutput *output) const
{
    auto it = m_states.find(output);
    if (it == m_states.end()) {
        // Should not happen in const context if properly managed, but fallback
        static ZoomScreenState defaultState;
        return &defaultState;
    }
    return &it->second;
}

void ZoomEffect::setTargetZoom(LogicalOutput *output, double value)
{
    value = std::min(value, 100.0);
    ZoomScreenState *s = stateForScreen(output);

    // Check if any screen was active before
    bool anyOldActive = false;
    for (const auto &[scr, state] : m_states) {
        if (state.targetZoom != 1.0) {
            anyOldActive = true;
            break;
        }
    }

    // Only update sourceZoom if we're starting a new animation
    if (s->targetZoom != value) {
        s->sourceZoom = s->zoom; // Set source to current zoom level
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