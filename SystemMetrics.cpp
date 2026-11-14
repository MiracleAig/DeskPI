#include "SystemMetrics.h"
#include <QFile>
#include <QSysInfo>
#include <QHostInfo>
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QRegularExpression>
#include <sys/statvfs.h>
namespace {
QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.read(32768) : QByteArray(); }
}
QJsonObject SystemMetrics::collect() {
    QJsonObject result{{"hostname", QHostInfo::localHostName()}, {"application", "running"},
        {"version", QCoreApplication::applicationVersion()}, {"qtVersion", qVersion()},
        {"architecture", QSysInfo::currentCpuArchitecture()}, {"os", QSysInfo::prettyProductName()}};
    bool ok;
    const double uptime = QString::fromLatin1(read(m_proc + "/uptime")).section(' ', 0, 0).toDouble(&ok);
    result["uptimeSeconds"] = ok ? QJsonValue(uptime) : QJsonValue();
    auto first = read(m_proc + "/stat").split('\n').value(0).simplified().split(' ');
    if (first.size() >= 6 && first[0] == "cpu") {
        quint64 total = 0; for (int i = 1; i < std::min(int(first.size()), 9); ++i) total += first[i].toULongLong();
        const quint64 idle = first[4].toULongLong() + first[5].toULongLong();
        result["cpuPercent"] = m_previousTotal && total > m_previousTotal ? QJsonValue(100.0 * (1.0 - double(idle - m_previousIdle) / (total - m_previousTotal))) : QJsonValue();
        m_previousTotal = total; m_previousIdle = idle;
    } else result["cpuPercent"] = QJsonValue();
    QMap<QByteArray, double> memory;
    for (const auto &line : read(m_proc + "/meminfo").split('\n')) {
        const auto parts = line.simplified().split(' ');
        if (parts.size() >= 2) memory[parts[0]] = parts[1].toDouble() * 1024;
    }
    result["memoryTotal"] = memory.contains("MemTotal:") ? QJsonValue(memory["MemTotal:"]) : QJsonValue();
    result["memoryUsed"] = memory.contains("MemAvailable:") ? QJsonValue(memory["MemTotal:"] - memory["MemAvailable:"]) : QJsonValue();
    for (const auto &line : read(m_proc + "/self/status").split('\n')) if (line.startsWith("VmRSS:")) result["applicationRss"] = line.simplified().split(' ').value(1).toDouble() * 1024;
    const double temperature = QString::fromLatin1(read(m_sys + "/class/thermal/thermal_zone0/temp")).trimmed().toDouble(&ok);
    result["temperatureC"] = ok ? QJsonValue(temperature / 1000) : QJsonValue();
    struct statvfs disk{};
    if (::statvfs("/", &disk) == 0) {
        result["diskTotal"] = double(disk.f_blocks) * disk.f_frsize;
        result["diskUsed"] = double(disk.f_blocks - disk.f_bavail) * disk.f_frsize;
    }
    QJsonArray addresses;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto &entry : iface.addressEntries()) if (!entry.ip().isNull()) addresses.append(entry.ip().toString());
    }
    result["addresses"] = addresses; result["networkConnected"] = !addresses.isEmpty();
    return result;
}
