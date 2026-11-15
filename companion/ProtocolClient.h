#pragma once
#include "ArtworkProcessor.h"
#include "MediaProvider.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QWebSocket>
class ProtocolClient : public QObject {
    Q_OBJECT
  public:
    ProtocolClient(MediaProvider *provider, QJsonObject config, QString path, QString code,
                   QObject *parent = nullptr);
    ~ProtocolClient() override;
    void start();
    void publish(QJsonObject state, QString uri);
    static int reconnectDelay(int attempt);
    static QString configurationError(const QJsonObject &config, bool pairing);

  private:
    MediaProvider *m_provider;
    QJsonObject m_config, m_state;
    QString m_path, m_code, m_uri;
    QWebSocket m_socket;
    ArtworkProcessor m_artwork;
    Artwork m_art;
    QTimer m_retry, m_heartbeat, m_publish;
    QElapsedTimer m_alive, m_positionAge;
    quint64 m_seq = 0;
    int m_attempt = 0, m_artLimit = 160 * 1024;
    bool m_ready = false, m_pinValid = false, m_fatal = false;
    QHash<QString, QJsonObject> m_acks;
    QSet<QString> m_inflight;
    void send(QJsonObject o);
    void sync();
    void flushState();
    void receive(const QString &text);
};
