#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

struct BacklightState {
    QString device, error;
    int maximum = 0, raw = 0, actual = 0;
    bool writable = false;
    bool valid() const { return maximum > 0 && !device.isEmpty(); }
};
Q_DECLARE_METATYPE(BacklightState)

// All file operations run on the controller's dedicated I/O thread.
class BacklightIo : public QObject {
    Q_OBJECT
public:
    explicit BacklightIo(QString root = "/sys/class/backlight", QObject *parent = nullptr);
    static BacklightState inspect(const QString &root);
public slots:
    void start();
    void poll();
    void setPercent(int percent, int minimum);
    QString saveSettings(const QVariantMap &values, const QString &file);
    QString flushSettings(const QString &file);
signals:
    void stateRead(BacklightState state);
private:
    QString m_root, m_persistenceError;
};
