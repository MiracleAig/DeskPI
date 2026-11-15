#include "companion/AgentConfig.h"
#include "companion/ArtworkProcessor.h"
#include "companion/MprisProvider.h"
#include "companion/ProtocolClient.h"
#include "media/MediaReceiver.h"
#include "media/Protocol.h"
#include <QBuffer>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QProcess>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>
#include <future>
class FakePlayer : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(bool CanControl READ yes)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanGoNext READ yes)
    Q_PROPERTY(bool CanGoPrevious READ yes)
    Q_PROPERTY(bool CanSeek READ yes)
  public:
    QString playback = "Playing";
    int calls = 0;
    qlonglong pos = 10000000;
    QString status() const { return playback; }
    bool yes() const { return true; }
    double rate() const { return 1.; }
    qlonglong position() const { return pos; }
    QVariantMap metadata() const {
        return {{"xesam:title", "Fake song"},
                {"xesam:artist", QStringList{"Fake artist"}},
                {"mpris:length", qlonglong(200000000)},
                {"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/test/track"))}};
    }
    void changed() {
        auto msg = QDBusMessage::createSignal(
            "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged");
        msg << QString("org.mpris.MediaPlayer2.Player") << QVariantMap{{"PlaybackStatus", playback}}
            << QStringList();
        QDBusConnection::sessionBus().send(msg);
    }
  public slots:
    void Pause() {
        playback = "Paused";
        ++calls;
        changed();
    }
    void Play() {
        playback = "Playing";
        ++calls;
        changed();
    }
    void PlayPause() {
        playback = playback == "Playing" ? "Paused" : "Playing";
        ++calls;
        changed();
    }
    void Next() { ++calls; }
    void Previous() { ++calls; }
    void SetPosition(QDBusObjectPath, qlonglong p) {
        pos = p;
        ++calls;
        emit Seeked(pos);
    }
  signals:
    void Seeked(qlonglong position);
};
class StubProvider : public MediaProvider {
    Q_OBJECT
  public:
    QJsonObject state = MprisProvider::normalized(
        "stub", {{"PlaybackStatus", "Playing"},
                 {"CanControl", true},
                 {"CanPause", true},
                 {"CanPlay", true},
                 {"Metadata", QVariantMap{{"mpris:trackid", "/test/track"}}}});
    int commands = 0;
    QString artworkUri;
    void start() override { emit stateChanged(state, artworkUri); }
    void prefer(const QString &) override { start(); }
    void execute(const QJsonObject &o) override {
        ++commands;
        emit commandDone(o.value("command_id").toString(), true, {});
    }
};
class MediaTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    std::unique_ptr<BrightnessController> brightness;
    std::unique_ptr<SettingsManager> settings;
    std::unique_ptr<MediaReceiver> receiver;
    QString url, pin, device, credential;
    QJsonObject state() {
        auto s = MprisProvider::normalized(
            "test", {{"PlaybackStatus", "Playing"},
                     {"CanControl", true},
                     {"CanPause", true},
                     {"CanPlay", true},
                     {"CanSeek", true},
                     {"Metadata", QVariantMap{{"mpris:trackid", "/test/track"},
                                              {"mpris:length", qlonglong(200000000)}}}});
        s["seq"] = 1;
        return s;
    }
    void connectSocket(QWebSocket &socket) {
        connect(&socket, &QWebSocket::sslErrors, &socket,
                [&socket](const QList<QSslError> &e) { socket.ignoreSslErrors(e); });
        socket.open(QUrl(url));
    }
    void authenticate(QWebSocket &socket) {
        auto o = MediaProtocol::message("authenticate");
        o["device_id"] = device;
        o["credential"] = credential;
        socket.sendTextMessage(MediaProtocol::encode(o));
    }
  private slots:
    void initTestCase() {
        QVERIFY(temp.isValid());
        QCOMPARE(QProcess::execute("openssl",
                                   {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout",
                                    temp.filePath("key.pem"), "-out", temp.filePath("cert.pem"),
                                    "-days", "1", "-subj", "/CN=localhost"}),
                 0);
        brightness = std::make_unique<BrightnessController>(nullptr, temp.path(),
                                                            temp.filePath("settings.ini"));
        settings =
            std::make_unique<SettingsManager>(brightness.get(), temp.filePath("settings.ini"));
        receiver = std::make_unique<MediaReceiver>(settings.get(), temp.filePath("devices.json"));
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        auto port = probe.serverPort();
        probe.close();
        QVERIFY(receiver->start("127.0.0.1", port, temp.filePath("cert.pem"),
                                temp.filePath("key.pem")));
        url = "wss://127.0.0.1:" + QString::number(port);
    }
    void unconfiguredAgentExits() {
        QProcess agent;
        agent.start(QStringLiteral(DESKBUDDY_AGENT_PATH),
                    {"--config", temp.filePath("missing-config.json")});
        const bool finished = agent.waitForFinished(3000);
        if (!finished) {
            agent.kill();
            agent.waitForFinished();
        }
        QVERIFY2(finished,
                 "An unconfigured agent must exit instead of entering an idle event loop.");
        QCOMPARE(agent.exitCode(), 2);
        QVERIFY(agent.readAllStandardError().contains("not configured"));
    }
    void startupConfiguration() {
        QVERIFY(!ProtocolClient::configurationError({}, false).isEmpty());
        QJsonObject config{{"url", "wss://deskpi.local:8091"}, {"pin", QString(64, 'a')}};
        QVERIFY(ProtocolClient::configurationError(config, true).isEmpty());
        QVERIFY(!ProtocolClient::configurationError(config, false).isEmpty());
        config["device_id"] = "host";
        config["credential"] = QString(64, 'b');
        QVERIFY(ProtocolClient::configurationError(config, false).isEmpty());
        config["url"] = "ws://deskpi.local:8091";
        QVERIFY(!ProtocolClient::configurationError(config, false).isEmpty());
    }
    void validationAndInterpolation() {
        auto s = state();
        QVERIFY(MediaProtocol::state(s));
        s["seq"] = -1;
        QVERIFY(!MediaProtocol::state(s));
        s["seq"] = 1;
        QCOMPARE(MediaProtocol::position(s, 2000), 2000);
        s["status"] = "paused";
        QCOMPARE(MediaProtocol::position(s, 2000), 0);
        s["status"] = "playing";
        s["duration_ms"] = 1000;
        QCOMPARE(MediaProtocol::position(s, 2000), 1000);
        QVERIFY(MediaProtocol::parse("{garbage").isEmpty());
        QVERIFY(MediaProtocol::parse(QString(20000, 'x')).isEmpty());
        s["can_seek"] = "true";
        QVERIFY(!MediaProtocol::state(s));
    }
    void artworkProcessing() {
        QImage im(1200, 800, QImage::Format_RGB32);
        im.fill(QColor("#d05090"));
        QByteArray data;
        QBuffer b(&data);
        b.open(QIODevice::WriteOnly);
        im.save(&b, "PNG");
        QElapsedTimer clock;
        clock.start();
        auto result = ArtworkProcessor::process(data);
        qInfo() << "Artwork processing ms" << clock.elapsed() << "JPEG bytes" << result.jpeg.size();
        QVERIFY(!result.id.isEmpty());
        auto decoded = QImage::fromData(result.jpeg);
        QVERIFY(decoded.width() <= 400 && decoded.height() <= 400);
        QVERIFY(result.jpeg.size() < 100000);
        QVERIFY(result.palette.value("primary").toString().startsWith('#'));
        QVERIFY(ArtworkProcessor::process(QByteArray(5 * 1024 * 1024, 'x')).jpeg.isEmpty());
        QVERIFY(ArtworkProcessor::process("not an image").jpeg.isEmpty());
        ArtworkProcessor processor;
        QSignalSpy done(&processor, &ArtworkProcessor::ready);
        processor.request("http://unsafe.example/image");
        QCOMPARE(done.count(), 1);
        QVERIFY(qvariant_cast<Artwork>(done.last()[1]).jpeg.isEmpty());
    }
    void pairingAndRouting() {
        auto pairing = receiver->administer("pair", {});
        QVERIFY(pairing.contains("code"));
        pin = pairing.value("certificate_sha256").toString();
        QWebSocket socket;
        QSignalSpy incoming(&socket, &QWebSocket::textMessageReceived);
        connectSocket(socket);
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        auto o = MediaProtocol::message("pair");
        o["code"] = pairing.value("code");
        o["name"] = "Test host";
        o["os"] = "linux";
        socket.sendTextMessage(MediaProtocol::encode(o));
        QTRY_VERIFY(!receiver->devices().value("pending").toArray().isEmpty());
        auto id = receiver->devices()
                      .value("pending")
                      .toArray()
                      .first()
                      .toObject()
                      .value("id")
                      .toString();
        receiver->administer("approve", {{"id", id}});
        QTRY_VERIFY(incoming.count() >= 2);
        auto paired = MediaProtocol::parse(incoming.last()[0].toString());
        credential = paired.value("credential").toString();
        device = paired.value("device_id").toString();
        QCOMPARE(credential.size(), 64);
        QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
        socket.open(QUrl(url));
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        authenticate(socket);
        QTRY_VERIFY(receiver->current().value("connected").toBool());
        auto s = state();
        socket.sendTextMessage(MediaProtocol::encode(s));
        QTRY_COMPARE(receiver->current().value("session_id").toString(), QString("test"));
        QVERIFY(receiver->screenRequested());
        auto stale = s;
        stale["title"] = "stale";
        socket.sendTextMessage(MediaProtocol::encode(stale));
        QTest::qWait(30);
        QVERIFY(receiver->current().value("title") != "stale");
        incoming.clear();
        receiver->command("pause");
        QTRY_COMPARE(incoming.count(), 1);
        auto cmd = MediaProtocol::parse(incoming.last()[0].toString());
        QCOMPARE(cmd.value("session_id").toString(), QString("test"));
        QCOMPARE(cmd.value("action").toString(), QString("pause"));
        auto ack = MediaProtocol::message("command_ack");
        ack["command_id"] = cmd.value("command_id");
        ack["ok"] = true;
        socket.sendTextMessage(MediaProtocol::encode(ack));
        receiver->dismiss();
        QVERIFY(!receiver->screenRequested());
        receiver->reopen();
        QVERIFY(receiver->screenRequested());
        QCOMPARE(settings->apply("media", {{"returnSeconds", 1}}), QString());
        s["seq"] = 2;
        s["status"] = "paused";
        socket.sendTextMessage(MediaProtocol::encode(s));
        QTRY_VERIFY(!receiver->screenRequested());
        s["seq"] = 3;
        s["status"] = "playing";
        socket.sendTextMessage(MediaProtocol::encode(s));
        QTRY_VERIFY(receiver->screenRequested());
        socket.close();
        QTRY_VERIFY(!receiver->current().value("connected").toBool());
        QTRY_VERIFY(!receiver->screenRequested());
        QFile stored(temp.filePath("devices.json"));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        auto storedBytes = stored.readAll();
        QVERIFY(!storedBytes.contains(credential.toLatin1()));
    }
    void clientReconnectAndAuthentication() {
        StubProvider provider;
        QImage fixture(900, 600, QImage::Format_RGB32);
        fixture.fill(QColor("#c95a80"));
        auto path = temp.filePath("agent-artwork.png");
        QVERIFY(fixture.save(path));
        provider.artworkUri = QUrl::fromLocalFile(path).toString();
        QJsonObject config{
            {"url", url}, {"pin", pin}, {"device_id", device}, {"credential", credential}};
        ProtocolClient client(&provider, config, temp.filePath("agent.json"), {});
        client.start();
        QTRY_COMPARE(receiver->current().value("session_id").toString(), QString("stub"));
        QTRY_VERIFY(!receiver->artwork().isEmpty());
        QVERIFY(receiver->image().width() <= 400);
        QVERIFY(receiver->image().height() <= 400);
        receiver->command("pause");
        QTRY_COMPARE(provider.commands, 1);
        auto port = receiver->port();
        receiver.reset();
        receiver = std::make_unique<MediaReceiver>(settings.get(), temp.filePath("devices.json"));
        QVERIFY(receiver->start("127.0.0.1", port, temp.filePath("cert.pem"),
                                temp.filePath("key.pem")));
        QElapsedTimer reconnect;
        reconnect.start();
        QTRY_COMPARE(receiver->current().value("session_id").toString(), QString("stub"));
        qInfo() << "Reconnect ms" << reconnect.elapsed();
        for (int i = 0; i < 7; ++i) {
            auto ms = ProtocolClient::reconnectDelay(i);
            QVERIFY(ms >= 1000 && ms <= 40000);
        }
    }
    void multipleHostsAndPinning() {
        StubProvider badProvider;
        ProtocolClient wrongPin(&badProvider,
                                {{"url", url},
                                 {"pin", QString(64, 'b')},
                                 {"device_id", device},
                                 {"credential", credential}},
                                temp.filePath("bad.json"), {});
        wrongPin.start();
        QTest::qWait(150);
        QVERIFY(!receiver->current().value("connected").toBool());
        auto pairing = receiver->administer("pair", {});
        QWebSocket second;
        QSignalSpy replies(&second, &QWebSocket::textMessageReceived);
        connectSocket(second);
        QTRY_COMPARE(second.state(), QAbstractSocket::ConnectedState);
        auto hello = MediaProtocol::message("pair");
        hello["code"] = pairing.value("code");
        hello["name"] = "Second host";
        hello["os"] = "linux";
        second.sendTextMessage(MediaProtocol::encode(hello));
        QTRY_VERIFY(!receiver->devices().value("pending").toArray().isEmpty());
        auto id = receiver->devices()
                      .value("pending")
                      .toArray()
                      .first()
                      .toObject()
                      .value("id")
                      .toString();
        receiver->administer("approve", {{"id", id}});
        QTRY_VERIFY(replies.count() >= 2);
        auto credentials = MediaProtocol::parse(replies.last()[0].toString());
        QTRY_COMPARE(second.state(), QAbstractSocket::UnconnectedState);
        QWebSocket first;
        connectSocket(first);
        QTRY_COMPARE(first.state(), QAbstractSocket::ConnectedState);
        authenticate(first);
        QTRY_VERIFY(receiver->current().value("connected").toBool());
        auto s = state();
        first.sendTextMessage(MediaProtocol::encode(s));
        QTRY_COMPARE(receiver->current().value("session_id").toString(), QString("test"));
        second.open(QUrl(url));
        QTRY_COMPARE(second.state(), QAbstractSocket::ConnectedState);
        auto auth = MediaProtocol::message("authenticate");
        auth["device_id"] = id;
        auth["credential"] = credentials.value("credential");
        second.sendTextMessage(MediaProtocol::encode(auth));
        QTest::qWait(30);
        s["session_id"] = "second-session";
        second.sendTextMessage(MediaProtocol::encode(s));
        QTest::qWait(30);
        QCOMPARE(receiver->current().value("device_id").toString(), device);
        QCOMPARE(settings->apply("media", {{"preferredHost", id}}), QString());
        QTRY_COMPARE(receiver->current().value("device_id").toString(), id);
        replies.clear();
        receiver->command("pause");
        QTRY_VERIFY(!replies.isEmpty());
        auto cmd = MediaProtocol::parse(replies.last()[0].toString());
        QCOMPARE(cmd.value("session_id").toString(), QString("second-session"));
        auto ack = MediaProtocol::message("command_ack");
        ack["command_id"] = cmd.value("command_id");
        ack["ok"] = true;
        second.sendTextMessage(MediaProtocol::encode(ack));
        receiver->administer("revoke", {{"id", id}});
        QTRY_COMPARE(second.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(settings->apply("media", {{"preferredHost", ""}}), QString());
        first.close();
        QTRY_VERIFY(!receiver->current().value("connected").toBool());
    }
    void malformedAndRevocation() {
        QWebSocket socket;
        connectSocket(socket);
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        authenticate(socket);
        QTest::qWait(30);
        socket.sendTextMessage("{bad");
        QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
        socket.open(QUrl(url));
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        authenticate(socket);
        QTest::qWait(30);
        socket.sendBinaryMessage(QByteArray(MediaProtocol::ArtworkLimit + 65, 'x'));
        QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
        auto result = receiver->administer("revoke", {{"id", device}});
        QVERIFY(result.value("devices").toArray().isEmpty());
        socket.open(QUrl(url));
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        authenticate(socket);
        QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
    }
    void mediaIniTypesSurviveFreshLoad() {
        const auto path=temp.filePath("fresh-media-settings.ini");
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[media]\nenabled=false\nautoSwitch=false\nreturnSeconds=67\nartworkLimitKiB=96\nanimations=false\nshowArtwork=false\npreferredPlayer=org.mpris.MediaPlayer2.spotify\n");file.close();
        SettingsManager restored(brightness.get(),path);
        const auto media=restored.snapshot().value("media").toObject();
        QCOMPARE(media.value("enabled"),QJsonValue(false));
        QCOMPARE(media.value("autoSwitch"),QJsonValue(false));
        QCOMPARE(media.value("animations"),QJsonValue(false));
        QCOMPARE(media.value("showArtwork"),QJsonValue(false));
        QCOMPARE(media.value("returnSeconds"),QJsonValue(67));
        QCOMPARE(media.value("artworkLimitKiB"),QJsonValue(96));
        QCOMPARE(media.value("preferredPlayer").toString(),QString("org.mpris.MediaPlayer2.spotify"));
    }
    void configuration() {
        QCOMPARE(settings->apply("media", {{"preferredPlayer", "org.mpris.MediaPlayer2.test"},
                                           {"gradients", false}}),
                 QString());
        auto flushed = std::async(std::launch::async, [this] { return settings->flush(); });
        QTRY_VERIFY(flushed.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        QCOMPARE(flushed.get(), QString());
        SettingsManager restored(brightness.get(), temp.filePath("settings.ini"));
        QCOMPARE(restored.snapshot().value("media"), settings->snapshot().value("media"));
        QVERIFY(!settings->apply("media", {{"returnSeconds", -1}}).isEmpty());
        QVERIFY(!settings->apply("media", {{"enabled", "yes"}}).isEmpty());
        auto path = temp.filePath("private/config.json");
        QVERIFY(AgentConfig::save(path, {{"credential", "secret"}}));
        QVERIFY(!(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther)));
        QCOMPARE(AgentConfig::load(path).value("credential").toString(), QString("secret"));
    }
    void mprisDiscoveryAndCommands() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        FakePlayer fake;
        QVERIFY(bus.registerObject("/org/mpris/MediaPlayer2", &fake,
                                   QDBusConnection::ExportAllProperties |
                                       QDBusConnection::ExportAllSlots |
                                       QDBusConnection::ExportAllSignals));
        MprisProvider provider;
        QSignalSpy states(&provider, &MediaProvider::stateChanged);
        QSignalSpy acks(&provider, &MediaProvider::commandDone);
        provider.start();
        // Register after discovery has started to exercise ownership event detection.
        QVERIFY(bus.registerService("org.mpris.MediaPlayer2.deskbuddytest"));
        QTRY_VERIFY(!states.isEmpty() &&
                    states.last()[0].toJsonObject().value("title") == "Fake song");
        auto s = states.last()[0].toJsonObject();
        QCOMPARE(s.value("duration_ms").toInt(), 200000);
        QCOMPARE(s.value("position_ms").toInt(), 10000);
        QCOMPARE(s.value("artist").toString(), QString("Fake artist"));
        auto cmd = MediaProtocol::message("command");
        cmd["session_id"] = s.value("session_id");
        cmd["track_id"] = s.value("track_id");
        cmd["command_id"] = "pause-test";
        cmd["action"] = "pause";
        provider.execute(cmd);
        QTRY_COMPARE(fake.calls, 1);
        QTRY_VERIFY(!acks.isEmpty());
        QVERIFY(acks.last()[1].toBool());
        QTRY_COMPARE(states.last()[0].toJsonObject().value("status").toString(), QString("paused"));
        cmd["action"] = "seek";
        cmd["command_id"] = "seek-test";
        cmd["position_ms"] = 45000;
        provider.execute(cmd);
        QTRY_COMPARE(fake.calls, 2);
        QTRY_COMPARE(states.last()[0].toJsonObject().value("position_ms").toInt(), 45000);
        QVERIFY(bus.registerService("org.mpris.MediaPlayer2.asecond"));
        fake.playback = "Playing";
        fake.changed();
        {
            MprisProvider startup;
            QSignalSpy initial(&startup, &MediaProvider::stateChanged);
            startup.start();
            QTRY_VERIFY(!initial.isEmpty() &&
                        initial.last()[0].toJsonObject().value("session_id").toString() ==
                            "org.mpris.MediaPlayer2.asecond");
        }
        provider.prefer("org.mpris.MediaPlayer2.asecond");
        QTRY_COMPARE(states.last()[0].toJsonObject().value("session_id").toString(),
                     QString("org.mpris.MediaPlayer2.asecond"));
        bus.unregisterService("org.mpris.MediaPlayer2.asecond");
        QTRY_COMPARE(states.last()[0].toJsonObject().value("session_id").toString(),
                     QString("org.mpris.MediaPlayer2.deskbuddytest"));
        bus.unregisterService("org.mpris.MediaPlayer2.deskbuddytest");
        QTRY_COMPARE(states.last()[0].toJsonObject().value("status").toString(),
                     QString("stopped"));
        bus.unregisterObject("/org/mpris/MediaPlayer2");
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "media_tests.moc"
