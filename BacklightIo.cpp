#include "BacklightIo.h"
#include "BrightnessPolicy.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <unistd.h>

namespace {
bool readNumber(const QString &path, int &value) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    bool ok = false;
    value = QString::fromLatin1(f.readAll()).trimmed().toInt(&ok);
    return ok;
}
}
BacklightIo::BacklightIo(QString root, QObject *parent) : QObject(parent), m_root(std::move(root)) {}
BacklightState BacklightIo::inspect(const QString &root) {
    QList<BacklightState> devices, dsi;
    for (const QString &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        BacklightState s;
        s.device = QDir(root).filePath(name);
        if (!readNumber(s.device + "/max_brightness", s.maximum) || s.maximum <= 0 ||
            !readNumber(s.device + "/brightness", s.raw) || s.raw < 0 || s.raw > s.maximum) continue;
        if (!readNumber(s.device + "/actual_brightness", s.actual)) s.actual = s.raw;
        if (s.actual < 0 || s.actual > s.maximum) s.actual = s.raw;
        s.writable = ::access(QFile::encodeName(s.device + "/brightness").constData(), W_OK) == 0;
        if (!s.writable) s.error = "Backlight permission denied. The user needs restricted write access to this device's brightness attribute.";
        devices.append(s);
        const QString driver = QFileInfo(QFileInfo(s.device).canonicalFilePath() + "/../../driver").canonicalFilePath();
        if (driver.endsWith("/rpi_touchscreen_attiny") || name == "rpi_backlight") dsi.append(s);
    }
    if (dsi.size() == 1) return dsi.first();
    if (devices.size() == 1) return devices.first();
    BacklightState s;
    s.error = devices.isEmpty() ? "Hardware brightness unavailable: no readable backlight device."
                               : "Multiple backlights found; the DSI backlight could not be identified safely.";
    return s;
}
void BacklightIo::start() {
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &BacklightIo::poll);
    timer->start(2000);
    poll();
}
void BacklightIo::poll() { emit stateRead(inspect(m_root)); }
void BacklightIo::setPercent(int percent, int minimum) {
    BacklightState s = inspect(m_root);
    if (s.valid() && s.writable) {
        const int raw = BrightnessPolicy::toRaw(percent, s.maximum, minimum);
        if (raw != s.raw) {
            QFile f(s.device + "/brightness");
            const QByteArray value = QByteArray::number(raw) + '\n';
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(value) != value.size() || !f.flush()) {
                s.writable = false;
                s.error = "Cannot write backlight brightness: " + f.errorString();
                emit stateRead(s);
                return;
            }
        }
        s = inspect(m_root);
    }
    emit stateRead(s);
}
