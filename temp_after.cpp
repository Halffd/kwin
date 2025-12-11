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
    Output *screen = effects->screenAt(effects->cursorPos().toPoint());
    if (!screen) {
        return;
    }
    ZoomScreenState *s = stateForScreen(screen);
    setTargetZoom(screen, s->targetZoom * m_zoomFactor);
    // Explicitly update focus point on zoom action
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
        // Allow updating focus point if we are zooming OR if we are about to start zooming
        if (s->zoom != 1.0 || s->targetZoom != 1.0) {
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