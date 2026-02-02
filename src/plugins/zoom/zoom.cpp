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

#if HAVE_ACCESSIBILITY
#include "accessibilityintegration.h"
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
    , m_zoomFactor(1.25)
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

    const QList<Output *> screens = effects->screens();
    for (Output *screen : screens) {
        OffscreenData &data = m_offscreenData[screen];
        data.viewport = screen->geometry();
    }

    reconfigure(ReconfigureAll);
}

ZoomEffect::~ZoomEffect()
{
    // switch off and free resources
    showCursor();

    // Make sure OpenGL context is current before destroying GPU resources
    if (effects) {
        effects->makeOpenGLContextCurrent();

        // Clean up all offscreen data
        for (auto &[screen, data] : m_offscreenData) {
            data.framebuffer.reset();
            data.texture.reset();
        }
        m_offscreenData.clear();
    }
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

            // Use proportional speed for larger changes
            const float animSpeed = (zoomDist * time) / animationTime(std::chrono::milliseconds(100));

            if (state.targetZoom > state.zoom) {
                state.zoom = std::min(state.zoom + animSpeed, state.targetZoom);
            } else {
                state.zoom = std::max(state.zoom - animSpeed, state.targetZoom);
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
    // When zoom is less than the pixel grid threshold, use the basic shader
    // When zoom is greater than or equal to the pixel grid threshold, use the pixel grid shader
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

void ZoomEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen)
{
    // Check if ANY screen needs zoom
    bool anyZoomActive = false;
    for (const auto &[scr, state] : m_states) {
        if (state.zoom != 1.0 || state.targetZoom != 1.0) {
            anyZoomActive = true;
            break;
        }
    }

    if (!anyZoomActive) {
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }

    const auto outputs = effects->screens();
    const auto scale = viewport.scale();

    // First pass: Render scene normally
    effects->paintScreen(renderTarget, viewport, mask, region, screen);

    // Second pass: For zoomed outputs only
    for (Output *out : outputs) {
        ZoomScreenState *state = stateForScreen(out);

        if (state->zoom == 1.0 && state->targetZoom == 1.0) {
            continue;
        }

        const QRect geo = out->geometry();
        const QSize outputSize(geo.width() * scale, geo.height() * scale);

        OffscreenData &data = m_offscreenData[out];

        // Optimize texture allocation: only reallocate if size changed significantly
        if (!data.texture || data.texture->size() != outputSize) {
            const GLenum textureFormat = renderTarget.colorDescription() == ColorDescription::sRGB ? GL_RGBA8 : GL_RGBA16F;
            data.texture = GLTexture::allocate(textureFormat, outputSize);
            if (!data.texture) {
                continue;
            }
            data.texture->setFilter(GL_LINEAR);
            data.texture->setWrapMode(GL_CLAMP_TO_EDGE);
            data.framebuffer = std::make_unique<GLFramebuffer>(data.texture.get());

            // Validate the framebuffer is created successfully
            if (!data.framebuffer || !data.framebuffer->valid()) {
                qCritical() << "Failed to create valid framebuffer for zoom effect";
                continue;
            }
        } else {
            // Ensure existing framebuffer is valid
            if (!data.framebuffer || !data.framebuffer->valid()) {
                data.framebuffer = std::make_unique<GLFramebuffer>(data.texture.get());
                if (!data.framebuffer || !data.framebuffer->valid()) {
                    qCritical() << "Failed to create valid framebuffer for zoom effect";
                    continue;
                }
            }
        }

        data.viewport = QRect(0, 0, geo.width(), geo.height());
        data.color = renderTarget.colorDescription();
        data.texture->setContentTransform(renderTarget.transform());

        // Render to offscreen
        RenderTarget offscreenTarget(data.framebuffer.get(), renderTarget.colorDescription());
        RenderViewport offscreenViewport(geo, scale, offscreenTarget);

        QRegion outputRegion = region.intersected(geo);
        outputRegion.translate(-geo.topLeft());

        // Optimize: Only render if there are visible windows that intersect with this output
        const auto windows = effects->stackingOrder();
        bool hasVisibleWindows = false;
        for (EffectWindow *w : windows) {
            if (w->isVisible() && w->isOnCurrentDesktop() && w->frameGeometry().intersects(geo)) {
                hasVisibleWindows = true;
                break;
            }
        }

        // Validate framebuffer before using it
        if (!data.framebuffer || !data.framebuffer->valid()) {
            qCritical() << "Invalid framebuffer during rendering phase";
            continue;
        }

        GLFramebuffer::pushFramebuffer(data.framebuffer.get());
        glViewport(0, 0, geo.width() * scale, geo.height() * scale);
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (hasVisibleWindows) {
            effects->paintScreen(offscreenTarget, offscreenViewport, mask, outputRegion, out);
        }

        GLFramebuffer::popFramebuffer();

        // Convert global coordinates to local output coordinates
        QPoint localFocus = state->focusPoint - geo.topLeft();
        QPoint localPrev = state->prevPoint - geo.topLeft();

        qreal xTranslation = 0;
        qreal yTranslation = 0;

        switch (m_mouseTracking) {
        case MouseTrackingProportional:
            xTranslation = -int(localFocus.x() * (state->zoom - 1.0));
            yTranslation = -int(localFocus.y() * (state->zoom - 1.0));
            state->prevPoint = state->focusPoint;
            break;

        case MouseTrackingCentered:
            state->prevPoint = state->focusPoint;
            // fall through

        case MouseTrackingDisabled: {
            int tX = int(geo.width() / 2.0 - localPrev.x() * state->zoom);
            int tY = int(geo.height() / 2.0 - localPrev.y() * state->zoom);

            int minX = int(geo.width() * (1.0 - state->zoom));
            int maxX = 0;
            int minY = int(geo.height() * (1.0 - state->zoom));
            int maxY = 0;

            xTranslation = std::clamp(tX, minX, maxX);
            yTranslation = std::clamp(tY, minY, maxY);
        } break;

        case MouseTrackingPush: {
            const int x = localFocus.x() * state->zoom - localPrev.x() * (state->zoom - 1.0);
            const int y = localFocus.y() * state->zoom - localPrev.y() * (state->zoom - 1.0);
            const int threshold = 4;

            state->xMove = state->yMove = 0;

            if (x < threshold) {
                state->xMove = (x - threshold) / state->zoom;
            } else if (x > geo.width() - threshold) {
                state->xMove = (x + threshold - geo.width()) / state->zoom;
            }

            if (y < threshold) {
                state->yMove = (y - threshold) / state->zoom; // FIXED: was xMove
            } else if (y > geo.height() - threshold) {
                state->yMove = (y + threshold - geo.height()) / state->zoom; // FIXED: was xMove
            }

            if (state->xMove) {
                state->prevPoint.setX(state->prevPoint.x() + state->xMove);
            }
            if (state->yMove) {
                state->prevPoint.setY(state->prevPoint.y() + state->yMove);
            }

            localPrev = state->prevPoint - geo.topLeft();
            xTranslation = -int(localPrev.x() * (state->zoom - 1.0));
            yTranslation = -int(localPrev.y() * (state->zoom - 1.0));
        } break;
        }

        // Focus tracking
        if (isFocusTrackingEnabled() || isTextCaretTrackingEnabled()) {
            bool acceptFocus = true;
            if (m_mouseTracking != MouseTrackingDisabled && m_focusDelay > 0) {
                const int msecs = m_lastMouseEvent.msecsTo(m_lastFocusEvent);
                acceptFocus = msecs > m_focusDelay;
            }
            if (acceptFocus) {
                xTranslation = -int(localFocus.x() * (state->zoom - 1.0));
                yTranslation = -int(localFocus.y() * (state->zoom - 1.0));
                state->prevPoint = state->focusPoint;
            }
        }

        // Clamp to prevent black borders
        if (m_mouseTracking != MouseTrackingDisabled && m_mouseTracking != MouseTrackingCentered) {
            int minX = int(geo.width() * (1.0 - state->zoom));
            int maxX = 0;
            int minY = int(geo.height() * (1.0 - state->zoom));
            int maxY = 0;

            xTranslation = std::clamp(int(xTranslation), minX, maxX);
            yTranslation = std::clamp(int(yTranslation), minY, maxY);
        }

        // Validate texture before compositing
        if (!data.texture || data.texture->texture() == 0) {
            qCritical() << "Invalid texture during compositing phase";
            glDisable(GL_SCISSOR_TEST);
            continue;
        }

        // Clear and composite
        glEnable(GL_SCISSOR_TEST);
        const int scissorY = renderTarget.size().height() - (geo.y() + geo.height());
        glScissor(geo.x() * scale, scissorY * scale, geo.width() * scale, geo.height() * scale);

        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        GLShader *shader = shaderForZoom(state->zoom);
        if (!shader || !shader->isValid()) {
            qCritical() << "Invalid shader during compositing phase";
            glDisable(GL_SCISSOR_TEST);
            continue;
        }

        ShaderManager::instance()->pushShader(shader);

        QMatrix4x4 matrix = viewport.projectionMatrix();

        // Move quad to the output's position on screen
        matrix.translate(geo.x() * scale, geo.y() * scale);

        // Apply zoom pan (moves the content within the output)
        matrix.translate(xTranslation * scale, yTranslation * scale);

        // Apply zoom scale (scales the content)
        matrix.scale(state->zoom, state->zoom);
        shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
        shader->setUniform(GLShader::IntUniform::TextureWidth, data.texture->width());
        shader->setUniform(GLShader::IntUniform::TextureHeight, data.texture->height());
        shader->setColorspaceUniforms(data.color, renderTarget.colorDescription(), RenderingIntent::Perceptual);

        glBindTexture(GL_TEXTURE_2D, data.texture->texture());

        const float x1 = 0;
        const float y1 = 0;
        const float x2 = geo.width() * scale;
        const float y2 = geo.height() * scale;

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
        vbo->render(GL_TRIANGLES);

        ShaderManager::instance()->popShader();
        glDisable(GL_SCISSOR_TEST);

        // Draw cursor
        if (m_mousePointer != MousePointerHide && effects->screenAt(effects->cursorPos().toPoint()) == out) {
            GLTexture *cursorTexture = ensureCursorTexture();
            if (cursorTexture) {
                const auto cursor = effects->cursorImage();
                QSizeF cursorSize = QSizeF(cursor.image().size()) / cursor.image().devicePixelRatio();

                // Handle cursor scaling
                QPointF hotspotOffset = cursor.hotSpot();
                if (m_mousePointer == MousePointerScale) {
                    cursorSize *= state->zoom;
                    hotspotOffset *= state->zoom;
                }

                // The cursor stays at its REAL global position
                // We do NOT apply xTranslation/yTranslation because those pan the CONTENT, not the cursor
                const QPointF globalP = effects->cursorPos() - hotspotOffset;

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                auto cursorShader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
                cursorShader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
                QMatrix4x4 mvp = viewport.projectionMatrix();
                mvp.translate(globalP.x() * scale, globalP.y() * scale);
                cursorShader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
                cursorTexture->render(cursorSize * scale);
                ShaderManager::instance()->popShader();
                glDisable(GL_BLEND);
            }
        }
    }
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

    // Only repaint if there's actual animation happening or if zoom is active
    // The zoom effect inherently requires continuous repainting when active because
    // the zoomed region follows the mouse/cursor/focus, so we need to keep this
    if (anyZooming || isActive()) {
        effects->addRepaintFull();
    }
}

void ZoomEffect::zoomIn()
{
    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    setTargetZoom(screen, s->targetZoom * m_zoomFactor);
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
    effects->addRepaintFull();
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
    s->prevPoint = s->focusPoint;
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
    s->focusPoint = effects->cursorPos().toPoint();
    s->prevPoint = s->focusPoint;
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
    Output *screen = effects->screenAt(pos.toPoint());
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

void ZoomEffect::slotScreenRemoved(Output *screen)
{
    if (auto it = m_offscreenData.find(screen); it != m_offscreenData.end()) {
        effects->makeOpenGLContextCurrent();
        // Explicitly reset the framebuffer and texture to ensure proper cleanup
        it->second.framebuffer.reset();
        it->second.texture.reset();
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
