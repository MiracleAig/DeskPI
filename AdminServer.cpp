// Parser and queue limits apply before route handlers allocate/parse JSON.
#define CPPHTTPLIB_REQUEST_URI_MAX_LENGTH 256
#define CPPHTTPLIB_HEADER_MAX_LENGTH 2048
#define CPPHTTPLIB_HEADER_MAX_COUNT 32
#define CPPHTTPLIB_RECV_BUFSIZ size_t(4096u)
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "third_party/httplib.h"
#include "AdminServer.h"
#include "AdminAuth.h"
#include "SystemMetrics.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QHostAddress>
#include <QCommandLineParser>
#include <QTimer>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <thread>
#include <atomic>
#include <QThread>
#include <unistd.h>
#include <cstdio>

namespace {
QByteArray bytes(const std::string &s) { return QByteArray(s.data(), qsizetype(s.size())); }
std::string str(const QByteArray &b) { return std::string(b.constData(), size_t(b.size())); }
QJsonObject failure(const QString &message) { return {{"error", QJsonObject{{"message", message}}}}; }
void json(httplib::Response &res, const QJsonObject &object, int status = 200) {
    res.status = status;
    res.set_content(str(QJsonDocument(object).toJson(QJsonDocument::Compact)), "application/json; charset=utf-8");
}
void fail(httplib::Response &res, int status, const QString &message) { json(res, failure(message), status); }
QByteArray cookie(const httplib::Request &req) {
    const auto parts = bytes(req.get_header_value("Cookie")).split(';');
    const QByteArray name("deskpi_session=");
    QByteArray token;
    for (auto part : parts) { part = part.trimmed(); if (part.startsWith(name)) { if (!token.isEmpty()) return {}; token = part.mid(name.size()); } }
    return token;
}
QString hostPort(const QString &host, int port) {
    return (host.contains(':') ? "[" + host + "]" : host.toLower()) + ":" + QString::number(port);
}
bool saveJson(const QString &path, const QJsonObject &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto content = QJsonDocument(data).toJson();
    return f.write(content) == content.size() && f.commit();
}
}
struct AdminServer::Impl {
    SettingsManager *settings;
    Options options;
    AdminAuth auth;
    SystemMetrics metrics;
    std::unique_ptr<httplib::Server> server;
    std::thread listener;
    std::atomic<bool> finished{false};
    QHash<QString, QByteArray> assets;
    Impl(SettingsManager *s, Options o) : settings(s), options(std::move(o)), auth(options.authFile) {}
    QJsonObject snapshot(bool exportOnly = false) {
        QJsonObject result;
        QMetaObject::invokeMethod(settings, [&, this] { result = exportOnly ? settings->exportConfig() : settings->snapshot(); }, Qt::BlockingQueuedConnection);
        return result;
    }
    QString apply(const QString &section, const QJsonObject &patch, bool imported) {
        QString error;
        QMetaObject::invokeMethod(settings, [&, this] { error = imported ? settings->importConfig(patch) : settings->apply(section, patch); }, Qt::BlockingQueuedConnection);
        if (error.isEmpty()) error = settings->flush();
        return error;
    }
    void security(httplib::Response &res) {
        res.set_header("Cache-Control", "no-store");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
        res.set_header("Connection", "close");
        if (!options.certificate.isEmpty()) res.set_header("Strict-Transport-Security", "max-age=31536000");
    }
    void serve(const httplib::Request &req, httplib::Response &res) {
        security(res);
        if (!AdminServer::privateAddress(QString::fromStdString(req.remote_addr))) { fail(res, 403, "Only loopback/private-network clients are allowed."); return; }
        const QString host = QString::fromStdString(req.get_header_value("Host")).toLower();
        if (req.get_header_value_count("Host") != 1 || !options.hosts.contains(host)) { fail(res, 403, "Host is not allowed."); return; }
        const QString path = QString::fromStdString(req.path);
        const bool changing = req.method != "GET" && req.method != "HEAD";
        const QString expectedOrigin = (options.certificate.isEmpty() ? "http://" : "https://") + host;
        if ((changing && QString::fromStdString(req.get_header_value("Origin")) != expectedOrigin)
            || req.get_header_value("Sec-Fetch-Site") == "cross-site") { fail(res, 403, "Same-origin request required."); return; }
        if (req.body.size() > 16384) { fail(res, 413, "Request body exceeds 16 KiB."); return; }
        if (req.path.find("..") != std::string::npos || req.path.find('\\') != std::string::npos) { fail(res, 404, "Not found."); return; }
        if (!changing && (path == "/" || path == "/app.js" || path == "/style.css")) {
            const QString asset = path == "/" ? "index.html" : path.mid(1);
            const bool gzip = req.get_header_value("Accept-Encoding").find("gzip") != std::string::npos;
            const auto data = assets.value(asset + (gzip ? ".gz" : ""));
            const auto etag = '"' + QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() + '"';
            res.set_header("ETag", str(etag)); res.set_header("Vary", "Accept-Encoding");
            if (path != "/") res.set_header("Cache-Control", "private, max-age=3600");
            if (bytes(req.get_header_value("If-None-Match")) == etag) { res.status = 304; return; }
            res.set_content(str(data), asset.endsWith("js") ? "text/javascript; charset=utf-8" : asset.endsWith("css") ? "text/css; charset=utf-8" : "text/html; charset=utf-8");
            if (gzip) res.set_header("Content-Encoding", "gzip");
            return;
        }
        if (path == "/api/v1/health" && req.method == "GET") { json(res, {{"ok", true}}); return; }
        const auto token = cookie(req);
        auto *session = auth.session(token);
        if (path == "/api/v1/session" && req.method == "GET") {
            json(res, {{"authenticated", session != nullptr}, {"setupRequired", !auth.configured()},
                       {"csrf", session ? QString::fromLatin1(session->csrf) : QString()}}); return;
        }
        QJsonObject body;
        if (changing) {
            if (req.get_header_value("Content-Type") != "application/json") { fail(res, 415, "Content-Type must be application/json."); return; }
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(bytes(req.body), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) { fail(res, 400, "Expected a valid JSON object."); return; }
            body = doc.object();
        }
        if (path == "/api/v1/login" && req.method == "POST") {
            if (!auth.configured()) { fail(res, 503, "Run appdeskpi --admin-set-password from a local/SSH terminal, then restart DeskPI."); return; }
            if (!auth.rateAllowed(QString::fromStdString(req.remote_addr))) { res.set_header("Retry-After", "60"); fail(res, 429, "Too many login attempts. Try again in one minute."); return; }
            if (body.size() != 1 || !body.value("password").isString() || body.value("password").toString().toUtf8().size() > 128) { fail(res, 400, "Please enter your password. If it is very long, try a shorter password."); return; }
            const auto newToken = auth.login(body.value("password").toString().toUtf8());
            if (newToken.isEmpty()) { fail(res, 401, "Invalid credentials."); return; }
            const auto *newSession = auth.session(newToken);
            res.set_header("Set-Cookie", "deskpi_session=" + str(newToken) + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800" + (options.certificate.isEmpty() ? "" : "; Secure"));
            json(res, {{"authenticated", true}, {"csrf", QString::fromLatin1(newSession->csrf)}}); return;
        }
        if (!session) { fail(res, 401, "Authentication required."); return; }
        if (changing && !auth.csrfValid(*session, bytes(req.get_header_value("X-CSRF-Token")))) { fail(res, 403, "Invalid CSRF token."); return; }
        if (path == "/api/v1/logout" && req.method == "POST") {
            if (!body.isEmpty()) { fail(res, 400, "Logout body must be empty."); return; }
            auth.logout(token);
            res.set_header("Set-Cookie", "deskpi_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0" + std::string(options.certificate.isEmpty() ? "" : "; Secure"));
            json(res, {{"ok", true}}); return;
        }
        if (path == "/api/v1/settings" && req.method == "GET") { json(res, snapshot()); return; }
        if (path == "/api/v1/status" && req.method == "GET") {
            auto status = metrics.collect(); status["display"] = snapshot().value("display"); json(res, status); return;
        }
        if (path == "/api/v1/config/export" && req.method == "GET") {
            res.set_header("Content-Disposition", "attachment; filename=deskpi-config.json"); json(res, snapshot(true)); return;
        }
        if (path == "/api/v1/config/import" && req.method == "POST") {
            // Validate the entire file before taking a recovery snapshot or touching live state.
            QString validation;
            QMetaObject::invokeMethod(settings, [&] {
                const auto current = settings->snapshot();
                const auto expected = settings->exportConfig().value("settings").toObject();
                if (body.size() != 2 || body.value("schemaVersion") != QJsonValue(1) || !body.value("settings").isObject() || body.value("settings").toObject().size() != 3) validation = "Unsupported configuration format.";
                else for (const QString section : {"display", "clock", "appearance"}) {
                    const auto value = body.value("settings").toObject().value(section);
                    if (!value.isObject() || value.toObject().size() != expected.value(section).toObject().size()) { validation = "Incomplete configuration section."; break; }
                    validation = SettingsManager::validate(section, value.toObject(), current);
                    if (!validation.isEmpty()) break;
                }
            }, Qt::BlockingQueuedConnection);
            if (!validation.isEmpty()) { fail(res, 400, validation); return; }
            if (!saveJson(settings->settingsPath() + ".previous.json", snapshot(true))) { fail(res, 500, "Cannot save recovery configuration; import not applied."); return; }
            const auto error = apply({}, body, true);
            if (!error.isEmpty()) { fail(res, 409, error); return; }
            json(res, snapshot()); return;
        }
        if (path.startsWith("/api/v1/settings/") && req.method == "PATCH") {
            const QString section = path.mid(QString("/api/v1/settings/").size());
            const auto error = apply(section, body, false);
            if (!error.isEmpty()) { fail(res, error.contains("unavailable") ? 409 : error.contains("persist") ? 500 : 400, error); return; }
            json(res, snapshot()); return;
        }
        if (path == "/api/v1/application/restart" && req.method == "POST") {
            if (body.size() != 1 || body.value("confirm") != QJsonValue(true)) { fail(res, 400, "Explicit confirm:true is required."); return; }
            const auto error = settings->flush();
            if (!error.isEmpty()) { fail(res, 500, error); return; }
            json(res, {{"restarting", true}}, 202);
            QMetaObject::invokeMethod(settings, [] { QTimer::singleShot(500, QCoreApplication::instance(), [] { QCoreApplication::exit(75); }); }, Qt::QueuedConnection);
            return;
        }
        fail(res, 404, "Not found.");
    }
};
AdminServer::AdminServer(SettingsManager *s, Options o) : d(std::make_unique<Impl>(s, std::move(o))) { Q_INIT_RESOURCE(web_assets); }
AdminServer::~AdminServer() { stop(); }
QString AdminServer::directory() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/DeskPI"; }
AdminServer::Options AdminServer::options() {
    Options o;
    QFile file(directory() + "/admin-listener.json");
    if (file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.read(8192)).object();
        o.address = doc.value("address").toString("127.0.0.1"); o.port = doc.value("port").toInt(8080);
        for (const auto &host : doc.value("hosts").toArray()) o.hosts.append(host.toString());
        o.certificate = doc.value("certificate").toString(); o.key = doc.value("key").toString();
    }
    o.authFile = directory() + "/admin-auth.ini";
    if (o.hosts.isEmpty()) o.hosts = {hostPort("localhost", o.port), hostPort(o.address, o.port)};
    return o;
}
bool AdminServer::privateAddress(const QString &address) {
    const QHostAddress ip(address);
    if (ip.isLoopback()) return true;
    const auto ipv4 = ip.toIPv4Address();
    return (ipv4 >> 24) == 10 || (ipv4 >> 20) == 0xac1 || (ipv4 >> 16) == 0xc0a8 || (ip.protocol() == QAbstractSocket::IPv6Protocol && (ip.toIPv6Address()[0] & 0xfe) == 0xfc);
}
bool AdminServer::start(QString *error) {
    const auto &o = d->options;
    const bool loopback = QHostAddress(o.address).isLoopback();
    if (!privateAddress(o.address) || o.port < 1024 || o.port > 65535 || (!loopback && (o.certificate.isEmpty() || !d->auth.configured()))) { *error = "Admin listener rejected: use loopback, or a private address with TLS and a configured password."; return false; }
    if (o.certificate.isEmpty()) d->server = std::make_unique<httplib::Server>();
    else {
        d->server = std::make_unique<httplib::SSLServer>(o.certificate.toUtf8().constData(), o.key.toUtf8().constData());
        if (!d->server->is_valid()) { *error = "Cannot load admin TLS certificate/key."; return false; }
    }
    d->server->new_task_queue = [] { return new httplib::ThreadPool(1, 0, 4); };
    d->server->set_payload_max_length(16384).set_read_timeout(3).set_write_timeout(3).set_keep_alive_max_count(1).set_idle_interval(60);
    for (const QString &name : {"index.html", "app.js", "style.css"}) for (const QString &suffix : {QString(), QString(".gz")}) {
        QFile f(":/web/" + name + suffix); if (!f.open(QIODevice::ReadOnly)) { *error = "Missing embedded frontend asset."; return false; }
        d->assets.insert(name + suffix, f.readAll());
    }
    d->server->set_pre_routing_handler([this](const httplib::Request &req, httplib::Response &res) {
        const auto host = QString::fromStdString(req.get_header_value("Host")).toLower();
        const auto origin = (d->options.certificate.isEmpty() ? "http://" : "https://") + host;
        const bool changes = req.method != "GET" && req.method != "HEAD";
        if (req.get_header_value_count("Host") != 1 || !d->options.hosts.contains(host)
            || (changes && QString::fromStdString(req.get_header_value("Origin")) != origin)
            || req.get_header_value("Sec-Fetch-Site") == "cross-site"
            || !AdminServer::privateAddress(QString::fromStdString(req.remote_addr))) {
            d->security(res); fail(res, 403, "Host/origin/client is not allowed.");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    const auto handler = [this](const httplib::Request &req, httplib::Response &res) {
        try { d->serve(req, res); } catch (...) { d->security(res); fail(res, 500, "Internal administration error."); }
    };
    for (const auto *path : {"/", "/app.js", "/style.css", "/api/v1/health", "/api/v1/session", "/api/v1/settings", "/api/v1/status", "/api/v1/config/export"}) d->server->Get(path, handler);
    for (const auto *path : {"/api/v1/login", "/api/v1/logout", "/api/v1/config/import", "/api/v1/application/restart"}) d->server->Post(path, handler);
    for (const auto *path : {"/api/v1/settings/display", "/api/v1/settings/clock", "/api/v1/settings/appearance"}) d->server->Patch(path, handler);
    d->server->set_error_handler([this](const httplib::Request &, httplib::Response &res) {
        d->security(res); if (res.body.empty()) fail(res, res.status, "HTTP request rejected.");
    });
    if (!d->server->bind_to_port(o.address.toUtf8().constData(), o.port)) { *error = "Cannot bind admin listener (address unavailable or port in use)."; return false; }
    d->listener = std::thread([this] { d->server->listen_after_bind(); d->finished = true; });
    return true;
}
void AdminServer::stop() {
    if (d->server) d->server->stop();
    // Destruction occurs after the GUI event loop exits. Any queued GUI callbacks
    // must finish before the caller exits the event loop (aboutToQuit hook below).
    if (d->listener.joinable()) {
        while (!d->finished.load()) {
            if (QThread::currentThread() == d->settings->thread()) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        d->listener.join();
    }
}
int AdminServer::localCommand(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.addOptions({{"admin-set-password", "Set/reset the administrator password locally."},
        {"admin-configure", "Configure listener; non-loopback requires TLS and password."},
        {"bind", "Listener IP address.", "address", "127.0.0.1"}, {"port", "Port.", "port", "8080"},
        {"host", "Additional allowed DNS hostname (repeatable).", "host"},
        {"tls-cert", "PEM certificate path.", "path"}, {"tls-key", "PEM private-key path.", "path"}});
    if (!parser.parse(arguments)) { fprintf(stderr, "%s\n", qPrintable(parser.errorText())); return 2; }
    if (parser.isSet("admin-set-password")) {
        if (!::isatty(STDIN_FILENO)) { fprintf(stderr, "Use an interactive local or SSH terminal. Passwords are never command-line arguments.\n"); return 2; }
        const QByteArray first(::getpass("Choose a password (at least 12 letters, numbers, or symbols): "));
        const QByteArray second(::getpass("Enter the same password again: "));
        if (first != second) { fprintf(stderr, "The passwords did not match. Please try again.\n"); return 2; }
        QString error;
        if (!AdminAuth::setPassword(directory() + "/admin-auth.ini", first, &error)) { fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
        fprintf(stdout, "Your password has been saved. Restart DeskPI to use it.\n"); return 0;
    }
    if (!parser.isSet("admin-configure")) { fprintf(stderr, "Use --admin-set-password or --admin-configure.\n"); return 2; }
    Options o; o.address = parser.value("bind"); o.port = parser.value("port").toInt();
    o.certificate = QFileInfo(parser.value("tls-cert")).absoluteFilePath(); o.key = QFileInfo(parser.value("tls-key")).absoluteFilePath();
    if (!parser.isSet("tls-cert")) o.certificate.clear(); if (!parser.isSet("tls-key")) o.key.clear();
    const bool loopback = QHostAddress(o.address).isLoopback();
    AdminAuth auth(directory() + "/admin-auth.ini");
    if (!privateAddress(o.address) || o.port < 1024 || o.port > 65535 || (!loopback && (o.certificate.isEmpty() || o.key.isEmpty() || !auth.configured()))) {
        fprintf(stderr, "Use a loopback address or a private LAN address with TLS files and an administrator password. Port must be 1024–65535.\n"); return 2;
    }
    if (!o.certificate.isEmpty() && (!QFileInfo::exists(o.certificate) || !QFileInfo::exists(o.key) || (QFileInfo(o.key).permissions() & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)))) {
        fprintf(stderr, "TLS files must exist and the private key must be owner-only (chmod 600).\n"); return 2;
    }
    o.hosts = {hostPort(o.address, o.port)};
    if (loopback) o.hosts.append(hostPort("localhost", o.port));
    for (const QString &host : parser.values("host")) {
        if (!QRegularExpression("^[a-zA-Z0-9][a-zA-Z0-9.-]{0,252}$").match(host).hasMatch()) { fprintf(stderr, "Invalid allowed hostname.\n"); return 2; }
        o.hosts.append(hostPort(host, o.port));
    }
    QJsonArray hosts; for (const auto &host : o.hosts) hosts.append(host);
    if (!saveJson(directory() + "/admin-listener.json", {{"address", o.address}, {"port", o.port}, {"hosts", hosts}, {"certificate", o.certificate}, {"key", o.key}})) return 1;
    fprintf(stdout, "Listener configuration saved. Restart DeskPI to apply.\n"); return 0;
}
