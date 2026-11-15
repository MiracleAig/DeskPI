#pragma once
#include "SettingsManager.h"
#include <QElapsedTimer>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
class MediaReceiver : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(bool screenRequested READ screenRequested NOTIFY changed)
    Q_PROPERTY(QString artwork READ artwork NOTIFY changed)
    Q_PROPERTY(QVariantMap options READ options NOTIFY changed)
  public:
    explicit MediaReceiver(SettingsManager *settings, QString store, QObject *parent = nullptr);
    ~MediaReceiver() override;
    bool start(const QString &address, quint16 port, const QString &cert, const QString &key);
    quint16 port() const { return m_server.serverPort(); }
    QVariantMap current() const;
    QVariantMap options() const;
    bool screenRequested() const { return m_show; }
    QString artwork() const;
    QImage image() const;
    QJsonObject devices() const;
    QJsonObject administer(const QString &action, const QJsonObject &body);
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void reopen();
    Q_INVOKABLE qint64 position() const;
    Q_INVOKABLE void command(const QString &action, qint64 positionMs = 0);
  signals:
    void changed();

  private:
    struct Host {
        QJsonObject device, state;
        QWebSocket *socket = nullptr;
        QElapsedTimer received;
        QByteArray art;
        QString artId;
        qint64 seq = 0;
    };
    struct Peer {
        QString host, request;
        QElapsedTimer alive, connected, rateWindow;
        int messages = 0;
    };
    SettingsManager *m_settings;
    QString m_store, m_selected, m_code, m_error;
    QWebSocketServer m_server;
    QHash<QString, Host> m_hosts;
    QHash<QWebSocket *, Peer> m_peers;
    QHash<QString, QJsonObject> m_pending;
    QHash<QString, QWebSocket *> m_commands;
    QElapsedTimer m_pairTime, m_attemptTime;
    int m_attempts = 0;
    QTimer m_heartbeat, m_return;
    bool m_show = false, m_dismissed = false, m_wasPlaying = false;
    QString m_lastTrack;
    void accept();
    void text(QWebSocket *s, const QString &message);
    void binary(QWebSocket *s, const QByteArray &data);
    void select();
    void send(QWebSocket *s, QJsonObject o);
    bool persist();
};
