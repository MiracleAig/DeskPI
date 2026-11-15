#pragma once
#include <QFutureWatcher>
#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
struct Artwork {
    QByteArray jpeg;
    QString id;
    QJsonObject palette;
};
Q_DECLARE_METATYPE(Artwork)
class ArtworkProcessor : public QObject {
    Q_OBJECT
  public:
    explicit ArtworkProcessor(QObject *parent = nullptr);
    void request(const QString &uri);
    static Artwork process(QByteArray bytes);
    static QJsonObject palette(const QImage &image);
  signals:
    void ready(QString uri, Artwork artwork);

  private:
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QFutureWatcher<Artwork> m_worker;
    QString m_wanted, m_processing, m_cachedUri;
    Artwork m_cached;
    QByteArray m_download;
    void begin();
    void work(QByteArray data);
};
