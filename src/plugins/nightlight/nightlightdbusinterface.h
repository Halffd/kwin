/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2024 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>

namespace KWin
{

class NightLightManager;

class NightLightDBusInterface : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.NightLight")
    Q_PROPERTY(bool inhibited READ isInhibited)
    Q_PROPERTY(bool enabled READ isEnabled)
    Q_PROPERTY(bool running READ isRunning)
    Q_PROPERTY(bool available READ isAvailable)
    Q_PROPERTY(quint32 currentTemperature READ currentTemperature)
    Q_PROPERTY(double brightness READ brightness)
    Q_PROPERTY(quint32 targetTemperature READ targetTemperature)
    Q_PROPERTY(quint32 mode READ mode)
    Q_PROPERTY(bool daylight READ daylight)
    Q_PROPERTY(quint64 previousTransitionDateTime READ previousTransitionDateTime)
    Q_PROPERTY(quint32 previousTransitionDuration READ previousTransitionDuration)
    Q_PROPERTY(quint64 scheduledTransitionDateTime READ scheduledTransitionDateTime)
    Q_PROPERTY(quint32 scheduledTransitionDuration READ scheduledTransitionDuration)

public:
    explicit NightLightDBusInterface(NightLightManager *parent);
    ~NightLightDBusInterface() override;

    bool isInhibited() const;
    bool isEnabled() const;
    bool isRunning() const;
    bool isAvailable() const;
    quint32 currentTemperature() const;
    double brightness() const;
    quint32 targetTemperature() const;
    quint32 mode() const;
    bool daylight() const;
    quint64 previousTransitionDateTime() const;
    quint32 previousTransitionDuration() const;
    quint64 scheduledTransitionDateTime() const;
    quint32 scheduledTransitionDuration() const;

public Q_SLOTS:
    /**
     * @brief Temporarily blocks Night Light.
     * @since 5.18
     */
    uint inhibit();
    /**
     * @brief Cancels the previous call to inhibit().
     * @since 5.18
     */
    void uninhibit(uint cookie);
    /**
     * @brief Previews a given temperature for a short time (15s).
     * @since 5.25
     */
    void preview(uint temperature);
    /**
     * @brief Stops an ongoing preview.
     * @since 5.25
     */
    void stopPreview();
    /**
     * @brief Sets the brightness level (0.00001 to 1.0).
     */
    void setBrightness(double brightness);
    /**
     * @brief Increases brightness by specified step.
     */
    void increaseBrightness(double step = 0.1);
    /**
     * @brief Decreases brightness by specified step.
     */
    void decreaseBrightness(double step = 0.1);
    /**
     * @brief Resets brightness to default (1.0).
     */
    void resetBrightness();
    /**
     * @brief Sets color temperature.
     */
    void setTemperature(int temperature);
    /**
     * @brief Gets the current color temperature.
     */
    int getTemperature();
    /**
     * @brief Increases temperature by specified step.
     */
    void increaseTemperature(int step = 100);
    /**
     * @brief Decreases temperature by specified step.
     */
    void decreaseTemperature(int step = 100);
    /**
     * @brief Resets temperature to automatic target.
     */
    void resetTemperature();
    /**
     * @brief Sets RGB gamma values directly.
     */
    void setGamma(double red, double green, double blue);
    /**
     * @brief Resets gamma to default values.
     */
    void resetGamma();

    /**
     * @brief Sets the Night Light operation mode.
     * @param mode Operation mode (0=automatic, 1=location, 2=timings, 3=constant)
     */
    void setMode(uint mode);

    /**
     * @brief Gets the current Night Light operation mode.
     * @return Current mode (0=automatic, 1=location, 2=timings, 3=constant)
     */
    uint getMode();

    /**
     * @brief Gets the current auto temperature timings.
     * @return Map with morningBegin, morningEnd, eveningBegin, eveningEnd times (in minutes from midnight)
     */
    QVariantMap getAutoTimings();

    /**
     * @brief Sets the auto temperature timings.
     * @param morningBegin Morning begin time (minutes from midnight, 0-1439)
     * @param morningEnd Morning end time (minutes from midnight, 0-1439)
     * @param eveningBegin Evening begin time (minutes from midnight, 0-1439)
     * @param eveningEnd Evening end time (minutes from midnight, 0-1439)
     */
    void setAutoTimings(uint morningBegin, uint morningEnd, uint eveningBegin, uint eveningEnd);

    /**
     * @brief Disables automatic temperature adjustments.
     */
    void disableAutoTemperature();

    /**
     * @brief Enables automatic temperature adjustments.
     */
    void enableAutoTemperature();

    /**
     * @brief Sets temperature limits for automatic adjustments.
     * @param minTemperature Minimum temperature (1000-6500K)
     * @param maxTemperature Maximum temperature (6500-10000K)
     */
    void setTemperatureLimits(uint minTemperature, uint maxTemperature);

    /**
     * @brief Gets the current temperature limits.
     * @return Map with minTemperature and maxTemperature
     */
    QVariantMap getTemperatureLimits();

private Q_SLOTS:
    void removeInhibitorService(const QString &serviceName);

private:
    void uninhibit(const QString &serviceName, uint cookie);

    NightLightManager *m_manager;
    QDBusServiceWatcher *m_inhibitorWatcher;
    QMultiHash<QString, uint> m_inhibitors;
    uint m_lastInhibitionCookie = 0;
};

}
