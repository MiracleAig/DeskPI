#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QFile>
#include "BrightnessController.h"

class BrightnessUiTests : public QObject {
    Q_OBJECT
private slots:
    void touchscreenSlider() {
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.path() + "/panel"));
        for (const QString &name : {QString("max_brightness"), QString("brightness")}) {
            QFile file(temp.path() + "/panel/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name == "max_brightness" ? "100" : "80");
        }
        BrightnessController controller(nullptr, temp.path(), temp.filePath("settings.ini"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"brightnessController", QVariant::fromValue(&controller)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DESKBUDDY_SOURCE_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window); QTRY_VERIFY(controller.available());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *open = window->findChild<QQuickItem *>("openSettings");
        QVERIFY(open);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          open->mapToScene(QPointF(open->width()/2, open->height()/2)).toPoint());
        QTRY_VERIFY(window->property("settingsOpen").toBool());
        auto *slider = window->findChild<QQuickItem *>("manualBrightnessSlider");
        QVERIFY(slider); QVERIFY(slider->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          slider->mapToScene(QPointF(slider->width()/2, slider->height()/2)).toPoint());
        QTRY_VERIFY(controller.manual() >= 45 && controller.manual() <= 55);
        QTRY_VERIFY(BacklightIo::inspect(temp.path()).raw >= 45 && BacklightIo::inspect(temp.path()).raw <= 55);
        controller.setManual(0); QTRY_COMPARE(BacklightIo::inspect(temp.path()).raw, 5);
        window->setProperty("settingsOpen", false);
        QVERIFY(!window->property("settingsOpen").toBool());
    }
};
QTEST_MAIN(BrightnessUiTests)
#include "brightness_ui_tests.moc"
