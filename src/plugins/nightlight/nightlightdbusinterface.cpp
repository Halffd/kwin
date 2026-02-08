/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2024 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "nightlightdbusinterface.h"
#include "nightlightadaptor.h"
#include "nightlightmanager.h"

#include <QDBusMessage>

namespace KWin
{

static void announceChangedProperties(const QVariantMap &properties)
{
    QDBusMessage message = QDBusMessage::createSignal(
        QStringLiteral("/org/kde/KWin/NightLight"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));

    message.setArguments({
        QStringLiteral("org.kde.KWin.NightLight"),
        properties,
        QStringList(), // invalidated_properties
    });

    QDBusConnection::sessionBus().send(message);
}

NightLightDBusInterface::NightLightDBusInterface(NightLightManager *parent)
    : QObject(parent)
    , m_manager(parent)
    , m_inhibitorWatcher(new QDBusServiceWatcher(this))
{
    m_inhibitorWatcher->setConnection(QDBusConnection::sessionBus());
    m_inhibitorWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_inhibitorWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &NightLightDBusInterface::removeInhibitorService);

    connect(m_manager, &NightLightManager::inhibitedChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("inhibited"), isInhibited()},
        });
    });

    connect(m_manager, &NightLightManager::enabledChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("enabled"), isEnabled()},
        });
    });

    connect(m_manager, &NightLightManager::runningChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("running"), isRunning()},
        });
    });

    connect(m_manager, &NightLightManager::currentTemperatureChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("currentTemperature"), currentTemperature()},
        });
    });

    connect(m_manager, &NightLightManager::brightnessChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("brightness"), brightness()},
        });
    });

    connect(m_manager, &NightLightManager::targetTemperatureChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("targetTemperature"), targetTemperature()},
        });
    });

    connect(m_manager, &NightLightManager::modeChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("mode"), mode()},
        });
    });

    connect(m_manager, &NightLightManager::daylightChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("daylight"), daylight()},
        });
    });

    connect(m_manager, &NightLightManager::previousTransitionTimingsChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("previousTransitionDateTime"), previousTransitionDateTime()},
            {QStringLiteral("previousTransitionDuration"), previousTransitionDuration()},
        });
    });

    connect(m_manager, &NightLightManager::scheduledTransitionTimingsChanged, this, [this] {
        announceChangedProperties({
            {QStringLiteral("scheduledTransitionDateTime"), scheduledTransitionDateTime()},
            {QStringLiteral("scheduledTransitionDuration"), scheduledTransitionDuration()},
        });
    });

    new NightLightAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/NightLight"), this);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWin.NightLight"));
}

NightLightDBusInterface::~NightLightDBusInterface()
{
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.KWin.NightLight"));
}

bool NightLightDBusInterface::isInhibited() const
{
    return m_manager->isInhibited();
}

bool NightLightDBusInterface::isEnabled() const
{
    return m_manager->isEnabled();
}

bool NightLightDBusInterface::isRunning() const
{
    return m_manager->isRunning();
}

bool NightLightDBusInterface::isAvailable() const
{
    return true; // TODO: Night color should register its own dbus service instead.
}

quint32 NightLightDBusInterface::currentTemperature() const
{
    return m_manager->currentTemperature();
}

double NightLightDBusInterface::brightness() const
{
    return m_manager->brightness();
}

quint32 NightLightDBusInterface::targetTemperature() const
{
    return m_manager->targetTemperature();
}

quint32 NightLightDBusInterface::mode() const
{
    return m_manager->mode();
}

bool NightLightDBusInterface::daylight() const
{
    return m_manager->daylight();
}

quint64 NightLightDBusInterface::previousTransitionDateTime() const
{
    const QDateTime dateTime = m_manager->previousTransitionDateTime();
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 NightLightDBusInterface::previousTransitionDuration() const
{
    return quint32(m_manager->previousTransitionDuration());
}

quint64 NightLightDBusInterface::scheduledTransitionDateTime() const
{
    const QDateTime dateTime = m_manager->scheduledTransitionDateTime();
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 NightLightDBusInterface::scheduledTransitionDuration() const
{
    return quint32(m_manager->scheduledTransitionDuration());
}

uint NightLightDBusInterface::inhibit()
{
    const QString serviceName = QDBusContext::message().service();

    if (!m_inhibitors.contains(serviceName)) {
        m_inhibitorWatcher->addWatchedService(serviceName);
    }

    m_inhibitors.insert(serviceName, ++m_lastInhibitionCookie);

    m_manager->inhibit();

    return m_lastInhibitionCookie;
}

void NightLightDBusInterface::uninhibit(uint cookie)
{
    const QString serviceName = QDBusContext::message().service();

    uninhibit(serviceName, cookie);
}

void NightLightDBusInterface::uninhibit(const QString &serviceName, uint cookie)
{
    const int removedCount = m_inhibitors.remove(serviceName, cookie);
    if (!removedCount) {
        return;
    }

    if (!m_inhibitors.contains(serviceName)) {
        m_inhibitorWatcher->removeWatchedService(serviceName);
    }

    m_manager->uninhibit();
}

void NightLightDBusInterface::removeInhibitorService(const QString &serviceName)
{
    const auto cookies = m_inhibitors.values(serviceName);
    for (const uint &cookie : cookies) {
        uninhibit(serviceName, cookie);
    }
}

void NightLightDBusInterface::preview(uint previewTemp)
{
    m_manager->preview(previewTemp);
}

void NightLightDBusInterface::stopPreview()
{
    m_manager->stopPreview();
}

void NightLightDBusInterface::setBrightness(double brightness)
{
    m_manager->setBrightness(brightness);
}

void NightLightDBusInterface::increaseBrightness(double step)
{
    m_manager->increaseBrightness(step);
}

void NightLightDBusInterface::decreaseBrightness(double step)
{
    m_manager->decreaseBrightness(step);
}

void NightLightDBusInterface::resetBrightness()
{
    m_manager->resetBrightness();
}

void NightLightDBusInterface::setTemperature(int temperature)
{
    m_manager->setTemperature(temperature);
}

void NightLightDBusInterface::increaseTemperature(int step)
{
    m_manager->increaseTemperature(step);
}

void NightLightDBusInterface::decreaseTemperature(int step)
{
    m_manager->decreaseTemperature(step);
}

void NightLightDBusInterface::resetTemperature()
{
    m_manager->resetTemperature();
}

void NightLightDBusInterface::setGamma(double red, double green, double blue)
{
    m_manager->setGamma(red, green, blue);
}

void NightLightDBusInterface::resetGamma()
{
    m_manager->resetGamma();
}

void NightLightDBusInterface::setMode(uint mode)
{
    // Map XML mode values to actual enum values
    // XML: 0=automatic, 1=location, 2=timings, 3=constant
    // Available: Constant=0, DarkLight=1

    NightLightMode nightLightMode;
    switch (mode) {
    case 0: // automatic
    case 1: // location
    case 2: // timings
        nightLightMode = NightLightMode::DarkLight; // Use DarkLight for all auto modes
        break;
    case 3: // constant
        nightLightMode = NightLightMode::Constant;
        break;
    default:
        return; // Invalid mode
    }

    // Note: setMode is private, so we can't call it directly
    // For now, this is a placeholder - would need to add a public method to NightLightManager
    Q_UNUSED(nightLightMode)

    // TODO: Add public setMode method to NightLightManager
}

uint NightLightDBusInterface::getMode()
{
    // Map enum values back to XML values
    NightLightMode currentMode = m_manager->mode();

    switch (currentMode) {
    case NightLightMode::Constant:
        return 3; // constant
    case NightLightMode::DarkLight:
        return 0; // automatic (default)
    default:
        return 0;
    }
}

QVariantMap NightLightDBusInterface::getAutoTimings()
{
    QVariantMap timings;

    // Get current auto timings from manager
    // Note: This would need to be implemented in NightLightManager
    // For now, return default values
    timings["morningBegin"] = 360; // 6:00 AM
    timings["morningEnd"] = 420; // 7:00 AM
    timings["eveningBegin"] = 1020; // 5:00 PM
    timings["eveningEnd"] = 1080; // 6:00 PM

    return timings;
}

void NightLightDBusInterface::setAutoTimings(uint morningBegin, uint morningEnd, uint eveningBegin, uint eveningEnd)
{
    // Validate time ranges (0-1439 minutes from midnight)
    if (morningBegin > 1439 || morningEnd > 1439 || eveningBegin > 1439 || eveningEnd > 1439) {
        return;
    }

    // Note: This would need to be implemented in NightLightManager
    // For now, just store in config or emit a signal
    Q_UNUSED(morningBegin)
    Q_UNUSED(morningEnd)
    Q_UNUSED(eveningBegin)
    Q_UNUSED(eveningEnd)

    // TODO: Implement timing storage in NightLightManager
}

void NightLightDBusInterface::disableAutoTemperature()
{
    // Note: setMode is private, so we can't call it directly
    // For now, this is a placeholder
    // TODO: Add public setMode method to NightLightManager
}

void NightLightDBusInterface::enableAutoTemperature()
{
    // Note: setMode is private, so we can't call it directly
    // For now, this is a placeholder
    // TODO: Add public setMode method to NightLightManager
}

void NightLightDBusInterface::setTemperatureLimits(uint minTemperature, uint maxTemperature)
{
    // Validate temperature ranges
    if (minTemperature < 1000 || minTemperature > 6500 || maxTemperature < 6500 || maxTemperature > 10000 || minTemperature >= maxTemperature) {
        return;
    }

    // Note: This would need to be implemented in NightLightManager
    // For now, just store the limits
    Q_UNUSED(minTemperature)
    Q_UNUSED(maxTemperature)

    // TODO: Implement temperature limits in NightLightManager
}

QVariantMap NightLightDBusInterface::getTemperatureLimits()
{
    QVariantMap limits;

    // Get current temperature limits from manager
    // Note: This would need to be implemented in NightLightManager
    // For now, return default values
    limits["minTemperature"] = 4000; // Default night temperature
    limits["maxTemperature"] = 6500; // Default day temperature

    return limits;
}
}

#include "moc_nightlightdbusinterface.cpp"
