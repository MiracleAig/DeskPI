#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
namespace MediaProtocol {
constexpr int JsonLimit = 16384, ArtworkLimit = 160 * 1024, HostLimit = 8;
inline QJsonObject message(const QString &type) { return {{"version", 1}, {"type", type}}; }
inline QByteArray encode(QJsonObject o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
inline bool integer(const QJsonValue &v, double low, double high) {
    return v.isDouble() && std::isfinite(v.toDouble()) &&
           std::floor(v.toDouble()) == v.toDouble() && v.toDouble() >= low && v.toDouble() <= high;
}
inline bool hash(const QString &s) {
    return QRegularExpression("^[a-f0-9]{64}$").match(s).hasMatch();
}
inline QJsonObject parse(const QString &s) {
    if (s.size() > JsonLimit || s.toUtf8().size() > JsonLimit)
        return {};
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(s.toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject())
        return {};
    auto o = d.object();
    if (o.value("version") != QJsonValue(1) || !o.value("type").isString())
        return {};
    return o;
}
inline bool state(const QJsonObject &o) {
    if (!integer(o.value("seq"), 1, 9007199254740991.) ||
        !integer(o.value("position_ms"), 0, 1e12) || !integer(o.value("duration_ms"), 0, 1e12))
        return false;
    if (!o.value("playback_rate").isDouble() ||
        !std::isfinite(o.value("playback_rate").toDouble()) ||
        std::abs(o.value("playback_rate").toDouble()) > 16)
        return false;
    if (!QStringList{"playing", "paused", "stopped"}.contains(o.value("status").toString()))
        return false;
    for (auto k : {"session_id", "track_id", "title", "artist", "album", "artwork_id"})
        if (!o.value(k).isString() || o.value(k).toString().size() > 1024)
            return false;
    for (auto k : {"can_control", "can_play", "can_pause", "can_next", "can_previous", "can_seek"})
        if (!o.value(k).isBool())
            return false;
    auto id = o.value("artwork_id").toString();
    if (!id.isEmpty() && !hash(id))
        return false;
    if (!o.value("palette").isObject())
        return false;
    for (auto k : {"primary", "secondary", "accent"})
        if (!QRegularExpression("^#[0-9A-Fa-f]{6}$")
                 .match(o.value("palette").toObject().value(k).toString())
                 .hasMatch())
            return false;
    return true;
}
inline qint64 position(const QJsonObject &s, qint64 elapsed) {
    double p = s.value("position_ms").toDouble();
    if (s.value("status") == "playing")
        p += qMax<qint64>(0, elapsed) * s.value("playback_rate").toDouble(1);
    auto duration = s.value("duration_ms").toDouble();
    return qint64(qBound(0., p, duration > 0 ? duration : 1e12));
}
} // namespace MediaProtocol
