#pragma once
#include "SettingsManager.h"
#include <memory>
#include <QStringList>

class MediaReceiver;
class AdminServer {
public:
    struct Options {
        QString address = "127.0.0.1";
        int port = 8080;
        QStringList hosts;
        QString certificate, key, authFile;
    };
    explicit AdminServer(SettingsManager *settings, Options options, MediaReceiver *media = nullptr);
    ~AdminServer();
    bool start(QString *error);
    void stop();
    static QString directory();
    static Options options();
    static int localCommand(const QStringList &arguments);
    static bool privateAddress(const QString &address);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
