#include "MprisProvider.h"
#include "../media/Protocol.h"
#include <QCryptographicHash>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
namespace {
const QString path = "/org/mpris/MediaPlayer2", player = "org.mpris.MediaPlayer2.Player",
              props = "org.freedesktop.DBus.Properties";
QVariantMap map(const QVariant &v) {
    if (v.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(v);
    return v.toMap();
}
} // namespace
MprisSession::MprisSession(QString name, QObject *parent) : QObject(parent), service(name) {
    auto bus = QDBusConnection::sessionBus();
    bus.connect(service, path, props, "PropertiesChanged", this,
                SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    bus.connect(service, path, player, "Seeked", this, SLOT(seeked(qlonglong)));
}
void MprisSession::refresh() {
    if (refreshing) {
        again = true;
        return;
    }
    refreshing = true;
    auto msg = QDBusMessage::createMethodCall(service, path, props, "GetAll");
    msg << player;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 2000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        QDBusPendingReply<QVariantMap> r = *w;
        refreshing = false;
        if (!r.isError()) {
            properties = r.value();
            positionAge.restart();
        }
        emit updated();
        w->deleteLater();
        if (again) {
            again = false;
            refresh();
        }
    });
}
void MprisSession::propertiesChanged(QString interface, QVariantMap, QStringList) {
    if (interface == player)
        refresh();
}
void MprisSession::seeked(qlonglong position) {
    properties["Position"] = position;
    positionAge.restart();
    emit updated();
}
MprisProvider::MprisProvider(QObject *parent) : MediaProvider(parent), m_watcher(this) {
    m_watcher.setConnection(QDBusConnection::sessionBus());
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &s, const QString &, const QString &owner) {
                if (m_sessions.contains(s)) {
                    m_sessions.take(s)->deleteLater();
                    m_initialPending.remove(s);
                    m_watcher.removeWatchedService(s);
                }
                if (!owner.isEmpty())
                    add(s);
                if (m_enumerated && m_initialPending.isEmpty())
                    m_initializing = false;
                select();
            });
    // NameOwnerChanged discovers new players, including services that did not exist at startup.
    QDBusConnection::sessionBus().connect("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                          "org.freedesktop.DBus", "NameOwnerChanged", this,
                                          SLOT(onOwnerChanged(QString, QString, QString)));
    m_sync.setInterval(30000);
    connect(&m_sync, &QTimer::timeout, this, [this] {
        if (m_sessions.contains(m_selected) &&
            m_sessions[m_selected]->properties.value("PlaybackStatus") == "Playing")
            m_sessions[m_selected]->refresh();
    });
}
void MprisProvider::start() {
    if (!QDBusConnection::sessionBus().isConnected() ||
        !QDBusConnection::sessionBus().interface()) {
        qWarning("No user session D-Bus connection; start from your desktop login session.");
        m_enumerated = true;
        m_initializing = false;
        select();
        return;
    }
    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().interface()->asyncCall("ListNames"), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        QDBusPendingReply<QStringList> r = *w;
        if (!r.isError())
            for (const auto &s : r.value())
                add(s);
        m_enumerated = true;
        if (m_initialPending.isEmpty())
            m_initializing = false;
        w->deleteLater();
        select();
    });
    m_sync.start();
}
void MprisProvider::add(const QString &s) {
    if (!s.startsWith("org.mpris.MediaPlayer2.") || m_sessions.contains(s) ||
        m_sessions.size() >= 32)
        return;
    auto *session = new MprisSession(s, this);
    m_sessions[s] = session;
    m_watcher.addWatchedService(s);
    if (m_initializing)
        m_initialPending.insert(s);
    connect(session, &MprisSession::updated, this, [this, s] {
        m_initialPending.remove(s);
        if (m_enumerated && m_initialPending.isEmpty())
            m_initializing = false;
        select();
    });
    session->refresh();
}
void MprisProvider::prefer(const QString &s) {
    m_preferred = s;
    select();
}
QJsonObject MprisProvider::normalized(const QString &service, const QVariantMap &p) {
    auto meta = map(p.value("Metadata"));
    auto state = MediaProtocol::message("media_state");
    state["session_id"] = service;
    auto track = meta.value("mpris:trackid");
    state["track_id"] = track.canConvert<QDBusObjectPath>() ? track.value<QDBusObjectPath>().path()
                                                            : track.toString();
    for (auto k : {"title", "album"})
        state[k] = meta.value(QString("xesam:") + k).toString().left(1024);
    state["artist"] = meta.value("xesam:artist").toStringList().join(", ").left(1024);
    state["duration_ms"] =
        double(qBound<qint64>(0, meta.value("mpris:length").toLongLong() / 1000, 1000000000000LL));
    state["position_ms"] =
        double(qBound<qint64>(0, p.value("Position").toLongLong() / 1000, 1000000000000LL));
    double rate = p.value("Rate", 1.).toDouble();
    state["playback_rate"] = std::isfinite(rate) && std::abs(rate) <= 16 ? rate : 1.;
    auto status = p.value("PlaybackStatus").toString().toLower();
    state["status"] =
        QStringList{"playing", "paused", "stopped"}.contains(status) ? status : "stopped";
    bool control = p.value("CanControl").toBool();
    state["can_control"] = control;
    for (auto pair : {qMakePair("can_play", "CanPlay"), qMakePair("can_pause", "CanPause"),
                      qMakePair("can_next", "CanGoNext"),
                      qMakePair("can_previous", "CanGoPrevious"), qMakePair("can_seek", "CanSeek")})
        state[pair.first] = control && p.value(pair.second).toBool();
    if (state.value("track_id").toString().size() > 1024) {
        state["track_id"] =
            "sha256:" + QString::fromLatin1(
                            QCryptographicHash::hash(state.value("track_id").toString().toUtf8(),
                                                     QCryptographicHash::Sha256)
                                .toHex());
        state["can_seek"] = false;
    }
    state["artwork_id"] = "";
    state["palette"] =
        QJsonObject{{"primary", "#25354b"}, {"secondary", "#422c42"}, {"accent", "#bd8c68"}};
    // MPRIS has no reliable music/video classifier; unknown is explicit.
    state["media_kind"] = "unknown";
    return state;
}
void MprisProvider::select() {
    if (m_initializing)
        return;
    QString chosen;
    if (m_sessions.contains(m_preferred))
        chosen = m_preferred;
    if (chosen.isEmpty() && m_sessions.contains(m_selected) &&
        m_sessions[m_selected]->properties.value("PlaybackStatus") == "Playing")
        chosen = m_selected;
    auto names = m_sessions.keys();
    names.sort();
    if (chosen.isEmpty())
        for (const auto &s : names)
            if (m_sessions[s]->properties.value("PlaybackStatus") == "Playing") {
                chosen = s;
                break;
            }
    if (chosen.isEmpty() && m_sessions.contains(m_selected))
        chosen = m_selected;
    if (chosen.isEmpty() && !names.isEmpty())
        chosen = names.first();
    m_selected = chosen;
    auto p = chosen.isEmpty() ? QVariantMap() : m_sessions[chosen]->properties;
    auto state = normalized(chosen, p);
    if (!chosen.isEmpty() && m_sessions[chosen]->positionAge.isValid())
        state["position_ms"] =
            double(MediaProtocol::position(state, m_sessions[chosen]->positionAge.elapsed()));
    emit stateChanged(state, map(p.value("Metadata")).value("mpris:artUrl").toString());
}
void MprisProvider::execute(const QJsonObject &o) {
    const QString id = o.value("command_id").toString(), service = o.value("session_id").toString(),
                  action = o.value("action").toString();
    if (m_inflight.contains(id))
        return;
    if (!m_sessions.contains(service) || service != m_selected) {
        emit commandDone(id, false, "Session unavailable");
        return;
    }
    auto state = normalized(service, m_sessions[service]->properties);
    QString capability = action == "play"    ? "can_play"
                         : action == "pause" ? "can_pause"
                         : action == "toggle"
                             ? (state.value("status") == "playing" ? "can_pause" : "can_play")
                         : action == "next"     ? "can_next"
                         : action == "previous" ? "can_previous"
                         : action == "seek"     ? "can_seek"
                                                : QString();
    if (capability.isEmpty() || !state.value(capability).toBool() ||
        o.value("track_id") != state.value("track_id")) {
        emit commandDone(id, false, "Unsupported control or stale track");
        return;
    }
    const QHash<QString, QString> methods = {{"play", "Play"},         {"pause", "Pause"},
                                             {"toggle", "PlayPause"},  {"next", "Next"},
                                             {"previous", "Previous"}, {"seek", "SetPosition"}};
    auto msg = QDBusMessage::createMethodCall(service, path, player, methods.value(action));
    if (action == "seek") {
        if (!MediaProtocol::integer(o.value("position_ms"), 0, 1e12) ||
            !state.value("track_id").toString().startsWith('/')) {
            emit commandDone(id, false, "Invalid seek");
            return;
        }
        msg << QVariant::fromValue(QDBusObjectPath(state.value("track_id").toString()))
            << QVariant::fromValue(qlonglong(o.value("position_ms").toDouble() * 1000));
    }
    m_inflight.insert(id);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 2000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, id, service] {
        QDBusPendingReply<> r = *w;
        m_inflight.remove(id);
        emit commandDone(id, !r.isError(), r.isError() ? r.error().message() : QString());
        w->deleteLater();
        if (m_sessions.contains(service))
            m_sessions[service]->refresh();
    });
}

void MprisProvider::onOwnerChanged(QString name, QString, QString owner) {
    if (!owner.isEmpty())
        add(name);
}
