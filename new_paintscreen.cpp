void ZoomEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen)
{
    // Check if ANY screen needs zoom
    bool anyZoom = false;
    for (const auto &[scr, state] : m_states) {
        if (state.zoom != 1.0 || state.targetZoom != 1.0) {
            anyZoom = true;
            break;
        }
    }

    if (!anyZoom) {
        // No zoom active, just pass through
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }

    // Step 1: Render entire desktop to offscreen buffer ONCE
    OffscreenData *fullscreenData = ensureOffscreenData(renderTarget, viewport, screen);
    if (!fullscreenData) {
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }

    // Render to offscreen
    RenderTarget offscreenTarget(fullscreenData->framebuffer.get(), renderTarget.colorDescription());
    RenderViewport offscreenViewport(viewport.renderRect(), viewport.scale(), offscreenTarget);

    GLFramebuffer::pushFramebuffer(fullscreenData->framebuffer.get());
    glViewport(0, 0, fullscreenData->texture->width(), fullscreenData->texture->height());
    effects->paintScreen(offscreenTarget, offscreenViewport, mask, region, screen);
    GLFramebuffer::popFramebuffer();

    // Step 2: Now composite each monitor with zoom from the offscreen buffer
    const auto outputs = effects->screens();
    const auto scale = viewport.scale();

    for (Output *out : outputs) {
        ZoomScreenState *state = stateForScreen(out);
        const QRect geo = out->geometry();

        // Skip if this monitor doesn't need zoom
        if (state->zoom == 1.0 && state->targetZoom == 1.0) {
            continue;
        }

        // Convert to local coordinates
        QPoint localFocus = state->focusPoint - geo.topLeft();
        QPoint localPrev = state->prevPoint - geo.topLeft();

        qreal xTranslation = 0;
        qreal yTranslation = 0;

        // Calculate zoom translation (your existing logic)
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
            int tY = int((geo.height() / 2.0) - localPrev.y() * state->zoom);

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
                state->yMove = (y - threshold) / state->zoom;
            } else if (y > geo.height() - threshold) {
                state->xMove = (y + threshold - geo.height()) / state->zoom;
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
            break;
        }
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

        // Clamp
        if (m_mouseTracking != MouseTrackingDisabled && m_mouseTracking != MouseTrackingCentered) {
            int minX = int(geo.width() * (1.0 - state->zoom));
            int maxX = 0;
            int minY = int(geo.height() * (1.0 - state->zoom));
            int maxY = 0;

            xTranslation = std::clamp(int(xTranslation), minX, maxX);
            yTranslation = std::clamp(int(yTranslation), minY, maxY);
        }

        // Scissor to this output
        glEnable(GL_SCISSOR_TEST);
        const int scissorY = renderTarget.size().height() - (geo.y() + geo.height()) * scale;
        glScissor(geo.x() * scale, scissorY * scale, geo.width() * scale, geo.height() * scale);

        GLShader *shader = shaderForZoom(state->zoom);
        ShaderManager::instance()->pushShader(shader);

        // Calculate texture coordinates for this monitor's region in the full offscreen buffer
        // Texture coords are normalized (0.0 to 1.0)
        const float fullWidth = fullscreenData->texture->width();
        const float fullHeight = fullscreenData->texture->height();

        // Source rectangle in texture space (where to sample from)
        const float texX = static_cast<float>(geo.x()) / static_cast<float>(fullWidth);
        const float texY = static_cast<float>(geo.y()) / static_cast<float>(fullHeight);
        const float texW = static_cast<float>(geo.width()) / static_cast<float>(fullWidth);
        const float texH = static_cast<float>(geo.height()) / static_cast<float>(fullHeight);

        QMatrix4x4 matrix = viewport.projectionMatrix();

        // Move to screen position
        matrix.translate(geo.x() * scale, geo.y() * scale);
        // Apply zoom pan
        matrix.translate(xTranslation * scale, yTranslation * scale);
        // Scale
        matrix.scale(state->zoom, state->zoom);

        shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
        shader->setUniform(GLShader::IntUniform::TextureWidth, fullscreenData->texture->width());
        shader->setUniform(GLShader::IntUniform::TextureHeight, fullscreenData->texture->height());
        shader->setColorspaceUniforms(fullscreenData->color, renderTarget.colorDescription(), RenderingIntent::Perceptual);

        // Render just this monitor's portion of the texture using vertex buffer
        glBindTexture(GL_TEXTURE_2D, fullscreenData->texture->texture());

        // Draw quad with correct texture coordinates
        const float x1 = 0;
        const float y1 = 0;
        const float x2 = geo.width() * scale;
        const float y2 = geo.height() * scale;

        GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));

        GLVertex2D vertices[6];
        // Triangle 1
        vertices[0] = {{x1, y1}, {texX, texY}};
        vertices[1] = {{x2, y1}, {texX + texW, texY}};
        vertices[2] = {{x2, y2}, {texX + texW, texY + texH}};
        // Triangle 2
        vertices[3] = {{x2, y2}, {texX + texW, texY + texH}};
        vertices[4] = {{x1, y2}, {texX, texY + texH}};
        vertices[5] = {{x1, y1}, {texX, texY}};

        vbo->write(vertices, sizeof(vertices));
        vbo->render(GL_TRIANGLES);

        ShaderManager::instance()->popShader();
        glDisable(GL_SCISSOR_TEST);

        // Draw cursor (your existing code)
        if (m_mousePointer != MousePointerHide && state->zoom != 1.0) {
            if (effects->screenAt(effects->cursorPos().toPoint()) == out) {
                GLTexture *cursorTexture = ensureCursorTexture();
                if (cursorTexture) {
                    const auto cursor = effects->cursorImage();
                    QSizeF cursorSize = QSizeF(cursor.image().size()) / cursor.image().devicePixelRatio();
                    if (m_mousePointer == MousePointerScale) {
                        cursorSize *= state->zoom;
                    }

                    // Cursor in local coordinates
                    QPoint localCursor = effects->cursorPos().toPoint() - geo.topLeft();
                    const QPointF p = (localCursor - cursor.hotSpot()) * state->zoom + QPoint(xTranslation, yTranslation);
                    const QPointF globalP = p + geo.topLeft();

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

    // For outputs that don't need zoom, render them normally
    for (Output *out : outputs) {
        ZoomScreenState *state = stateForScreen(out);
        if (state->zoom == 1.0 && state->targetZoom == 1.0) {
            // Draw the unzoomed content for this output
            const QRect geo = out->geometry();

            glEnable(GL_SCISSOR_TEST);
            const int scissorY = renderTarget.size().height() - (geo.y() + geo.height()) * scale;
            glScissor(geo.x() * scale, scissorY * scale, geo.width() * scale, geo.height() * scale);

            // Use a simple shader to draw the unzoomed content from the fullscreen buffer
            GLShader *shader = ShaderManager::instance()->shader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
            ShaderManager::instance()->pushShader(shader);

            // Calculate texture coordinates for this monitor's region
            const float fullWidth = fullscreenData->texture->width();
            const float fullHeight = fullscreenData->texture->height();

            const float texX = static_cast<float>(geo.x()) / static_cast<float>(fullWidth);
            const float texY = static_cast<float>(geo.y()) / static_cast<float>(fullHeight);
            const float texW = static_cast<float>(geo.width()) / static_cast<float>(fullWidth);
            const float texH = static_cast<float>(geo.height()) / static_cast<float>(fullHeight);

            QMatrix4x4 matrix = viewport.projectionMatrix();
            matrix.translate(geo.x() * scale, geo.y() * scale);

            shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
            shader->setUniform(GLShader::IntUniform::TextureWidth, fullscreenData->texture->width());
            shader->setUniform(GLShader::IntUniform::TextureHeight, fullscreenData->texture->height());
            shader->setColorspaceUniforms(fullscreenData->color, renderTarget.colorDescription(), RenderingIntent::Perceptual);

            glBindTexture(GL_TEXTURE_2D, fullscreenData->texture->texture());

            const float x1 = 0;
            const float y1 = 0;
            const float x2 = geo.width() * scale;
            const float y2 = geo.height() * scale;

            GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
            vbo->reset();
            vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));

            GLVertex2D vertices[6];
            // Triangle 1
            vertices[0] = {{x1, y1}, {texX, texY}};
            vertices[1] = {{x2, y1}, {texX + texW, texY}};
            vertices[2] = {{x2, y2}, {texX + texW, texY + texH}};
            // Triangle 2
            vertices[3] = {{x2, y2}, {texX + texW, texY + texH}};
            vertices[4] = {{x1, y2}, {texX, texY + texH}};
            vertices[5] = {{x1, y1}, {texX, texY}};

            vbo->write(vertices, sizeof(vertices));
            vbo->render(GL_TRIANGLES);

            ShaderManager::instance()->popShader();
            glDisable(GL_SCISSOR_TEST);
        }
    }
}