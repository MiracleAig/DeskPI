#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QProcess>
#include <QSslConfiguration>
#include <QSslCertificate>
#include "AdminServer.h"
#include "AdminAuth.h"
#include "SystemMetrics.h"

class AdminTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    std::unique_ptr<BrightnessController> brightness;
    std::unique_ptr<SettingsManager> settings;
    std::unique_ptr<AdminServer> server;
    QNetworkAccessManager network;
    QString base;
    QByteArray sessionCookie, csrf;
    struct Reply { int code; QJsonObject json; QByteArray data, cookie, csp; };
    Reply request(const QString &path, const QByteArray &method = "GET", QByteArray data = {}, bool protection = true, const QByteArray &host = {}) {
        QNetworkRequest req(QUrl(base + path));
        req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        if (!host.isEmpty()) req.setRawHeader("Host", host);
        req.setRawHeader("Cookie", sessionCookie);
        if (method != "GET") {
            req.setRawHeader("Origin", base.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            if (protection) req.setRawHeader("X-CSRF-Token", csrf);
        }
        auto *reply = network.sendCustomRequest(req, method, data);
        QSignalSpy done(reply, &QNetworkReply::finished);
        if (!done.wait(30000)) { reply->abort(); return {0, {}, {}, {}, {}}; }
        const auto body = reply->readAll();
        Reply result{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), QJsonDocument::fromJson(body).object(), body, reply->rawHeader("Set-Cookie"), reply->rawHeader("Content-Security-Policy")};
        reply->deleteLater(); return result;
    }
private slots:
    void initTestCase() {
        QCoreApplication::setApplicationName("DeskBuddyTests"); QCoreApplication::setOrganizationName("DeskBuddyTests");
        QDir().mkpath(temp.path() + "/panel");
        for (const auto &name : {"max_brightness", "brightness"}) {
            QFile f(temp.path() + "/panel/" + name); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(qstrcmp(name,"max_brightness") == 0 ? "255" : "204");
        }
        brightness = std::make_unique<BrightnessController>(nullptr, temp.path(), temp.filePath("settings.ini"));
        settings = std::make_unique<SettingsManager>(brightness.get(), temp.filePath("settings.ini"));
        QString error; QVERIFY(AdminAuth::setPassword(temp.filePath("auth.ini"), "test password only 12345", &error));
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0)); int port = probe.serverPort(); probe.close();
        AdminServer::Options options; options.port = port; options.authFile = temp.filePath("auth.ini"); options.hosts = {"127.0.0.1:" + QString::number(port)};
        base = "http://127.0.0.1:" + QString::number(port);
        server = std::make_unique<AdminServer>(settings.get(), options); QVERIFY2(server->start(&error), qPrintable(error));
        QTRY_VERIFY(brightness->available());
    }
    void publicAndHostChecks() {
        QCOMPARE(request("/api/v1/health").code, 200);
        QCOMPARE(request("/api/v1/settings").code, 401);
        QCOMPARE(request("/api/v1/status", "GET", {}, true, "evil.example").code, 403);
        const auto page = request("/"); QCOMPARE(page.code, 200); QVERIFY(page.data.contains("DeskBuddy")); QVERIFY(page.csp.contains("default-src 'none'"));
        QVERIFY(request("/app.js").data.contains("setInterval(refresh,8000)"));
        QCOMPARE(request("/../../etc/passwd").code, 404);
    }
    void loginAndGuiResponsiveness() {
        QCOMPARE(request("/api/v1/login", "POST", R"({"password":"incorrect"})").code, 401);
        int ticks = 0; QTimer timer; connect(&timer, &QTimer::timeout, [&] { ++ticks; }); timer.start(5);
        const auto reply = request("/api/v1/login", "POST", R"({"password":"test password only 12345"})");
        QCOMPARE(reply.code, 200); QVERIFY(ticks > 5); QVERIFY(reply.cookie.contains("HttpOnly")); QVERIFY(reply.cookie.contains("SameSite=Strict"));
        sessionCookie = reply.cookie.split(';').first(); csrf = reply.json.value("csrf").toString().toLatin1(); QVERIFY(!csrf.isEmpty());
        QCOMPARE(request("/api/v1/settings").code, 200);
    }
    void csrfAndValidation() {
        QCOMPARE(request("/api/v1/settings/clock", "PATCH", R"({"seconds":true})", false).code, 403);
        for (const QByteArray invalid : {QByteArray("[]"), QByteArray("{"), QByteArray(R"({"seconds":"true"})"), QByteArray(R"({"unsupported":3})")}) QCOMPARE(request("/api/v1/settings/clock", "PATCH", invalid).code, 400);
        QCOMPARE(request("/api/v1/settings/display", "PATCH", R"({"manual":101})").code, 400);
        QCOMPARE(request("/api/v1/settings/display", "PATCH", R"({"manual":50.5})").code, 400);
        QCOMPARE(request("/api/v1/settings/appearance", "PATCH", R"({"accent":"red;evil"})").code, 400);
        QCOMPARE(request("/api/v1/settings/clock", "PATCH", QByteArray(17000,'x')).code, 413);
    }
    void settingsAndPersistence() {
        QCOMPARE(request("/api/v1/settings/clock", "PATCH", R"({"format24":true,"seconds":true,"date":true,"dateFormat":"yyyy-MM-dd"})").code, 200);
        QVERIFY(settings->clock24()); QVERIFY(settings->showSeconds()); QVERIFY(settings->showDate());
        QSettings persisted(temp.filePath("settings.ini"), QSettings::IniFormat); QCOMPARE(persisted.value("clock/dateFormat").toString(), QString("yyyy-MM-dd"));
        QCOMPARE(request("/api/v1/settings/appearance", "PATCH", R"({"theme":"light","accent":"#123456"})").code, 200);
        QCOMPARE(settings->theme(), QString("light"));
        QCOMPARE(request("/api/v1/settings/display", "PATCH", R"({"manual":50,"automatic":true})").code, 200);
        QTRY_COMPARE(BacklightIo::inspect(temp.path()).raw, 128); QVERIFY(brightness->overrideActive());
        request("/api/v1/settings/clock", "PATCH", R"({"seconds":false})"); QVERIFY(brightness->overrideActive());
        brightness->setManual(60);
        const auto browser = request("/api/v1/settings").json.value("display").toObject(); QCOMPARE(browser.value("manual").toInt(), 60);
    }
    void configImportExport() {
        const auto exported = request("/api/v1/config/export"); QCOMPARE(exported.code, 200);
        QVERIFY(!exported.data.contains("password")); QVERIFY(!exported.data.contains("salt")); QVERIFY(!exported.data.contains("hash"));
        auto broken = exported.json; auto sections = broken.value("settings").toObject(); auto clock = sections.value("clock").toObject(); clock["seconds"] = "bad"; sections["clock"] = clock; broken["settings"] = sections;
        const auto old = settings->exportConfig();
        QCOMPARE(request("/api/v1/config/import", "POST", QJsonDocument(broken).toJson()).code, 400); QCOMPARE(settings->exportConfig(), old);
        QCOMPARE(request("/api/v1/config/import", "POST", exported.data).code, 200);
        QVERIFY(QFileInfo::exists(temp.filePath("settings.ini.previous.json")));
        QVERIFY(!brightness->overrideActive());
        QCOMPARE(request("/api/v1/application/restart", "POST", R"({"confirm":false})").code, 400);
    }
    void missingSensorsAndHardware() {
        auto metrics = SystemMetrics(temp.filePath("missing-proc"), temp.filePath("missing-sys")).collect();
        QVERIFY(metrics.value("temperatureC").isNull()); QVERIFY(metrics.value("memoryTotal").isNull());
        QCOMPARE(request("/api/v1/status").code, 200);
        QDir(temp.path() + "/panel").removeRecursively(); QTRY_VERIFY_WITH_TIMEOUT(!brightness->available(), 4000);
        QCOMPARE(request("/api/v1/settings/display", "PATCH", R"({"manual":50})").code, 409);
    }
    void concurrentRequests() {
        QList<QNetworkReply *> replies;
        for (int i=0; i<4; ++i) { QNetworkRequest req(QUrl(base + "/api/v1/status")); req.setRawHeader("Cookie", sessionCookie); replies.append(network.get(req)); }
        for (auto *reply : replies) { if (!reply->isFinished()) { QSignalSpy done(reply, &QNetworkReply::finished); QVERIFY(done.wait(10000)); } QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),200); reply->deleteLater(); }
    }
    void tlsCookies() {
        const auto cert = temp.filePath("test-cert.pem"), key = temp.filePath("test-key.pem");
        QProcess openssl;
        openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=IP:127.0.0.1,DNS:localhost", "-keyout", key, "-out", cert});
        QVERIFY(openssl.waitForFinished(30000)); QCOMPARE(openssl.exitCode(), 0);
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0)); const int port = probe.serverPort(); probe.close();
        AdminServer::Options o; o.port = port; o.certificate = cert; o.key = key; o.authFile = temp.filePath("auth.ini"); o.hosts = {"127.0.0.1:" + QString::number(port)};
        AdminServer tls(settings.get(), o); QString error; QVERIFY2(tls.start(&error), qPrintable(error));
        const auto oldBase = base; base = "https://127.0.0.1:" + QString::number(port);
        auto previous = QSslConfiguration::defaultConfiguration(); auto trusted = previous;
        QFile f(cert); QVERIFY(f.open(QIODevice::ReadOnly)); trusted.addCaCertificates(QSslCertificate::fromData(f.readAll())); QSslConfiguration::setDefaultConfiguration(trusted);
        auto reply = request("/api/v1/login", "POST", R"({"password":"test password only 12345"})");
        base = oldBase; QSslConfiguration::setDefaultConfiguration(previous);
        QCOMPARE(reply.code, 200); QVERIFY(reply.cookie.contains("; Secure")); tls.stop();
    }
    void frontendAssets() {
        QFile jsFile(":/web/app.js"); QVERIFY(jsFile.open(QIODevice::ReadOnly));
        const auto script = QString::fromUtf8(jsFile.readAll());
        QJSEngine engine;
        engine.globalObject().setProperty("globalThis", engine.globalObject());
        const auto compiled = engine.evaluate("(function(){" + script + "\n})");
        QVERIFY2(compiled.isCallable(), qPrintable(compiled.toString()));
        const auto helpers = script.mid(script.indexOf("function time("), script.indexOf("function setInput(") - script.indexOf("function time("));
        QVERIFY(!engine.evaluate(helpers).isError());
        QCOMPARE(engine.evaluate("time(420)").toString(), QString("07:00"));
        QCOMPARE(engine.evaluate("minute('22:00')").toInt(), 1320);
        QFile htmlFile(":/web/index.html"); QVERIFY(htmlFile.open(QIODevice::ReadOnly));
        const auto html = QString::fromUtf8(htmlFile.readAll());
        auto ids = QRegularExpression("\\$\\('([^']+)'\\)").globalMatch(script);
        while (ids.hasNext()) { const auto id = ids.next().captured(1); QVERIFY2(html.contains("id=\"" + id + "\""), qPrintable(id)); }
        // Lightweight DOM/fetch harness exercises the real frontend event handlers
        // without a browser runtime or frontend packages.
        const QString model = QString::fromUtf8(QJsonDocument(settings->snapshot()).toJson(QJsonDocument::Compact));
        const QString metrics = QString::fromUtf8(QJsonDocument(SystemMetrics().collect()).toJson(QJsonDocument::Compact));
        const QString harness = R"JS(
            globalThis.sent=[]; globalThis.nodes={};
            function node(id){return nodes[id]||(nodes[id]={id,hidden:false,value:'',checked:false,dataset:{},handlers:{},classList:{toggle(){},contains(){return false}},style:{setProperty(){}},addEventListener(k,v){this.handlers[k]=v},querySelector(){return node('button')}})}
            globalThis.document={hidden:false,activeElement:null,documentElement:node('html'),getElementById:node,querySelectorAll(){return []},querySelector(){return null},addEventListener(){}};
            globalThis.setInterval=(fn,ms)=>{globalThis.pollInterval=ms;return 1}; globalThis.clearInterval=()=>{};
            globalThis.confirm=()=>false;
            globalThis.fetch=(url,options)=>{sent.push({url,options});
                if(options.method==='PATCH'){const section=url.split('/').pop();Object.assign(model[section],JSON.parse(options.body))}
                const value=url.endsWith('/session')?{authenticated:false,setupRequired:false}:url.endsWith('/status')?metrics:url.endsWith('/logout')?{ok:true}:model;
                return Promise.resolve({ok:true,status:200,json:()=>Promise.resolve(value)});
            };
        )JS";
        const auto harnessResult = engine.evaluate("globalThis.model=" + model + ";globalThis.metrics=" + metrics + ";" + harness);
        QVERIFY2(!harnessResult.isError(), qPrintable(harnessResult.toString()));
        const auto run = engine.evaluate(script + "\nglobalThis.frontendHooks={signedIn,navigate,state:()=>({settings,busy,authenticated})};");
        QVERIFY2(!run.isError(), qPrintable(run.toString()));
        const auto login = engine.evaluate("frontendHooks.signedIn({csrf:'test-csrf'})");
        QVERIFY2(!login.isError(), qPrintable(login.toString()));
        QTRY_VERIFY(engine.globalObject().property("pollInterval").isNumber());
        QCOMPARE(engine.globalObject().property("pollInterval").toInt(), 8000);
        QVERIFY(!engine.evaluate("nodes.shell.hidden").toBool());
        engine.evaluate("navigate('clock');nodes.format24.value='true';nodes.seconds.checked=true;nodes.date.checked=true;nodes['date-format'].value='yyyy-MM-dd';nodes['clock-form'].handlers.submit({preventDefault(){}})");
        QTRY_COMPARE(engine.evaluate("frontendHooks.state().busy").toInt(), 0);
        QVERIFY(engine.evaluate("sent.some(r=>r.options.method==='PATCH' && r.url.endsWith('/clock') && JSON.parse(r.options.body).seconds===true)").toBool());
        QCOMPARE(engine.evaluate("nodes['page-title'].textContent").toString(), QString("Clock"));
        engine.evaluate("nodes.restart.handlers.click()");
        QVERIFY(!engine.evaluate("sent.some(r=>r.url.endsWith('/application/restart'))").toBool());

    }
    void sessionsAndRateLimit() {
        qint64 now = 100000; AdminAuth auth(temp.filePath("auth.ini"), [&] { return now; });
        auto token = auth.login("test password only 12345"); QVERIFY(!token.isEmpty()); QVERIFY(auth.session(token));
        now += 1200001; QVERIFY(!auth.session(token));
        token = auth.login("test password only 12345"); QVERIFY(auth.session(token)); auth.logout(token); QVERIFY(!auth.session(token));
        token = auth.login("test password only 12345");
        for (int i=0; i<24; ++i) { now += 1190000; QVERIFY(auth.session(token)); }
        now += 250000; QVERIFY(!auth.session(token));
        QVERIFY(!(QFileInfo(temp.filePath("auth.ini")).permissions() & (QFile::ReadGroup | QFile::ReadOther)));
        for (int i=0; i<5; ++i) QVERIFY(auth.rateAllowed("peer")); QVERIFY(!auth.rateAllowed("peer")); now += 60001; QVERIFY(auth.rateAllowed("peer"));
        QCOMPARE(request("/api/v1/logout", "POST", "{}").code, 200); QCOMPARE(request("/api/v1/settings").code, 401);
        QVERIFY(!AdminServer::privateAddress("0.0.0.0")); QVERIFY(!AdminServer::privateAddress("8.8.8.8")); QVERIFY(AdminServer::privateAddress("192.168.0.213"));
    }
    void cleanupTestCase() { server.reset(); settings.reset(); brightness.reset(); }
};
QTEST_GUILESS_MAIN(AdminTests)
#include "admin_tests.moc"
