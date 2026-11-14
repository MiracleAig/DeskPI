#pragma once
#include "BacklightIo.h"
#include "BrightnessPolicy.h"
#include <QThread>
#include <QTimer>
#include <memory>

class BrightnessController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString device READ device NOTIFY stateChanged)
    Q_PROPERTY(int maximum READ maximum NOTIFY stateChanged)
    Q_PROPERTY(int percentage READ percentage NOTIFY stateChanged)
    Q_PROPERTY(int manual READ manual NOTIFY settingsChanged)
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY settingsChanged)
    Q_PROPERTY(int day READ day WRITE setDay NOTIFY settingsChanged)
    Q_PROPERTY(int night READ night WRITE setNight NOTIFY settingsChanged)
    Q_PROPERTY(int dayStart READ dayStart WRITE setDayStart NOTIFY settingsChanged)
    Q_PROPERTY(int nightStart READ nightStart WRITE setNightStart NOTIFY settingsChanged)
    Q_PROPERTY(int minimum READ minimum WRITE setMinimum NOTIFY settingsChanged)
    Q_PROPERTY(bool overrideActive READ overrideActive NOTIFY settingsChanged)
    Q_PROPERTY(QString status READ status NOTIFY settingsChanged)
public:
    explicit BrightnessController(QObject *parent = nullptr, QString root = "/sys/class/backlight",
                                  QString settingsFile = QString());
    ~BrightnessController() override;
    bool available() const { return m_state.valid() && m_state.writable; }
    QString error() const { return m_state.error; }
    QString device() const { return m_state.device; }
    int maximum() const { return m_state.maximum; }
    int percentage() const { return BrightnessPolicy::toPercent(m_state.actual, m_state.maximum); }
    int manual() const { return m_settings.manual; }
    bool automatic() const { return m_settings.automatic; }
    int day() const { return m_settings.day; }
    int night() const { return m_settings.night; }
    int dayStart() const { return m_settings.dayStart; }
    int nightStart() const { return m_settings.nightStart; }
    int minimum() const { return m_settings.minimum; }
    bool overrideActive() const { return automatic() && m_override.active(QDateTime::currentDateTime()); }
    QString status() const;
    void setAutomatic(bool value);
    void setDay(int value);
    void setNight(int value);
    void setDayStart(int value);
    void setNightStart(int value);
    void setMinimum(int value);
    Q_INVOKABLE void setManual(int value);
    Q_INVOKABLE void resumeSchedule();
signals:
    void stateChanged();
    void settingsChanged();
    void writeRequested(int percent, int minimum);
private:
    void persist();
    void reconfigure();
    void request(int value);
    void evaluate();
    BacklightState m_state;
    BrightnessPolicy::Settings m_settings;
    BrightnessPolicy::Override m_override;
    std::unique_ptr<QSettings> m_store;
    QThread m_thread;
    QTimer m_writeTimer, m_clockTimer, m_saveTimer;
    bool m_initialized = false, m_hadManual = false;
    int m_target = -1, m_period = -1;
};
