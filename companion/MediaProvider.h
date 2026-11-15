#pragma once
#include <QJsonObject>
#include <QObject>
class MediaProvider : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual void prefer(const QString &) = 0;
    virtual void execute(const QJsonObject &) = 0;
  signals:
    void stateChanged(QJsonObject state, QString artworkUri);
    void commandDone(QString id, bool ok, QString error);
};
