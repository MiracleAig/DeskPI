#pragma once
#include "BrightnessController.h"
#include <QJsonObject>

// The shared application model. HTTP calls enter via queued GUI-thread invocation;
// QML and HTTP use the same BrightnessController and QSettings file.
class SettingsManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool clock24 READ clock24 NOTIFY changed)
    Q_PROPERTY(bool showSeconds READ showSeconds NOTIFY changed)
    Q_PROPERTY(bool showDate READ showDate NOTIFY changed)
    Q_PROPERTY(QString dateFormat READ dateFormat NOTIFY changed)
    Q_PROPERTY(QString theme READ theme NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
public:
    explicit SettingsManager(BrightnessController *brightness, QString settingsFile = {}, QObject *parent = nullptr);
    bool clock24() const { return m_clock.value("format24").toBool(); }
    bool showSeconds() const { return m_clock.value("seconds").toBool(); }
    bool showDate() const { return m_clock.value("date").toBool(); }
    QString dateFormat() const { return m_clock.value("dateFormat").toString(); }
    QString theme() const { return m_appearance.value("theme").toString(); }
    QString accent() const { return m_appearance.value("accent").toString(); }
    QJsonObject snapshot() const;
    QJsonObject exportConfig() const;
    // Validation is side-effect free. Import validates all sections before applying any.
    static QString validate(const QString &section, const QJsonObject &patch, const QJsonObject &current);
    QString apply(const QString &section, const QJsonObject &patch);
    QString importConfig(const QJsonObject &config);
    QString settingsPath() const { return m_file; }
    QString flush(); // Call from a non-GUI HTTP thread after apply.
signals:
    void changed();
private:
    BrightnessController *m_brightness;
    QString m_file;
    QJsonObject m_clock, m_appearance;
    void persistExtras();
};
