#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <unistd.h>
#include "BrightnessController.h"

using namespace BrightnessPolicy;
class BrightnessTests : public QObject {
    Q_OBJECT
    static void put(const QString &path, const QByteArray &text) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write(text), text.size());
    }
    static void device(const QString &root, int max = 255, int raw = 128) {
        QVERIFY(QDir().mkpath(root + "/panel"));
        put(root + "/panel/max_brightness", QByteArray::number(max));
        put(root + "/panel/brightness", QByteArray::number(raw));
    }
    static QDateTime at(const char *value) { return QDateTime::fromString(QString::fromLatin1(value), Qt::ISODate); }
private slots:
    void conversion() {
        QCOMPARE(toRaw(80, 255), 204); QCOMPARE(toRaw(20, 255), 51);
        QCOMPARE(toRaw(50, 1023), 512); QCOMPARE(toPercent(512, 1023), 50);
        QCOMPARE(toRaw(100, 7), 7); QCOMPARE(toRaw(50, 0), -1);
        QCOMPARE(toPercent(-50, 255), 0); QCOMPARE(toPercent(300, 255), 100);
    }
    void minimumProtection() {
        QCOMPARE(toRaw(0, 255), 13); QCOMPARE(toRaw(-40, 100), 5);
        QCOMPARE(toRaw(1000, 100), 100); QCOMPARE(toRaw(0, 3), 1);
        QCOMPARE(toRaw(0, 255, 10), 26); QCOMPARE(safePercent(0, 0), 1);
    }
    void rangeDetection() {
        QTemporaryDir dir; device(dir.path(), 4095, 2020);
        auto s = BacklightIo::inspect(dir.path());
        QVERIFY(s.valid()); QCOMPARE(s.maximum, 4095); QCOMPARE(s.raw, 2020);
        QCOMPARE(s.actual, 2020);
        put(dir.path() + "/panel/actual_brightness", "2010");
        QCOMPARE(BacklightIo::inspect(dir.path()).actual, 2010);
        put(dir.path() + "/panel/max_brightness", "bad");
        QVERIFY(!BacklightIo::inspect(dir.path()).valid());
    }
    void persistenceAndCorruption() {
        QTemporaryDir dir;
        const auto file = dir.filePath("settings.ini");
        Settings original; original.manual = 34; original.day = 88; original.night = 12;
        original.automatic = true; original.dayStart = 480; original.nightStart = 1260; original.minimum = 8;
        { QSettings s(file, QSettings::IniFormat); save(s, original); }
        QSettings s(file, QSettings::IniFormat); const auto restored = load(s);
        QCOMPARE(restored.manual, 34); QCOMPARE(restored.day, 88); QCOMPARE(restored.night, 12);
        QCOMPARE(restored.dayStart, 480); QCOMPARE(restored.nightStart, 1260);
        QVERIFY(restored.automatic); QCOMPARE(restored.minimum, 8);
        s.setValue("brightness/minimum", 10); s.setValue("brightness/manual", -90);
        s.setValue("brightness/day", "broken"); s.setValue("brightness/night", 900);
        s.setValue("brightness/automatic", "garbage");
        s.setValue("brightness/dayStart", 700); s.setValue("brightness/nightStart", 700);
        auto v = load(s); QCOMPARE(v.manual, 10); QCOMPARE(v.day, 80); QCOMPARE(v.night, 100);
        QVERIFY(!v.automatic); QCOMPARE(v.dayStart, 420); QCOMPARE(v.nightStart, 1320);
        QSettings empty(dir.filePath("empty.ini"), QSettings::IniFormat);
        QCOMPARE(load(empty).night, 20);
    }
    void scheduleBoundaries() {
        Settings s;
        QVERIFY(!isDay(QTime(6, 59, 59), s)); QVERIFY(isDay(QTime(7, 0), s));
        QVERIFY(isDay(QTime(21, 59, 59), s)); QVERIFY(!isDay(QTime(22, 0), s));
        QVERIFY(!isDay(QTime(0, 0), s));
        QCOMPARE(nextTransition(at("2026-11-14T23:00:00-06:00"), s).date(), QDate(2026, 11, 15));
        QCOMPARE(nextTransition(at("2026-11-14T22:00:00-06:00"), s).time(), QTime(7, 0));
        s.dayStart = 1320; s.nightStart = 420;
        QVERIFY(isDay(QTime(23, 59), s)); QVERIFY(isDay(QTime(6, 59), s));
        QVERIFY(!isDay(QTime(7, 0), s)); QVERIFY(isDay(QTime(22, 0), s));
    }
    void manualOverride() {
        Settings s; s.automatic = true; s.manual = 55;
        Override o;
        const auto now = at("2026-11-14T21:59:00-06:00");
        QCOMPARE(target(s, o, now), 80);
        o.begin(now, s); QVERIFY(o.active(now)); QCOMPARE(target(s, o, now), 55);
        const auto boundary = at("2026-11-14T22:00:00-06:00");
        QVERIFY(!o.active(boundary)); QCOMPARE(target(s, o, boundary), 20);
        o.begin(at("2026-11-14T23:00:00-06:00"), s);
        QCOMPARE(o.until.date(), QDate(2026, 11, 15));
        QCOMPARE(o.until.time(), QTime(7, 0)); o.clear(); QVERIFY(!o.active(now));
        s.automatic = false; QCOMPARE(target(s, o, boundary), 55);
    }
    void missingAmbiguousAndRemoved() {
        QTemporaryDir dir; QVERIFY(!BacklightIo::inspect(dir.path()).valid());
        BacklightIo io(dir.path()); QSignalSpy spy(&io, &BacklightIo::stateRead);
        io.setPercent(20, 5); QCOMPARE(spy.count(), 1);
        QVERIFY(!qvariant_cast<BacklightState>(spy.takeFirst().at(0)).valid());
        device(dir.path()); QVERIFY(BacklightIo::inspect(dir.path()).valid());
        QVERIFY(QDir().mkpath(dir.path() + "/other"));
        put(dir.path() + "/other/max_brightness", "100"); put(dir.path() + "/other/brightness", "50");
        QVERIFY(!BacklightIo::inspect(dir.path()).valid());
        QVERIFY(QDir(dir.path() + "/other").removeRecursively());
        QVERIFY(QDir(dir.path() + "/panel").removeRecursively());
        QVERIFY(!BacklightIo::inspect(dir.path()).valid());
    }
    void writesAndPermissions() {
        QTemporaryDir dir; device(dir.path(), 100, 50);
        const auto path = dir.path() + "/panel/brightness";
        BacklightIo io(dir.path());
        QFileSystemWatcher watcher({path}); QSignalSpy changes(&watcher, &QFileSystemWatcher::fileChanged);
        io.setPercent(50, 5); QTest::qWait(40); QCOMPARE(changes.count(), 0);
        io.setPercent(0, 5); QCOMPARE(BacklightIo::inspect(dir.path()).raw, 5);
        if (::geteuid() == 0) QSKIP("Permission denial requires an unprivileged test process");
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner));
        QVERIFY(!BacklightIo::inspect(dir.path()).writable);
        QSignalSpy states(&io, &BacklightIo::stateRead); io.setPercent(30, 5);
        QVERIFY(!qvariant_cast<BacklightState>(states.last().at(0)).error.isEmpty());
        QCOMPARE(BacklightIo::inspect(dir.path()).raw, 5);
    }
    void controllerLifecycle() {
        QTemporaryDir dir; device(dir.path(), 100, 70);
        const auto settings = dir.filePath("controller.ini");
        {
            BrightnessController controller(nullptr, dir.path(), settings);
            QTRY_VERIFY(controller.available()); QTRY_COMPARE(controller.manual(), 70);
            for (int i = 10; i <= 35; ++i) controller.setManual(i);
            QTRY_COMPARE(BacklightIo::inspect(dir.path()).raw, 35);
            controller.setAutomatic(true); controller.setManual(46);
            QVERIFY(controller.overrideActive());
            controller.resumeSchedule(); QVERIFY(!controller.overrideActive());
            controller.setAutomatic(false); controller.setManual(42);
            QTRY_COMPARE(BacklightIo::inspect(dir.path()).raw, 42);
            QVERIFY(QDir(dir.path() + "/panel").removeRecursively());
            QTRY_VERIFY_WITH_TIMEOUT(!controller.available(), 4000);
            device(dir.path(), 100, 50);
            QTRY_VERIFY_WITH_TIMEOUT(controller.available(), 4000);
            QTRY_COMPARE(BacklightIo::inspect(dir.path()).raw, 42);
        }
        QSettings saved(settings, QSettings::IniFormat); QCOMPARE(load(saved).manual, 42);
    }
};
QTEST_GUILESS_MAIN(BrightnessTests)
#include "brightness_tests.moc"
