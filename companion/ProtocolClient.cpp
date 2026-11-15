#include "ProtocolClient.h"
#include "../media/Protocol.h"
#include "AgentConfig.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QUrl>
ProtocolClient::ProtocolClient(MediaProvider *provider, QJsonObject config, QString path,
                               QString code, QObject *parent)
    : QObject(parent), m_provider(provider), m_config(config), m_path(path), m_code(code) {
    m_publish.setSingleShot(true);
    m_publish.setInterval(40);
    connect(&m_publish, &QTimer::timeout, this, &ProtocolClient::flushState);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &ProtocolClient::start);
    m_socket.setMaxAllowedIncomingFrameSize(MediaProtocol::JsonLimit);
    m_socket.setMaxAllowedIncomingMessageSize(MediaProtocol::JsonLimit);
    connect(&m_socket, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        const auto cert = m_socket.sslConfiguration().peerCertificate();
        if (!cert.isNull() && cert.effectiveDate() <= QDateTime::currentDateTimeUtc() &&
            cert.expiryDate() > QDateTime::currentDateTimeUtc() &&
            cert.digest(QCryptographicHash::Sha256).toHex() ==
                m_config.value("pin").toString().toLatin1())
            m_socket.ignoreSslErrors(errors);
        else {
            m_pinValid = false;
            m_fatal = true;
            qCritical() << "Certificate pin mismatch. Verify the fingerprint on the Pi.";
            m_socket.abort();
        }
    });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        auto cert = m_socket.sslConfiguration().peerCertificate();
        m_pinValid = cert.effectiveDate() <= QDateTime::currentDateTimeUtc() &&
                     cert.expiryDate() > QDateTime::currentDateTimeUtc() &&
                     cert.digest(QCryptographicHash::Sha256).toHex() ==
                         m_config.value("pin").toString().toLatin1();
        if (!m_pinValid) {
            m_fatal = true;
            m_socket.abort();
            return;
        }
        m_alive.start();
        m_heartbeat.start();
        m_acks.clear();
        m_inflight.clear();
        m_seq = 0;
        QJsonObject hello = MediaProtocol::message(m_code.isEmpty() ? "authenticate" : "pair");
        if (m_code.isEmpty()) {
            hello["device_id"] = m_config.value("device_id");
            hello["credential"] = m_config.value("credential");
        } else {
            hello["code"] = m_code;
            hello["name"] = m_config.value("name");
            hello["os"] = "linux";
        }
        send(hello);
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_ready = false;
        m_heartbeat.stop();
        if (m_socket.closeCode() == QWebSocketProtocol::CloseCodePolicyViolated) {
            m_fatal = true;
            qCritical() << "Receiver rejected authentication; pair again or check duplicate agent.";
        }
        if (!m_fatal)
            m_retry.start(reconnectDelay(m_attempt++));
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_fatal && !m_retry.isActive())
            m_retry.start(reconnectDelay(m_attempt++));
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &ProtocolClient::receive);
    connect(&m_socket, &QWebSocket::pong, this, [this] { m_alive.restart(); });
    m_heartbeat.setInterval(15000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (m_alive.elapsed() > 45000)
            m_socket.abort();
        else
            m_socket.ping();
    });
    connect(provider, &MediaProvider::stateChanged, this, &ProtocolClient::publish);
    connect(provider, &MediaProvider::commandDone, this,
            [this](QString id, bool ok, QString error) {
                if (!m_inflight.remove(id))
                    return;
                auto o = MediaProtocol::message("command_ack");
                o["command_id"] = id;
                o["ok"] = ok;
                o["error"] = error.left(256);
                if (m_acks.size() >= 64)
                    m_acks.erase(m_acks.begin());
                m_acks[id] = o;
                if (m_ready)
                    send(o);
            });
    connect(&m_artwork, &ArtworkProcessor::ready, this, [this](QString uri, Artwork art) {
        if (uri != m_uri)
            return;
        m_art = art;
        sync();
    });
}
int ProtocolClient::reconnectDelay(int attempt) {
    int base = 1000 * (1 << qMin(attempt, 5));
    return base + int(QRandomGenerator::global()->bounded(quint32(base / 4 + 1)));
}
QString ProtocolClient::configurationError(const QJsonObject &config, bool pairing) {
    QUrl url(config.value("url").toString());
    if (url.scheme() != "wss" || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !MediaProtocol::hash(config.value("pin").toString()))
        return "Agent is not configured. Open Connected Devices on the Pi, start pairing, then run "
               "deskbuddy-agent --url wss://deskpi.local:8091 --pin "
               "<verified-certificate-fingerprint> with DESKBUDDY_PAIR_CODE set.";
    if (!pairing && (config.value("device_id").toString().isEmpty() ||
                     !MediaProtocol::hash(config.value("credential").toString())))
        return "This computer is not paired. Start pairing in the Pi's Connected Devices page and "
               "set DESKBUDDY_PAIR_CODE for the first connection.";
    return {};
}
void ProtocolClient::start() {
    if (m_fatal)
        return;
    const auto error = configurationError(m_config, !m_code.isEmpty());
    if (!error.isEmpty()) {
        qCritical().noquote() << error;
        m_fatal = true;
        QTimer::singleShot(0, QCoreApplication::instance(), [] { QCoreApplication::exit(2); });
        return;
    }
    QUrl u(m_config.value("url").toString());
    auto ssl = QSslConfiguration::defaultConfiguration();
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    m_socket.setSslConfiguration(ssl);
    m_socket.open(u);
    QTimer::singleShot(8000, this, [this] {
        if (m_socket.state() != QAbstractSocket::ConnectedState)
            m_socket.abort();
    });
}
void ProtocolClient::send(QJsonObject o) {
    if (m_pinValid)
        m_socket.sendTextMessage(QString::fromUtf8(MediaProtocol::encode(o)));
}
void ProtocolClient::publish(QJsonObject state, QString uri) {
    m_state = state;
    m_positionAge.restart();
    if (uri != m_uri) {
        m_uri = uri;
        m_art = {};
        m_artwork.request(uri);
    }
    sync();
}
ProtocolClient::~ProtocolClient() {
    m_socket.disconnect(this);
    m_socket.abort();
}
void ProtocolClient::sync() {
    if (!m_publish.isActive())
        m_publish.start();
}
void ProtocolClient::flushState() {
    if (!m_ready || m_state.isEmpty())
        return;
    if (m_socket.bytesToWrite() > 65536) {
        m_publish.start(250);
        return;
    }
    auto o = m_state;
    o["seq"] = double(++m_seq);
    o["position_ms"] = double(
        MediaProtocol::position(m_state, m_positionAge.isValid() ? m_positionAge.elapsed() : 0));
    o["artwork_id"] = m_art.jpeg.size() <= m_artLimit ? m_art.id : QString();
    if (!m_art.palette.isEmpty())
        o["palette"] = m_art.palette;
    send(o);
}
void ProtocolClient::receive(const QString &text) {
    auto o = MediaProtocol::parse(text);
    if (o.isEmpty()) {
        m_socket.close(QWebSocketProtocol::CloseCodeProtocolError);
        return;
    }
    m_alive.restart();
    auto type = o.value("type").toString();
    if (type == "paired" && !m_code.isEmpty()) {
        auto credential = o.value("credential").toString();
        if (!MediaProtocol::hash(credential) || o.value("device_id").toString().size() > 80) {
            m_socket.abort();
            return;
        }
        m_config["credential"] = credential;
        m_config["device_id"] = o.value("device_id");
        if (!AgentConfig::save(m_path, m_config)) {
            m_fatal = true;
            qCritical() << "Cannot securely save device credential.";
            m_socket.close();
            return;
        }
        m_code.clear();
        qInfo() << "Pairing saved. Reconnecting with device credential.";
        return;
    }
    if (type == "approval_pending") {
        qInfo() << "Approve this computer in Connected Devices within two minutes.";
        return;
    }
    if (type == "ready" || type == "preferences") {
        if (type == "ready" && AgentConfig::load(m_path) != m_config &&
            !AgentConfig::save(m_path, m_config))
            qWarning() << "Could not persist the authenticated companion configuration.";
        m_ready = true;
        m_attempt = 0;
        m_artLimit = qBound(16384, o.value("max_artwork_bytes").toInt(160 * 1024), 160 * 1024);
        m_provider->prefer(o.value("preferred_player").toString());
        sync();
        return;
    }
    if (type == "artwork_request" && m_ready) {
        if (o.value("artwork_id") == m_art.id && !m_art.id.isEmpty() &&
            m_socket.bytesToWrite() < MediaProtocol::ArtworkLimit)
            m_socket.sendBinaryMessage(m_art.id.toLatin1() + m_art.jpeg);
        return;
    }
    if (type == "command" && m_ready) {
        auto id = o.value("command_id").toString();
        if (id.isEmpty() || id.size() > 80) {
            m_socket.close();
            return;
        }
        if (m_acks.contains(id)) {
            send(m_acks[id]);
            return;
        }
        if (m_inflight.contains(id))
            return;
        if (m_inflight.size() >= 16) {
            m_socket.close();
            return;
        }
        m_inflight.insert(id);
        m_provider->execute(o);
        return;
    }
    m_socket.close(QWebSocketProtocol::CloseCodeProtocolError);
}
