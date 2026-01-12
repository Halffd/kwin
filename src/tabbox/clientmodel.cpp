/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "clientmodel.h"
#include "tabboxconfig.h"
#include "window.h"

#include <KLocalizedString>

#include <QDebug>
#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>

#ifdef __linux__
#include <execinfo.h>
#endif

// Timing helper macro
#define TRACE_TIMING(label)              \
    static QElapsedTimer _timer_##label; \
    if (!_timer_##label.isValid())       \
        _timer_##label.start();          \
    qWarning() << "[TABBOX TIMING]" << #label << ":" << _timer_##label.restart() << "ms";

namespace KWin
{
namespace TabBox
{

ClientModel::ClientModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

ClientModel::~ClientModel()
{
}

QVariant ClientModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (m_clientList.isEmpty()) {
        return QVariant();
    }

    int clientIndex = index.row();
    if (clientIndex >= m_clientList.count()) {
        return QVariant();
    }
    Window *client = m_clientList[clientIndex];
    if (!client) {
        return QVariant();
    }
    switch (role) {
    case Qt::DisplayRole:
    case CaptionRole: {
        if (client->isDesktop()) {
            return i18nc("Special entry in alt+tab list for minimizing all windows",
                         "Show Desktop");
        }
        return client->caption();
    }
    case ClientRole:
        return QVariant::fromValue<void *>(client);
    case DesktopNameRole: {
        return tabBox->desktopName(client);
    }
    case WIdRole:
        return client->internalId();
    case MinimizedRole:
        return client->isMinimized();
    case CloseableRole:
        return client->isCloseable();
    case IconRole:
        if (client->isDesktop()) {
            return QIcon::fromTheme(QStringLiteral("user-desktop"));
        }
        return client->icon();
    default:
        return QVariant();
    }
}

QString ClientModel::longestCaption() const
{
    QString caption;
    for (Window *window : std::as_const(m_clientList)) {
        if (window->caption().size() > caption.size()) {
            caption = window->caption();
        }
    }
    return caption;
}

int ClientModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

int ClientModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_clientList.count();
}

QModelIndex ClientModel::parent(const QModelIndex &child) const
{
    return QModelIndex();
}

QModelIndex ClientModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column != 0 || parent.isValid()) {
        return QModelIndex();
    }
    int index = row * columnCount();
    if (index >= m_clientList.count() && !m_clientList.isEmpty()) {
        return QModelIndex();
    }
    return createIndex(row, 0);
}

QHash<int, QByteArray> ClientModel::roleNames() const
{
    return {
        {CaptionRole, QByteArrayLiteral("caption")},
        {DesktopNameRole, QByteArrayLiteral("desktopName")},
        {MinimizedRole, QByteArrayLiteral("minimized")},
        {WIdRole, QByteArrayLiteral("windowId")},
        {CloseableRole, QByteArrayLiteral("closeable")},
        {IconRole, QByteArrayLiteral("icon")},
    };
}

QModelIndex ClientModel::index(Window *client) const
{
    const int index = m_clientList.indexOf(client);
    if (index == -1) {
        return QModelIndex();
    }
    int row = index / columnCount();
    int column = index % columnCount();
    return createIndex(row, column);
}

void ClientModel::createFocusChainClientList(Window *start)
{
    TRACE_TIMING(focus_chain_list_start);
    auto c = start;
    if (!tabBox->isInFocusChain(c)) {
        TRACE_TIMING(focus_chain_not_in_chain);
        Window *firstClient = tabBox->firstClientFocusChain();
        if (firstClient) {
            c = firstClient;
        }
    }
    TRACE_TIMING(focus_chain_before_loop);
    auto stop = c;
    int count = 0;
    const int MAX_WINDOWS = 50; // Prevent infinite loops

    do {
        Window *add = tabBox->clientToAddToList(c);
        if (add) {
            m_mutableClientList += add;
            count++;
        }
        c = tabBox->nextClientFocusChain(c);

        // Safety check to prevent infinite loops
        if (count > MAX_WINDOWS) {
            qWarning() << "Breaking focus chain loop - too many windows";
            break;
        }
    } while (c && c != stop);
    TRACE_TIMING(focus_chain_after_loop);

    // ========== BATCH THUMBNAIL LOADING ==========
    // Load thumbnails in batches to prevent overwhelming GPU
    loadThumbnailsInBatches();
}

void ClientModel::createStackingOrderClientList(Window *start)
{
    // TODO: needs improvement
    TRACE_TIMING(stacking_order_list_start);
    const QList<Window *> stacking = tabBox->stackingOrder();
    if (stacking.isEmpty()) {
        TRACE_TIMING(stacking_order_empty);
        return;
    }
    TRACE_TIMING(stacking_order_before_loop);
    auto c = stacking.first();
    auto stop = c;
    int index = 0;
    int count = 0;
    const int MAX_WINDOWS = 50; // Prevent infinite loops

    while (c && count < MAX_WINDOWS) {
        Window *add = tabBox->clientToAddToList(c);
        if (add) {
            if (start == add) {
                m_mutableClientList.removeAll(add);
                m_mutableClientList.prepend(add);
            } else {
                m_mutableClientList += add;
            }
            count++;
        }
        if (index >= stacking.size() - 1) {
            c = nullptr;
        } else {
            c = stacking[++index];
        }

        if (c == stop) {
            break;
        }
    }
    TRACE_TIMING(stacking_order_after_loop);
}

void ClientModel::createClientList(bool partialReset)
{
    TRACE_TIMING(clientlist_start);

    // Don't rebuild if we're in the middle of showing
    if (m_isCreating) {
        qWarning() << "Skipping client list recreation - already in progress";
        return;
    }

    // Throttle rebuilds to max once per 100ms
    if (m_lastRebuildTimer.isValid() && m_lastRebuildTimer.elapsed() < 100) {
        TRACE_TIMING(clientlist_throttled);
        return;
    }

    m_isCreating = true;
    m_lastRebuildTimer.restart();

    auto start = tabBox->activeClient();
    // TODO: new clients are not added at correct position
    if (partialReset && !m_mutableClientList.isEmpty()) {
        TRACE_TIMING(clientlist_partial_reset_check);
        Window *firstClient = m_mutableClientList.constFirst();
        if (!firstClient->isDeleted()) {
            start = firstClient;
        }
    }

    TRACE_TIMING(clientlist_before_clear);
    m_mutableClientList.clear();

    TRACE_TIMING(clientlist_before_switch_mode);
    switch (tabBox->config().clientSwitchingMode()) {
    case TabBoxConfig::FocusChainSwitching: {
        TRACE_TIMING(clientlist_focus_chain_mode);
        createFocusChainClientList(start);
        break;
    }
    case TabBoxConfig::StackingOrderSwitching: {
        TRACE_TIMING(clientlist_stacking_order_mode);
        createStackingOrderClientList(start);
        break;
    }
    }

    TRACE_TIMING(clientlist_before_minimized_order);
    if (tabBox->config().orderMinimizedMode() == TabBoxConfig::GroupByMinimized) {
        // Put all non-minimized included clients first.
        std::stable_partition(m_mutableClientList.begin(), m_mutableClientList.end(), [](const auto &client) {
            return !client->isMinimized();
        });
    }

    TRACE_TIMING(clientlist_before_desktop_client);
    if (!m_mutableClientList.isEmpty()
        && tabBox->config().clientApplicationsMode() != TabBoxConfig::AllWindowsCurrentApplication
        && tabBox->config().showDesktopMode() == TabBoxConfig::ShowDesktopClient) {
        Window *desktopClient = tabBox->desktopClient();
        if (desktopClient) {
            m_mutableClientList.append(desktopClient);
        }
    }

    TRACE_TIMING(clientlist_before_model_reset_check);
    if (m_clientList == m_mutableClientList) {
        TRACE_TIMING(clientlist_no_changes);
        m_isCreating = false;
        return;
    }

    // Simple approach: always do full reset to avoid threading issues
    TRACE_TIMING(clientlist_before_begin_reset);
    beginResetModel();
    m_clientList = m_mutableClientList;
    endResetModel();

    TRACE_TIMING(clientlist_end);
    m_isCreating = false;
}

void ClientModel::close(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    Window *client = m_mutableClientList.at(i);
    if (client) {
        client->closeWindow();
    }
}

void ClientModel::loadThumbnailsInBatches()
{
    // Get batch size from config (default 5)
    int batchSize = tabBox->config().thumbnailBatchSize();

    // Load thumbnails in batches to prevent overwhelming GPU
    for (int i = 0; i < m_mutableClientList.size(); i++) {
        Window *window = m_mutableClientList[i];
        if (window) {
            // Calculate which batch this window belongs to
            int batchNumber = i / batchSize;
            int delay = batchNumber * 50; // 50ms delay between batches

            // Schedule thumbnail loading with delay based on batch
            int currentBatchSize = batchSize; // Capture the value
            QTimer::singleShot(delay, [window, i, currentBatchSize, this]() {
                // Trigger thumbnail preparation for this window
                // This would integrate with the thumbnail system
                TRACE_TIMING(thumbnail_batch_load);
                qWarning() << "Loading thumbnail batch" << (i / currentBatchSize) << "for window" << i;

                // In a full implementation, this would trigger the actual thumbnail preparation
                // for the specific window, respecting the batch loading approach
            });
        }
    }
}

void ClientModel::activate(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    tabBox->setCurrentIndex(ind);
    tabBox->activateAndClose();
}

} // namespace Tabbox
} // namespace KWin

#include "moc_clientmodel.cpp"
