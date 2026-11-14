#include "AdminAuth.h"
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QDateTime>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

AdminAuth::AdminAuth(QString file, std::function<qint64()> clock) : m_now(std::move(clock)) {
    if (!m_now) m_now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    QSettings s(file, QSettings::IniFormat);
    if (s.value("iterations").toInt() != 600000) return;
    m_salt = QByteArray::fromHex(s.value("salt").toByteArray());
    m_hash = QByteArray::fromHex(s.value("hash").toByteArray());
}
QByteArray AdminAuth::randomToken() {
    QByteArray random(32, '\0');
    for (int i = 0; i < 32; i += 4) {
        quint32 value = QRandomGenerator::system()->generate();
        memcpy(random.data() + i, &value, 4);
    }
    return random.toHex();
}
QByteArray AdminAuth::hash(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }
QByteArray AdminAuth::digest(const QByteArray &password, const QByteArray &salt) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password, salt, 600000, 32);
}
bool AdminAuth::equal(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size()) return false;
    volatile unsigned char different = 0;
    for (qsizetype i = 0; i < a.size(); ++i) different = different | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    return different == 0;
}
bool AdminAuth::setPassword(const QString &file, const QByteArray &password, QString *error) {
    if (password.size() < 12) { *error = "Your password is too short. Please use at least 12 letters, numbers, or symbols."; return false; }
    if (password.size() > 128) { *error = "Your password is too long. Please try a shorter password."; return false; }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) { *error = out.errorString(); return false; }
    out.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto salt = QByteArray::fromHex(randomToken());
    const auto data = QByteArray("[General]\niterations=600000\nsalt=") + salt.toHex() + "\nhash=" + digest(password, salt).toHex() + "\n";
    if (out.write(data) != data.size() || !out.commit()) { *error = out.errorString(); return false; }
    return true;
}
bool AdminAuth::rateAllowed(const QString &peer) {
    const auto now = m_now();
    if (m_attempts.size() > 32) m_attempts.clear();
    auto &local = m_attempts[peer];
    for (auto *a : {&local, &m_global}) {
        if (now - a->since >= 60000 || now < a->since) { a->since = now; a->count = 0; }
    }
    if (local.count >= 5 || m_global.count >= 10) return false;
    ++local.count; ++m_global.count; return true;
}
QByteArray AdminAuth::login(const QByteArray &password) {
    if (!configured() || password.size() > 128 || !equal(digest(password, m_salt), m_hash)) return {};
    const auto now = m_now();
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (now - it->touched >= 1200000 || now - it->created >= 28800000) it = m_sessions.erase(it); else ++it;
    }
    if (m_sessions.size() >= 8) m_sessions.clear();
    const auto token = randomToken();
    m_sessions.insert(hash(token), {randomToken(), now, now});
    return token;
}
AdminAuth::Session *AdminAuth::session(const QByteArray &token) {
    if (token.size() != 64) return nullptr;
    auto it = m_sessions.find(hash(token));
    if (it == m_sessions.end()) return nullptr;
    const auto now = m_now();
    if (now < it->created || now - it->touched >= 1200000 || now - it->created >= 28800000) {
        m_sessions.erase(it); return nullptr;
    }
    it->touched = now; return &it.value();
}
void AdminAuth::logout(const QByteArray &token) { m_sessions.remove(hash(token)); }
bool AdminAuth::csrfValid(const Session &s, const QByteArray &csrf) const { return equal(s.csrf, csrf); }
