#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QFile>
#include <QRegularExpression>
#include "BrightnessController.h"
#include "SettingsManager.h"

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
        SettingsManager settings(&controller, temp.filePath("settings.ini"));
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"brightnessController", QVariant::fromValue(&controller)}, {"settingsManager", QVariant::fromValue(&settings)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DESKPI_SOURCE_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window); QTRY_VERIFY(controller.available());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *open = window->findChild<QQuickItem *>("openSettings");
        QVERIFY(open);
        QVERIFY(!open->isVisible());
        const QPoint holdPoint(400, 240);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, holdPoint);
        QVERIFY(!open->isVisible());
        // Moving away cancels a hold.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, holdPoint);
        QTest::mouseMove(window, QPoint(500, 300));
        QTest::qWait(2100);
        QVERIFY(!open->isVisible());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(500, 300));
        // A real touch must last two seconds, rather than Qt's default hold time.
        auto *touch = QTest::createTouchDevice();
        QTest::touchEvent(window, touch).press(0, holdPoint, window).commit();
        QTest::qWait(1700);
        QVERIFY(!open->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(open->isVisible(), 1000);
        QTest::touchEvent(window, touch).release(0, holdPoint, window).commit();
        QVERIFY(!window->property("settingsOpen").toBool());
        QVERIFY(open->x() > window->width() / 2);
        QVERIFY(open->y() < window->height() / 2);
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
        QVERIFY(!open->isVisible());
        QVERIFY(settings.apply("clock", {{"format24", true}, {"seconds", true}, {"date", true}, {"dateFormat", "yyyy-MM-dd"}}).isEmpty());
        auto *clock = window->findChild<QObject *>("clockText");
        auto *date = window->findChild<QQuickItem *>("clockDate");
        QVERIFY(clock); QVERIFY(date);
        QTRY_VERIFY(QRegularExpression("^[0-9]{2}:[0-9]{2}:[0-9]{2}$").match(clock->property("text").toString()).hasMatch());
        QVERIFY(date->isVisible());
        QVERIFY(settings.apply("appearance", {{"theme", "light"}, {"accent", "#123456"}}).isEmpty());
        QTRY_COMPARE(window->color(), QColor("#f4f6fa"));
    }
};
QTEST_MAIN(BrightnessUiTests)
#include "brightness_ui_tests.moc"
