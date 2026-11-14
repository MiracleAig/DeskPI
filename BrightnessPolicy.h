#pragma once
#include <QDateTime>
#include <QSettings>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace BrightnessPolicy {
inline int safePercent(int value, int minimum = 5) {
    return std::clamp(value, std::clamp(minimum, 1, 100), 100);
}
inline int toRaw(int percent, int maximum, int minimum = 5) {
    if (maximum <= 0) return -1;
    return std::clamp(int(std::ceil(double(maximum) * safePercent(percent, minimum) / 100.0)), 1, maximum);
}
inline int toPercent(int raw, int maximum) {
    if (maximum <= 0) return 0;
    return std::clamp(int(std::lround(double(raw) * 100.0 / maximum)), 0, 100);
}
struct Settings {
    int manual = 80, day = 80, night = 20, dayStart = 420, nightStart = 1320, minimum = 5;
    bool automatic = false;
};
inline int number(QSettings &s, const QString &key, int fallback, int low, int high) {
    bool ok = false;
    const int value = s.value(key, fallback).toInt(&ok);
    return ok ? std::clamp(value, low, high) : fallback;
}
inline Settings load(QSettings &s) {
    Settings v;
    s.beginGroup("brightness");
    v.minimum = number(s, "minimum", 5, 1, 100);
    v.manual = number(s, "manual", 80, v.minimum, 100);
    v.day = number(s, "day", 80, v.minimum, 100);
    v.night = number(s, "night", 20, v.minimum, 100);
    v.dayStart = number(s, "dayStart", 420, 0, 1439);
    v.nightStart = number(s, "nightStart", 1320, 0, 1439);
    if (v.dayStart == v.nightStart) { v.dayStart = 420; v.nightStart = 1320; }
    const QString automatic = s.value("automatic", false).toString().toLower();
    v.automatic = automatic == "true" || automatic == "1";
    s.endGroup();
    // Fallbacks must also honor a custom minimum.
    v.manual = safePercent(v.manual, v.minimum);
    v.day = safePercent(v.day, v.minimum);
    v.night = safePercent(v.night, v.minimum);
    return v;
}
inline void save(QSettings &s, const Settings &v) {
    s.beginGroup("brightness");
    s.setValue("manual", v.manual); s.setValue("automatic", v.automatic);
    s.setValue("day", v.day); s.setValue("night", v.night);
    s.setValue("dayStart", v.dayStart); s.setValue("nightStart", v.nightStart);
    s.setValue("minimum", v.minimum);
    s.endGroup(); s.sync();
}
inline bool isDay(const QTime &time, const Settings &s) {
    const int minute = time.hour() * 60 + time.minute();
    return s.dayStart < s.nightStart ? minute >= s.dayStart && minute < s.nightStart
                                  : minute >= s.dayStart || minute < s.nightStart;
}
inline QDateTime nextTransition(const QDateTime &now, const Settings &s) {
    QDateTime result;
    for (int minute : {s.dayStart, s.nightStart}) {
        QDateTime candidate(now.date(), QTime(minute / 60, minute % 60), now.timeZone());
        if (candidate <= now) candidate = QDateTime(now.date().addDays(1), candidate.time(), now.timeZone());
        if (!result.isValid() || candidate < result) result = candidate;
    }
    return result;
}
struct Override {
    QDateTime until;
    void begin(const QDateTime &now, const Settings &s) { until = nextTransition(now, s); }
    void clear() { until = {}; }
    bool active(const QDateTime &now) const { return until.isValid() && now < until; }
};
inline int target(const Settings &s, const Override &override, const QDateTime &now) {
    return !s.automatic || override.active(now) ? s.manual : (isDay(now.time(), s) ? s.day : s.night);
}
}
