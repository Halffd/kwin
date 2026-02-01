/*
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pluginmanager.h"
#include "dbusinterface.h"
#include "main.h"
#include "plugin.h"
#include "utils/common.h"

#include <KConfigGroup>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <QPluginLoader>

namespace KWin
{

static const QString s_pluginDirectory = KWIN_PLUGINDIR + QStringLiteral("/plugins");

static QJsonValue readPluginInfo(const QJsonObject &metadata, const QString &key)
{
    return metadata.value(QLatin1String("KPlugin")).toObject().value(key);
}

PluginManager::PluginManager()
{
    const KConfigGroup config(kwinApp()->config(), QStringLiteral("Plugins"));

    auto checkEnabled = [&config](const QString &pluginId, const QJsonObject &metadata) {
        const QString configKey = pluginId + QLatin1String("Enabled");
        if (config.hasKey(configKey)) {
            return config.readEntry(configKey, false);
        }
        return readPluginInfo(metadata, QStringLiteral("EnabledByDefault")).toBool(false);
    };

    const QList<KPluginMetaData> plugins = KPluginMetaData::findPlugins(s_pluginDirectory);
    for (const KPluginMetaData &metadata : plugins) {
        if (m_plugins.find(metadata.pluginId()) != m_plugins.end()) {
            qWarning() << "Conflicting plugin id" << metadata.pluginId();
            continue;
        }
        if (checkEnabled(metadata.pluginId(), metadata.rawData())) {
            loadPlugin(metadata);
        }
    }

    new PluginManagerDBusInterface(this);
}

PluginManager::~PluginManager() = default;

QStringList PluginManager::loadedPlugins() const
{
    QStringList ret;
    ret.reserve(m_plugins.size());
    for (const auto &[key, _] : m_plugins) {
        ret.push_back(key);
    }
    return ret;
}

QStringList PluginManager::availablePlugins() const
{
    const QList<KPluginMetaData> plugins = KPluginMetaData::findPlugins(s_pluginDirectory);

    QStringList ret;
    ret.reserve(plugins.size());

    for (const KPluginMetaData &metadata : plugins) {
        ret.append(metadata.pluginId());
    }

    return ret;
}

bool PluginManager::loadPlugin(const QString &pluginId)
{
    if (m_plugins.find(pluginId) != m_plugins.end()) {
        qDebug() << "Plugin with id" << pluginId << "is already loaded";
        return false;
    }
    const KPluginMetaData metadata = KPluginMetaData::findPluginById(s_pluginDirectory, pluginId);
    if (metadata.isValid()) {
        if (loadPlugin(metadata)) {
            return true;
        }
    }
    return false;
}

bool PluginManager::loadPlugin(const KPluginMetaData &metadata)
{
    if (!metadata.isValid()) {
        qDebug() << "PluginManager::loadPlugin needs a valid plugin metadata";
        return false;
    }

    const QString pluginId = metadata.pluginId();
    QPluginLoader pluginLoader(metadata.fileName());
    if (pluginLoader.metaData().value("IID").toString() != PluginFactory_iid) {
        qWarning() << pluginId << "has mismatching plugin version";
        return false;
    }

    std::unique_ptr<PluginFactory> factory(qobject_cast<PluginFactory *>(pluginLoader.instance()));
    if (!factory) {
        qWarning() << "Failed to get plugin factory for" << pluginId;
        return false;
    }

    if (std::unique_ptr<Plugin> plugin = factory->create()) {
        m_plugins[pluginId] = std::move(plugin);
        return true;
    } else {
        return false;
    }
}

void PluginManager::unloadPlugin(const QString &pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it != m_plugins.end()) {
        m_plugins.erase(it);
    } else {
        qWarning() << "No plugin with the specified id:" << pluginId;
    }
}

} // namespace KWin

#include "moc_pluginmanager.cpp"
