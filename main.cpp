#include "AdminServer.h"
#include "BrightnessController.h"
#include "SettingsManager.h"
#include "media/MediaReceiver.h"
#include "media/RoundedArtwork.h"
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <unistd.h>

namespace {
void identity() {
    QCoreApplication::setOrganizationName("DeskPI");
    QCoreApplication::setApplicationName("DeskPI");
    QCoreApplication::setApplicationVersion("0.2.0");
}
} // namespace
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--admin-set-password" || arg == "--admin-configure") {
            QCoreApplication app(argc, argv);
            identity();
            return AdminServer::localCommand(app.arguments());
        }
    }
    int result;
    {
        QGuiApplication app(argc, argv);
        identity();
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
        BrightnessController brightness;
        SettingsManager settings(&brightness);
        auto adminOptions = AdminServer::options();
        std::unique_ptr<MediaReceiver> media;
        if (!app.arguments().contains("--media-disabled")) {
            media = std::make_unique<MediaReceiver>(&settings, AdminServer::directory() +
                                                                   "/media-devices.json");
            const auto port = qEnvironmentVariableIntValue("DESKBUDDY_MEDIA_PORT");
            if (!media->start(adminOptions.address, port ? port : 8091, adminOptions.certificate,
                              adminOptions.key))
                qWarning() << "Media listener requires TLS configuration.";
        }
        AdminServer admin(&settings, adminOptions, media.get());
        if (!app.arguments().contains("--admin-disabled")) {
            QString error;
            if (!admin.start(&error))
                qWarning().noquote() << "Administration:" << error;
        }
        QQmlApplicationEngine engine;
        qmlRegisterType<RoundedArtwork>("DeskBuddy.Media", 1, 0, "RoundedArtwork");
        engine.setInitialProperties({{"mediaController", QVariant::fromValue(media.get())},
                                     {"brightnessController", QVariant::fromValue(&brightness)},
                                     {"settingsManager", QVariant::fromValue(&settings)}});
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
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
