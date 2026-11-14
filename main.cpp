#include <QGuiApplication>
#include <QCursor>
#include <QQmlApplicationEngine>
#include <QDebug>
#include "BrightnessController.h"
#include "SettingsManager.h"
#include "AdminServer.h"
#include <unistd.h>

namespace {
void identity() {
    QCoreApplication::setOrganizationName("DeskPI");
    QCoreApplication::setApplicationName("DeskPI");
    QCoreApplication::setApplicationVersion("0.2.0");
}
}
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--admin-set-password" || arg == "--admin-configure") {
            QCoreApplication app(argc, argv); identity();
            return AdminServer::localCommand(app.arguments());
        }
    }
    int result;
    {
        QGuiApplication app(argc, argv); identity();
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
        BrightnessController brightness;
        SettingsManager settings(&brightness);
        AdminServer admin(&settings, AdminServer::options());
        if (!app.arguments().contains("--admin-disabled")) {
            QString error;
            if (!admin.start(&error)) qWarning().noquote() << "Administration:" << error;
        }
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"brightnessController", QVariant::fromValue(&brightness)},
                                     {"settingsManager", QVariant::fromValue(&settings)}});
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                         [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
        engine.loadFromModule("deskpi", "Main");
        result = app.exec();
        admin.stop();
    }
    // Same PID stays under Cage. No shell, sudo, systemd privilege, or arbitrary executable.
    if (result == 75) {
        ::execv(argv[0], argv);
        return 1;
    }
    return result;
}
