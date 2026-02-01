/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QQuickItem>
#include <QUuid>

#include <epoxy/gl.h>

namespace KWin
{
class Window;
class GLFramebuffer;
class GLTexture;
class ThumbnailTextureProvider;
class WindowThumbnailSource;

class WindowThumbnailSource : public QObject
{
    Q_OBJECT

public:
    WindowThumbnailSource(QQuickWindow *view, Window *handle);
    ~WindowThumbnailSource() override;

    static std::shared_ptr<WindowThumbnailSource> getOrCreate(QQuickWindow *window, Window *handle);

    struct Frame
    {
        std::shared_ptr<GLTexture> texture;
        GLsync fence;
    };

    Frame acquire();

Q_SIGNALS:
    void changed();

private:
    void update();

    QPointer<QQuickWindow> m_view;
    QPointer<Window> m_handle;

    std::shared_ptr<GLTexture> m_offscreenTexture;
    std::unique_ptr<GLFramebuffer> m_offscreenTarget;
    GLsync m_acquireFence = 0;
    bool m_dirty = true;
    bool m_updating = false; // For re-entrancy protection - Issue #3
};

class WindowThumbnailItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QUuid wId READ wId WRITE setWId NOTIFY wIdChanged)
    Q_PROPERTY(KWin::Window *client READ client WRITE setClient NOTIFY clientChanged)

public:
    explicit WindowThumbnailItem(QQuickItem *parent = nullptr);
    ~WindowThumbnailItem() override;

    QUuid wId() const;
    void setWId(const QUuid &wId);

    Window *client() const;
    void setClient(Window *client);

    QSGTextureProvider *textureProvider() const override;
    bool isTextureProvider() const override;
    QSGNode *updatePaintNode(QSGNode *oldNode, QQuickItem::UpdatePaintNodeData *) override;

protected:
    void releaseResources() override;
    void itemChange(QQuickItem::ItemChange change, const QQuickItem::ItemChangeData &value) override;

Q_SIGNALS:
    void wIdChanged();
    void clientChanged();

private:
    QImage fallbackImage() const;
    QRectF paintedRect() const;
    void updateImplicitSize();
    void updateSource();
    void resetSource();

    QUuid m_wId;
    QPointer<Window> m_client;
    bool m_isSelected = false; // Track if this thumbnail is currently selected

    mutable ThumbnailTextureProvider *m_provider = nullptr;
    std::shared_ptr<WindowThumbnailSource> m_source;

public:
    void setSelected(bool selected)
    {
        m_isSelected = selected;
    }
    bool isSelected() const
    {
        return m_isSelected;
    }
};

} // namespace KWin
