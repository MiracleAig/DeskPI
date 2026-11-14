#pragma once
#include <QJsonObject>
#include <QString>
class SystemMetrics {
public:
    explicit SystemMetrics(QString proc = "/proc", QString sys = "/sys") : m_proc(std::move(proc)), m_sys(std::move(sys)) {}
    QJsonObject collect();
private:
    QString m_proc, m_sys;
    quint64 m_previousTotal = 0, m_previousIdle = 0;
};
