#include <QGuiApplication>
#include <QCursor>
#include <QQmlApplicationEngine>
#include "BrightnessController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DeskBuddy");
    QCoreApplication::setApplicationName("DeskBuddy");
    QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

    BrightnessController brightness;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"brightnessController", QVariant::fromValue(&brightness)}});
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("deskpi", "Main");

    return app.exec();
}
