#include "../media/Protocol.h"
#include "AgentConfig.h"
#include "MprisProvider.h"
#include "ProtocolClient.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QHostInfo>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>
class Simulation : public MediaProvider {
  public:
    QJsonObject state;
    QString art;
    QElapsedTimer age;
    int trackNumber = 0;
    explicit Simulation(QString uri) : art(uri) {
        state = MprisProvider::normalized(
            "simulator",
            {{"PlaybackStatus", "Playing"},
             {"CanControl", true},
             {"CanPlay", true},
             {"CanPause", true},
             {"CanGoNext", true},
             {"CanGoPrevious", true},
             {"CanSeek", true},
             {"Metadata", QVariantMap{{"xesam:title", "DeskBuddy test track"},
                                      {"xesam:artist", QStringList{"Simulated artist"}},
                                      {"mpris:trackid", "/test/track"},
                                      {"mpris:length", qlonglong(240000000)}}}});
        state["media_kind"] = "music";
    }
    void start() override {
        state["position_ms"] =
            double(MediaProtocol::position(state, age.isValid() ? age.elapsed() : 0));
        age.restart();
        emit stateChanged(state, art);
    }
    void prefer(const QString &) override { start(); }
    void execute(const QJsonObject &o) override {
        state["position_ms"] =
            double(MediaProtocol::position(state, age.isValid() ? age.elapsed() : 0));
        age.restart();
        auto action = o.value("action").toString();
        bool ok = true;
        if (action == "pause")
            state["status"] = "paused";
        else if (action == "play")
            state["status"] = "playing";
        else if (action == "toggle")
            state["status"] = state.value("status") == "playing" ? "paused" : "playing";
        else if (action == "seek")
            state["position_ms"] = o.value("position_ms");
        else if (action == "next" || action == "previous") {
            state["title"] = "Another simulated track";
            state["track_id"] = QString("/test/track%1").arg(++trackNumber);
            state["position_ms"] = 0;
        } else
            ok = false;
        emit commandDone(o.value("command_id").toString(), ok, ok ? "" : "Unsupported");
        start();
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("deskbuddy-agent");
    app.setApplicationVersion("1.0.1");
    if (geteuid() == 0) {
        qCritical() << "Run deskbuddy-agent as your desktop user.";
        return 2;
    }
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions(
        {{"url", "DeskBuddy wss://address:8091", "url"},
         {"pin", "SHA256 certificate fingerprint verified on the Pi", "hex"},
         {"pair-code", "Short lived pairing code (prefer DESKBUDDY_PAIR_CODE environment)", "code"},
         {"name", "Computer name", "name"},
         {"config", "Configuration file", "path"},
         {"inspect",
          "Read selected MPRIS source and print a redacted diagnostic, without connecting"},
         {"simulate", "Use a simulated media source"},
         {"artwork", "Simulator artwork URI", "uri"}});
    parser.process(app);
    QString path = parser.value("config");
    if (path.isEmpty())
        path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
               "/deskbuddy-agent/config.json";
    auto config = AgentConfig::load(path);
    for (auto key : {"url", "pin", "name"})
        if (parser.isSet(key))
            config[key] = parser.value(key);
    if (!config.contains("name"))
        config["name"] = QHostInfo::localHostName();
    QString code = parser.value("pair-code");
    if (code.isEmpty())
        code = qEnvironmentVariable("DESKBUDDY_PAIR_CODE");
    qunsetenv("DESKBUDDY_PAIR_CODE");
    if (!parser.isSet("inspect")) {
        const auto error = ProtocolClient::configurationError(config, !code.isEmpty());
        if (!error.isEmpty()) {
            fprintf(stderr, "%s\nConfiguration file: %s\n", qPrintable(error), qPrintable(path));
            return 2;
        }
    }
    sigset_t shutdownSignals;
    sigemptyset(&shutdownSignals);
    sigaddset(&shutdownSignals, SIGTERM);
    sigaddset(&shutdownSignals, SIGINT);
    if (sigprocmask(SIG_BLOCK, &shutdownSignals, nullptr) != 0)
        return 2;
    int signalFd = signalfd(-1, &shutdownSignals, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signalFd < 0)
        return 2;
    QSocketNotifier notifier(signalFd, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
        signalfd_siginfo info;
        ::read(signalFd, &info, sizeof(info));
        app.quit();
    });
    std::unique_ptr<MediaProvider> provider;
    if (parser.isSet("simulate"))
        provider = std::make_unique<Simulation>(parser.value("artwork"));
    else
        provider = std::make_unique<MprisProvider>();
    if (parser.isSet("inspect")) {
        QObject::connect(provider.get(), &MediaProvider::stateChanged, &app,
                         [&](QJsonObject state, QString uri) {
                             if (state.value("session_id").toString().isEmpty())
                                 return;
                             QJsonObject diagnostic{
                                 {"session_id", state.value("session_id")},
                                 {"status", state.value("status")},
                                 {"has_title", !state.value("title").toString().isEmpty()},
                                 {"has_artwork_uri", !uri.isEmpty()},
                                 {"duration_known", state.value("duration_ms").toDouble() > 0},
                                 {"can_control", state.value("can_control")},
                                 {"can_seek", state.value("can_seek")}};
                             auto bytes = QJsonDocument(diagnostic).toJson();
                             fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
                             QTimer::singleShot(0, &app, &QCoreApplication::quit);
                         });
        QTimer::singleShot(5000, &app, [&] { app.exit(3); });
        provider->start();
        int result = app.exec();
        ::close(signalFd);
        return result;
    }
    ProtocolClient client(provider.get(), config, path, code);
    provider->start();
    client.start();
    int result = app.exec();
    ::close(signalFd);
    return result;
}
