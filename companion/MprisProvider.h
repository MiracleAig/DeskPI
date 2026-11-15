#pragma once
#include "MediaProvider.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QTimer>
class MprisSession : public QObject {
    Q_OBJECT
  public:
    QString service;
    QVariantMap properties;
    QElapsedTimer positionAge;
    bool refreshing = false, again = false;
    explicit MprisSession(QString name, QObject *parent);
    void refresh();
  signals:
    void updated();
  public slots:
    void propertiesChanged(QString interface, QVariantMap changed, QStringList invalidated);
    void seeked(qlonglong position);
};
class MprisProvider : public MediaProvider {
    Q_OBJECT
  public:
    explicit MprisProvider(QObject *parent = nullptr);
    void start() override;
    void prefer(const QString &) override;
    void execute(const QJsonObject &) override;
    static QJsonObject normalized(const QString &service, const QVariantMap &properties);
  private slots:
    void onOwnerChanged(QString name, QString oldOwner, QString newOwner);

  private:
    QHash<QString, MprisSession *> m_sessions;
    QDBusServiceWatcher m_watcher;
    QString m_selected, m_preferred;
    QTimer m_sync;
    QSet<QString> m_inflight, m_initialPending;
    bool m_initializing = true, m_enumerated = false;
    void add(const QString &name);
    void select();
};
