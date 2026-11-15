#include "companion/ArtworkProcessor.h"
#include "companion/MprisProvider.h"
#include "media/Protocol.h"
#include "media/RoundedArtwork.h"
#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
class MediaUiTests : public QObject {
    Q_OBJECT
  private slots:
    void emptyStateHasDisabledControls() {
        QTemporaryDir dir;
        BrightnessController brightness(nullptr, dir.path(), dir.filePath("settings.ini"));
        SettingsManager settings(&brightness, dir.filePath("settings.ini"));
        MediaReceiver empty(&settings, dir.filePath("devices.json"));
        qmlRegisterType<RoundedArtwork>("DeskBuddy.Media", 1, 0, "RoundedArtwork");
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(DESKPI_SOURCE_DIR "/MediaScreen.qml")));
        std::unique_ptr<QObject> screen(
            component.createWithInitialProperties({{"controller", QVariant::fromValue(&empty)}}));
        QVERIFY2(screen != nullptr, qPrintable(component.errorString()));
        auto *progress = screen->findChild<QQuickItem *>("mediaProgress");
        QVERIFY(progress);
        QVERIFY(!progress->isEnabled());
        QCOMPARE(warnings.count(), 0);
    }
    void renderAndDismiss() {
        QTemporaryDir dir;
        QString token(64, 'a');
        auto id = QString("ui-test");
        QFile store(dir.filePath("devices.json"));
        QVERIFY(store.open(QIODevice::WriteOnly));
        store.write(
            QJsonDocument(QJsonArray{QJsonObject{
                              {"id", id},
                              {"name", "Studio Linux"},
                              {"os", "linux"},
                              {"credential_hash",
                               QString::fromLatin1(QCryptographicHash::hash(
                                                       token.toLatin1(), QCryptographicHash::Sha256)
                                                       .toHex())}}})
                .toJson());
        store.close();
        QCOMPARE(QProcess::execute("openssl",
                                   {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout",
                                    dir.filePath("key"), "-out", dir.filePath("cert"), "-days", "1",
                                    "-subj", "/CN=localhost"}),
                 0);
        BrightnessController brightness(nullptr, dir.path(), dir.filePath("settings.ini"));
        SettingsManager settings(&brightness, dir.filePath("settings.ini"));
        MediaReceiver media(&settings, store.fileName());
        QVERIFY(media.start("127.0.0.1", 0, dir.filePath("cert"), dir.filePath("key")));
        QWebSocket socket;
        connect(&socket, &QWebSocket::sslErrors, &socket,
                [&socket](const QList<QSslError> &e) { socket.ignoreSslErrors(e); });
        socket.open(QUrl("wss://127.0.0.1:" + QString::number(media.port())));
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        auto auth = MediaProtocol::message("authenticate");
        auth["device_id"] = id;
        auth["credential"] = token;
        socket.sendTextMessage(MediaProtocol::encode(auth));
        QTRY_VERIFY(media.current().value("connected").toBool());
        QImage album(400, 400, QImage::Format_RGB32);
        QPainter painter(&album);
        QLinearGradient gradient(0, 0, 400, 400);
        gradient.setColorAt(0, QColor("#d6a276"));
        gradient.setColorAt(1, QColor("#344c67"));
        painter.fillRect(album.rect(), gradient);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#a55355"));
        painter.drawEllipse(QRect(80, 50, 300, 300));
        painter.setBrush(QColor("#343e54"));
        painter.drawEllipse(QRect(30, 160, 240, 240));
        painter.setPen(Qt::white);
        QFont font;
        font.setPixelSize(28);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(28, 20, 344, 80), Qt::AlignLeft, "DESKBUDDY\nSESSIONS");
        painter.end();
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        album.save(&buffer, "JPEG", 82);
        auto artId = QCryptographicHash::hash(jpeg, QCryptographicHash::Sha256).toHex();
        auto state = MprisProvider::normalized(
            "org.mpris.MediaPlayer2.spotify",
            {{"PlaybackStatus", "Playing"},
             {"CanControl", true},
             {"CanPause", true},
             {"CanPlay", true},
             {"CanGoNext", true},
             {"CanGoPrevious", true},
             {"CanSeek", true},
             {"Position", qlonglong(42000000)},
             {"Metadata", QVariantMap{{"xesam:title", "A Place in the Sun"},
                                      {"xesam:artist", QStringList{"The Evening Sessions"}},
                                      {"xesam:album", "After Hours"},
                                      {"mpris:trackid", "/test/track"},
                                      {"mpris:length", qlonglong(215000000)}}}});
        state["seq"] = 1;
        state["artwork_id"] = QString::fromLatin1(artId);
        state["palette"] = ArtworkProcessor::palette(album);
        socket.sendTextMessage(MediaProtocol::encode(state));
        QTRY_VERIFY(media.screenRequested());
        socket.sendBinaryMessage(artId + jpeg);
        QTRY_VERIFY(!media.artwork().isEmpty());
        qmlRegisterType<RoundedArtwork>("DeskBuddy.Media", 1, 0, "RoundedArtwork");
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"brightnessController", QVariant::fromValue(&brightness)},
                                     {"settingsManager", QVariant::fromValue(&settings)},
                                     {"mediaController", QVariant::fromValue(&media)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DESKPI_SOURCE_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTest::qWait(450);
        auto frame = window->grabWindow();
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.size(), QSize(800, 480));
        auto *title = window->findChild<QQuickItem *>("mediaTitle");
        QVERIFY(title);
        QCOMPARE(title->property("textFormat").toInt(), 0);
        auto *loader = window->findChild<QObject *>("mediaLoader");
        QVERIFY(loader);
        QVERIFY(loader->property("status").toInt() == 1);
        std::function<QQuickItem *(QQuickItem *)> findToggle =
            [&](QQuickItem *root) -> QQuickItem * {
            if (root->objectName() == "media-toggle")
                return root;
            for (auto *child : root->childItems())
                if (auto *item = findToggle(child))
                    return item;
            return nullptr;
        };
        auto *toggle = findToggle(window->contentItem());
        QVERIFY(toggle);
        QSignalSpy messages(&socket, &QWebSocket::textMessageReceived);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        QTRY_VERIFY(!messages.isEmpty());
        auto command = MediaProtocol::parse(messages.last()[0].toString());
        QCOMPARE(command.value("action").toString(), QString("toggle"));
        auto ack = MediaProtocol::message("command_ack");
        ack["command_id"] = command.value("command_id");
        ack["ok"] = true;
        socket.sendTextMessage(MediaProtocol::encode(ack));
        window->setProperty("settingsOpen", true);
        QTRY_VERIFY(!loader->property("active").toBool());
        QVERIFY(window->property("settingsOpen").toBool());
        window->setProperty("settingsOpen", false);
        QTRY_VERIFY(loader->property("status").toInt() == 1);
        auto output = qEnvironmentVariable("DESKBUDDY_UI_CAPTURE");
        if (!output.isEmpty())
            QVERIFY(frame.save(output));
        QElapsedTimer changeLatency;
        changeLatency.start();
        state["seq"] = 2;
        state["title"] = "Measured track change";
        socket.sendTextMessage(MediaProtocol::encode(state));
        auto *activeMedia = qvariant_cast<QQuickItem *>(loader->property("item"));
        QVERIFY(activeMedia);
        auto *currentTitle = activeMedia->findChild<QQuickItem *>("mediaTitle");
        QVERIFY(currentTitle);
        QTRY_COMPARE(currentTitle->property("text").toString(), QString("Measured track change"));
        window->grabWindow();
        qInfo() << "Metadata change to software frame ms (50ms polling resolution)"
                << changeLatency.elapsed();
        QElapsedTimer render;
        render.start();
        for (int i = 0; i < 30; ++i) {
            window->update();
            QCoreApplication::processEvents();
            window->grabWindow();
        }
        qInfo() << "30 software frame grabs ms" << render.elapsed();
        if (qEnvironmentVariableIsSet("DESKBUDDY_STRESS")) {
            auto rss = [] {
                QFile f("/proc/self/smaps_rollup");
                if (!f.open(QIODevice::ReadOnly))
                    return qint64(0);
                auto lines = f.readAll().split('\n');
                for (const auto &l : lines)
                    if (l.startsWith("Rss:"))
                        return l.simplified().split(' ')[1].toLongLong();
                return qint64(0);
            };
            auto before = rss();
            QElapsedTimer changes;
            changes.start();
            for (int i = 0; i < 120; ++i) {
                QImage next(400, 400, QImage::Format_RGB32);
                next.fill(QColor::fromHsv(i * 3 % 360, 150, 140));
                QByteArray data;
                QBuffer out(&data);
                out.open(QIODevice::WriteOnly);
                next.save(&out, "JPEG", 82);
                auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
                state["seq"] = i + 10;
                state["track_id"] = QString("/test/track%1").arg(i);
                state["title"] = QString("Track %1").arg(i);
                state["artwork_id"] = QString::fromLatin1(hash);
                socket.sendTextMessage(MediaProtocol::encode(state));
                socket.sendBinaryMessage(hash + data);
                QTest::qWait(60);
            }
            QTRY_COMPARE(media.current().value("title").toString(), QString("Track 119"));
            auto after = rss();
            qInfo() << "120 artwork changes ms" << changes.elapsed() << "RSS before/after KiB"
                    << before << after << "delta" << after - before;
            QVERIFY(after - before < 8 * 1024);
        }
        media.dismiss();
        QTRY_VERIFY(!media.screenRequested());
    }
};
QTEST_MAIN(MediaUiTests)
#include "media_ui_tests.moc"
