#include <QGuiApplication>
#include <QCursor>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("deskpi", "Main");

    return app.exec();
}
