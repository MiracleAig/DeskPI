#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
namespace AgentConfig {
inline QJsonObject load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.read(8192)).object();
}
inline bool save(const QString &path, const QJsonObject &o) {
    const auto directory = QFileInfo(path).absolutePath();
    const bool existed = QFileInfo::exists(directory);
    if (!QDir().mkpath(directory))
        return false;
    if (!existed)
        QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto b = QJsonDocument(o).toJson();
    return f.write(b) == b.size() && f.commit();
}
} // namespace AgentConfig
