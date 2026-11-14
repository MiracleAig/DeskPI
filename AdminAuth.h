#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <functional>

// Used only by the serialized HTTP worker (and by the local setup command).
class AdminAuth {
public:
    struct Session { QByteArray csrf; qint64 created = 0, touched = 0; };
    explicit AdminAuth(QString file, std::function<qint64()> clock = {});
    static QByteArray randomToken();
    static bool setPassword(const QString &file, const QByteArray &password, QString *error);
    bool configured() const { return m_hash.size() == 32 && m_salt.size() == 32; }
    bool rateAllowed(const QString &peer);
    QByteArray login(const QByteArray &password);
    Session *session(const QByteArray &token);
    void logout(const QByteArray &token);
    bool csrfValid(const Session &session, const QByteArray &csrf) const;
private:
    static QByteArray digest(const QByteArray &password, const QByteArray &salt);
    static QByteArray hash(const QByteArray &data);
    static bool equal(const QByteArray &a, const QByteArray &b);
    std::function<qint64()> m_now;
    QByteArray m_hash, m_salt;
    QHash<QByteArray, Session> m_sessions;
    struct Attempts { qint64 since = 0; int count = 0; };
    QHash<QString, Attempts> m_attempts;
    Attempts m_global;
};
