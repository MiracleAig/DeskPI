#include "MediaReceiver.h"
#include "AdminAuth.h"
#include "AdminServer.h"
#include "Protocol.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QUuid>
#include <openssl/crypto.h>
namespace {
QByteArray digest(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
bool equal(const QByteArray &a, const QByteArray &b) {
    return a.size() == b.size() &&
           CRYPTO_memcmp(a.constData(), b.constData(), size_t(a.size())) == 0;
}
} // namespace
MediaReceiver::MediaReceiver(SettingsManager *s, QString store, QObject *parent)
    : QObject(parent), m_settings(s), m_store(store),
      m_server("DeskBuddy Media v1", QWebSocketServer::SecureMode, this) {
    QFile f(store);
    if (f.open(QIODevice::ReadOnly)) {
        auto a = QJsonDocument::fromJson(f.read(32768)).array();
        for (auto v : a) {
            auto d = v.toObject();
            if (m_hosts.size() < MediaProtocol::HostLimit &&
                MediaProtocol::hash(d.value("credential_hash").toString())) {
                Host h;
                h.device = d;
                m_hosts.insert(d.value("id").toString(), h);
            }
        }
    }
    connect(&m_server, &QWebSocketServer::newConnection, this, &MediaReceiver::accept);
    m_server.setMaxPendingConnections(8);
    m_server.setHandshakeTimeout(5000);
    m_return.setSingleShot(true);
    connect(&m_return, &QTimer::timeout, this, [this] {
        m_show = false;
        emit changed();
    });
    connect(s, &SettingsManager::changed, this, [this] {
        if (!options().value("enabled").toBool()) {
            for (auto *p : m_peers.keys())
                p->close();
            m_show = false;
        }
        select();
        for (auto &h : m_hosts)
            if (h.socket) {
                auto o = MediaProtocol::message("preferences");
                o["preferred_player"] = options().value("preferredPlayer").toString();
                o["max_artwork_bytes"] = options().value("artworkLimitKiB").toInt() * 1024;
                send(h.socket, o);
            }
    });
    m_heartbeat.setInterval(15000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        for (auto *p : m_peers.keys()) {
            const auto &peer = m_peers[p];
            if (peer.alive.elapsed() > 45000 ||
                (peer.host.isEmpty() && peer.connected.elapsed() > 120000))
                p->close();
            else
                p->ping();
        }
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            bool found = false;
            for (const auto &p : m_peers)
                if (p.request == it.key())
                    found = true;
            if (!found)
                it = m_pending.erase(it);
            else
                ++it;
        }
    });
    m_heartbeat.start();
}
MediaReceiver::~MediaReceiver() {
    m_server.close();
    for (auto *s : m_peers.keys()) {
        s->disconnect(this);
        s->abort();
        delete s;
    }
    m_peers.clear();
}
bool MediaReceiver::start(const QString &address, quint16 port, const QString &cert,
                          const QString &key) {
    QFile c(cert), k(key);
    if (!c.open(QIODevice::ReadOnly) || !k.open(QIODevice::ReadOnly)) {
        m_error = "Media requires the administrator TLS certificate and key.";
        return false;
    }
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    ssl.setLocalCertificate(QSslCertificate(c.readAll()));
    ssl.setPrivateKey(QSslKey(k.readAll(), QSsl::Rsa));
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    if (ssl.localCertificate().isNull() || ssl.privateKey().isNull()) {
        m_error = "Invalid media TLS identity (RSA PEM required).";
        return false;
    }
    m_server.setSslConfiguration(ssl);
    if (address != "lan" && !AdminServer::privateAddress(address)) {
        m_error = "Private listener address required.";
        return false;
    }
    bool ok =
        m_server.listen(address == "lan" ? QHostAddress::AnyIPv4 : QHostAddress(address), port);
    if (!ok)
        m_error = m_server.errorString();
    return ok;
}
QVariantMap MediaReceiver::options() const {
    return m_settings->snapshot().value("media").toObject().toVariantMap();
}
void MediaReceiver::send(QWebSocket *s, QJsonObject o) {
    if (s && s->bytesToWrite() < 65536)
        s->sendTextMessage(QString::fromUtf8(MediaProtocol::encode(o)));
}
void MediaReceiver::accept() {
    auto *s = m_server.nextPendingConnection();
    if (!s)
        return;
    if (m_peers.size() >= 12 || !AdminServer::privateAddress(s->peerAddress().toString()) ||
        !options().value("enabled").toBool() || !s->origin().isEmpty()) {
        s->close();
        s->deleteLater();
        return;
    }
    s->setMaxAllowedIncomingFrameSize(MediaProtocol::ArtworkLimit + 64);
    s->setMaxAllowedIncomingMessageSize(MediaProtocol::ArtworkLimit + 64);
    s->setReadBufferSize(MediaProtocol::ArtworkLimit + 1024);
    Peer peer;
    peer.alive.start();
    peer.connected.start();
    peer.rateWindow.start();
    m_peers.insert(s, peer);
    connect(s, &QWebSocket::pong, this, [this, s] {
        if (m_peers.contains(s))
            m_peers[s].alive.restart();
    });
    connect(s, &QWebSocket::textMessageReceived, this, [this, s](const QString &t) { text(s, t); });
    connect(s, &QWebSocket::binaryMessageReceived, this,
            [this, s](const QByteArray &b) { binary(s, b); });
    QTimer::singleShot(5000, s, [this, s] {
        if (m_peers.contains(s) && m_peers[s].host.isEmpty() && m_peers[s].request.isEmpty())
            s->close();
    });
    connect(s, &QWebSocket::disconnected, this, [this, s] {
        auto p = m_peers.take(s);
        m_pending.remove(p.request);
        if (m_hosts.contains(p.host) && m_hosts[p.host].socket == s) {
            auto &host = m_hosts[p.host];
            if (host.received.isValid())
                host.state["position_ms"] =
                    double(MediaProtocol::position(host.state, host.received.elapsed()));
            host.socket = nullptr;
            select();
        }
        for (auto it = m_commands.begin(); it != m_commands.end();)
            if (it.value() == s)
                it = m_commands.erase(it);
            else
                ++it;
        s->deleteLater();
    });
}
void MediaReceiver::text(QWebSocket *s, const QString &t) {
    auto &rate = m_peers[s];
    if (rate.rateWindow.elapsed() > 1000) {
        rate.rateWindow.restart();
        rate.messages = 0;
    }
    if (++rate.messages > 64) {
        s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Message rate exceeded");
        return;
    }
    auto o = MediaProtocol::parse(t);
    if (o.isEmpty()) {
        s->close(QWebSocketProtocol::CloseCodeProtocolError, "Invalid v1 message");
        return;
    }
    auto &p = m_peers[s];
    auto type = o.value("type").toString();
    if (p.host.isEmpty()) {
        if (!p.request.isEmpty()) {
            s->close();
            return;
        }
        if (!m_attemptTime.isValid() || m_attemptTime.elapsed() > 60000) {
            m_attemptTime.start();
            m_attempts = 0;
        }
        if (++m_attempts > 12) {
            s->close();
            return;
        }
        if (type == "authenticate") {
            QString id = o.value("device_id").toString();
            auto token = o.value("credential").toString().toLatin1();
            if (!m_hosts.contains(id) || token.size() != 64 ||
                !equal(digest(token).toHex(),
                       m_hosts[id].device.value("credential_hash").toString().toLatin1())) {
                s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Authentication failed");
                return;
            }
            auto &h = m_hosts[id];
            if (h.socket) {
                s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Device already connected");
                return;
            }
            p.host = id;
            h.socket = s;
            h.seq = 0;
            h.state = {};
            auto ready = MediaProtocol::message("ready");
            ready["preferred_player"] = options().value("preferredPlayer").toString();
            ready["max_artwork_bytes"] = options().value("artworkLimitKiB").toInt() * 1024;
            send(s, ready);
            select();
            return;
        }
        if (type == "pair" && m_pairTime.isValid() && m_pairTime.elapsed() < 120000 &&
            !m_code.isEmpty() && equal(o.value("code").toString().toLatin1(), m_code.toLatin1()) &&
            m_pending.size() < 4 && m_hosts.size() < MediaProtocol::HostLimit) {
            QString name = o.value("name").toString(), os = o.value("os").toString();
            if (name.isEmpty() || name.size() > 80 || os.size() > 32) {
                s->close();
                return;
            }
            p.request = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_pending[p.request] = {{"id", p.request}, {"name", name}, {"os", os}};
            send(s, MediaProtocol::message("approval_pending"));
            emit changed();
            return;
        }
        s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Pairing denied");
        return;
    }
    if (type == "media_state") {
        auto &h = m_hosts[p.host];
        if (!MediaProtocol::state(o)) {
            s->close(QWebSocketProtocol::CloseCodeProtocolError, "Invalid media state");
            return;
        }
        qint64 seq = qint64(o.value("seq").toDouble());
        if (seq <= h.seq)
            return;
        h.seq = seq;
        h.state = o;
        h.received.restart();
        QString art = o.value("artwork_id").toString();
        if (art != h.artId) {
            h.art.clear();
            h.artId.clear();
            if (!art.isEmpty()) {
                auto r = MediaProtocol::message("artwork_request");
                r["artwork_id"] = art;
                send(s, r);
            }
        }
        select();
        return;
    }
    if (type == "command_ack") {
        QString id = o.value("command_id").toString();
        if (!m_commands.contains(id))
            return; // Acknowledgments may arrive after timeout.
        if (m_commands.value(id) != s) {
            s->close();
            return;
        }
        m_commands.remove(id);
        if (!o.value("ok").toBool())
            m_error = "Playback command was rejected by the host.";
        emit changed();
        return;
    }
    s->close(QWebSocketProtocol::CloseCodeProtocolError, "Unexpected message");
}
void MediaReceiver::binary(QWebSocket *s, const QByteArray &b) {
    auto &rate = m_peers[s];
    if (rate.rateWindow.elapsed() > 1000) {
        rate.rateWindow.restart();
        rate.messages = 0;
    }
    if (++rate.messages > 64) {
        s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Message rate exceeded");
        return;
    }

    const auto p = m_peers.value(s);
    if (p.host.isEmpty() || b.size() <= 64 ||
        b.size() > options().value("artworkLimitKiB").toInt() * 1024 + 64) {
        s->close();
        return;
    }
    auto &h = m_hosts[p.host];
    QString id = QString::fromLatin1(b.first(64));
    auto data = b.mid(64);
    if (!MediaProtocol::hash(id) || id != h.state.value("artwork_id").toString() ||
        digest(data).toHex() != id.toLatin1()) {
        s->close();
        return;
    }
    QBuffer buf(&data);
    buf.open(QIODevice::ReadOnly);
    QImageReader r(&buf, "jpeg");
    r.setDecideFormatFromContent(false);
    auto size = r.size();
    if (size.width() < 1 || size.height() < 1 || size.width() > 400 || size.height() > 400 ||
        r.read().isNull()) {
        s->close();
        return;
    }
    h.art = data;
    h.artId = id;
    emit changed();
}
bool MediaReceiver::persist() {
    QDir().mkpath(QFileInfo(m_store).absolutePath());
    QSaveFile f(m_store);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    QJsonArray a;
    for (const auto &h : m_hosts)
        a.append(h.device);
    auto b = QJsonDocument(a).toJson();
    return f.write(b) == b.size() && f.commit();
}
QJsonObject MediaReceiver::devices() const {
    QJsonArray a, p;
    for (auto it = m_hosts.begin(); it != m_hosts.end(); ++it) {
        auto d = it->device;
        d.remove("credential_hash");
        d["connected"] = it->socket != nullptr;
        d["session"] = it->state.value("session_id");
        a.append(d);
    }
    for (const auto &v : m_pending)
        p.append(v);
    return {{"devices", a},
            {"pending", p},
            {"selected", m_selected},
            {"error", m_error},
            {"listening", m_server.isListening()}};
}
QJsonObject MediaReceiver::administer(const QString &action, const QJsonObject &b) {
    if (action == "pair") {
        if (!m_server.isListening())
            return {{"error", "Configure TLS before pairing."}};
        m_code = QString::fromLatin1(AdminAuth::randomToken().first(12));
        m_pairTime.start();
        m_pending.clear();
        return {{"code", m_code},
                {"expires_seconds", 120},
                {"certificate_sha256", QString::fromLatin1(m_server.sslConfiguration()
                                                               .localCertificate()
                                                               .digest(QCryptographicHash::Sha256)
                                                               .toHex())}};
    }
    auto id = b.value("id").toString();
    if (action == "approve") {
        if (!m_pairTime.isValid() || m_pairTime.elapsed() >= 120000 || !m_pending.contains(id) ||
            m_hosts.size() >= MediaProtocol::HostLimit)
            return {{"error", "Request expired or unavailable."}};
        QWebSocket *socket = nullptr;
        for (auto *s : m_peers.keys())
            if (m_peers[s].request == id)
                socket = s;
        if (!socket)
            return {{"error", "Agent disconnected."}};
        auto token = AdminAuth::randomToken();
        Host h;
        h.device = m_pending[id];
        h.device["credential_hash"] = QString::fromLatin1(digest(token).toHex());
        m_hosts[id] = h;
        if (!persist()) {
            m_hosts.remove(id);
            return {{"error", "Cannot persist device."}};
        }
        auto response = MediaProtocol::message("paired");
        response["device_id"] = id;
        response["credential"] = QString::fromLatin1(token);
        send(socket, response);
        m_code.clear();
        m_pending.clear();
        socket->close();
        return devices();
    }
    if (action == "revoke" && m_hosts.contains(id)) {
        auto h = m_hosts.take(id);
        if (!persist()) {
            m_hosts[id] = h;
            return {{"error", "Cannot persist revocation."}};
        }
        if (h.socket)
            h.socket->close();
        select();
        return devices();
    }
    return {{"error", "Unknown device action."}};
}
void MediaReceiver::select() {
    auto previous = current();
    auto settings = options();
    QString chosen;
    auto preferred = settings.value("preferredHost").toString();
    if (m_hosts.contains(preferred) && m_hosts[preferred].socket)
        chosen = preferred;
    if (chosen.isEmpty() && m_hosts.contains(m_selected) && m_hosts[m_selected].socket &&
        m_hosts[m_selected].state.value("status") == "playing")
        chosen = m_selected;
    auto ids = m_hosts.keys();
    ids.sort();
    if (chosen.isEmpty())
        for (const auto &id : ids)
            if (m_hosts[id].socket && m_hosts[id].state.value("status") == "playing") {
                chosen = id;
                break;
            }
    if (chosen.isEmpty() && m_hosts.contains(m_selected))
        chosen = m_selected;
    if (chosen.isEmpty())
        for (const auto &id : ids)
            if (m_hosts[id].socket) {
                chosen = id;
                break;
            }
    m_selected = chosen;
    auto now = current();
    QString track = chosen + now.value("session_id").toString() + now.value("track_id").toString();
    bool playing = now.value("status") == "playing" && now.value("connected").toBool();
    if (track != m_lastTrack || (playing && !m_wasPlaying))
        m_dismissed = false;
    m_lastTrack = track;
    m_wasPlaying = playing;
    if (playing) {
        m_return.stop();
        if (settings.value("enabled").toBool() && settings.value("autoSwitch").toBool() &&
            !m_dismissed &&
            (settings.value("nonMusic").toBool() || now.value("media_kind") == "music"))
            m_show = true;
    } else if (m_show && !m_return.isActive())
        m_return.start(settings.value("returnSeconds").toInt() * 1000);
    emit changed();
}
QVariantMap MediaReceiver::current() const {
    if (!m_hosts.contains(m_selected))
        return {};
    const auto &h = m_hosts[m_selected];
    auto o = h.state;
    o["host_name"] = h.device.value("name");
    o["connected"] = h.socket != nullptr;
    o["device_id"] = m_selected;
    return o.toVariantMap();
}
QString MediaReceiver::artwork() const {
    return m_hosts.contains(m_selected) && !m_hosts[m_selected].art.isEmpty()
               ? "image://media/" + m_selected + "/" + m_hosts[m_selected].artId
               : QString();
}
QImage MediaReceiver::image() const {
    return m_hosts.contains(m_selected) ? QImage::fromData(m_hosts[m_selected].art, "JPEG")
                                        : QImage();
}
qint64 MediaReceiver::position() const {
    if (!m_hosts.contains(m_selected))
        return 0;
    const auto &h = m_hosts[m_selected];
    return MediaProtocol::position(h.state,
                                   h.received.isValid() && h.socket ? h.received.elapsed() : 0);
}
void MediaReceiver::dismiss() {
    m_show = false;
    m_dismissed = true;
    m_return.stop();
    emit changed();
}
void MediaReceiver::reopen() {
    if (!options().value("enabled").toBool())
        return;
    m_show = true;
    m_return.stop();
    emit changed();
    select();
}
void MediaReceiver::command(const QString &action, qint64 pos) {
    if (!m_hosts.contains(m_selected))
        return;
    auto &h = m_hosts[m_selected];
    QString cap = action == "play"    ? "can_play"
                  : action == "pause" ? "can_pause"
                  : action == "toggle"
                      ? (h.state.value("status") == "playing" ? "can_pause" : "can_play")
                  : action == "next"     ? "can_next"
                  : action == "previous" ? "can_previous"
                  : action == "seek"     ? "can_seek"
                                         : QString();
    if (!h.socket || cap.isEmpty() || !h.state.value("can_control").toBool() ||
        !h.state.value(cap).toBool() || m_commands.size() >= 16)
        return;
    auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto o = MediaProtocol::message("command");
    o["command_id"] = id;
    o["session_id"] = h.state.value("session_id");
    o["track_id"] = h.state.value("track_id");
    o["action"] = action;
    o["position_ms"] = double(qBound<qint64>(0, pos, 1000000000000LL));
    m_commands[id] = h.socket;
    send(h.socket, o);
    QTimer::singleShot(5000, this, [this, id] {
        if (m_commands.remove(id)) {
            m_error = "Playback command timed out.";
            emit changed();
        }
    });
}
