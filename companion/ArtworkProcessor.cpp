#include "ArtworkProcessor.h"
#include "../media/Protocol.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
QJsonObject ArtworkProcessor::palette(const QImage &image) {
    auto tiny = image.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                    .convertToFormat(QImage::Format_ARGB32);
    struct Bin {
        int n = 0, r = 0, g = 0, b = 0;
    };
    Bin bins[64];
    for (int y = 0; y < tiny.height(); ++y)
        for (int x = 0; x < tiny.width(); ++x) {
            auto c = tiny.pixelColor(x, y);
            if (c.alpha() < 128)
                continue;
            auto &b = bins[(c.red() / 64) * 16 + (c.green() / 64) * 4 + c.blue() / 64];
            ++b.n;
            b.r += c.red();
            b.g += c.green();
            b.b += c.blue();
        }
    QList<QColor> colors;
    QList<int> keys;
    for (int i = 0; i < 64; ++i)
        keys << i;
    std::sort(keys.begin(), keys.end(), [&](int a, int b) { return bins[a].n > bins[b].n; });
    for (int i : keys) {
        auto b = bins[i];
        if (!b.n)
            continue;
        QColor c(b.r / b.n, b.g / b.n, b.b / b.n);
        bool distinct = true;
        for (auto d : colors)
            if (qAbs(c.red() - d.red()) + qAbs(c.green() - d.green()) + qAbs(c.blue() - d.blue()) <
                90)
                distinct = false;
        if (distinct)
            colors << c;
        if (colors.size() == 3)
            break;
    }
    while (colors.size() < 3)
        colors << QColor(colors.isEmpty() ? "#354462" : colors.size() == 1 ? "#684459" : "#aa8062");
    // Darken the background colors to guarantee readable white controls.
    for (int i = 0; i < 2; ++i) {
        auto c = colors[i];
        int max = qMax(c.red(), qMax(c.green(), c.blue()));
        if (max > 100)
            colors[i] = QColor(c.red() * 100 / max, c.green() * 100 / max, c.blue() * 100 / max);
    }
    return {{"primary", colors[0].name()},
            {"secondary", colors[1].name()},
            {"accent", colors[2].name()}};
}
Artwork ArtworkProcessor::process(QByteArray bytes) {
    Artwork a;
    if (bytes.size() > 4 * 1024 * 1024)
        return a;
    QBuffer in(&bytes);
    in.open(QIODevice::ReadOnly);
    QImageReader r(&in);
    auto format = r.format();
    if (format != "jpeg" && format != "png" && format != "webp")
        return a;
    auto size = r.size();
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192 ||
        qint64(size.width()) * size.height() > 16000000)
        return a;
    r.setScaledSize(size.scaled(400, 400, Qt::KeepAspectRatio));
    auto image = r.read();
    if (image.isNull())
        return a;
    image = image.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    a.palette = palette(image);
    QBuffer out(&a.jpeg);
    out.open(QIODevice::WriteOnly);
    image.save(&out, "JPEG", 82);
    if (a.jpeg.size() > MediaProtocol::ArtworkLimit) {
        a.jpeg.clear();
        return a;
    }
    a.id =
        QString::fromLatin1(QCryptographicHash::hash(a.jpeg, QCryptographicHash::Sha256).toHex());
    return a;
}
ArtworkProcessor::ArtworkProcessor(QObject *p) : QObject(p) {
    QImageReader::setAllocationLimit(64);
    connect(&m_worker, &QFutureWatcher<Artwork>::finished, this, [this] {
        auto art = m_worker.result();
        if (m_processing == m_wanted) {
            m_cachedUri = m_processing;
            m_cached = art;
            emit ready(m_processing, art);
        }
        m_processing.clear();
        if (m_cachedUri != m_wanted)
            begin();
    });
}
void ArtworkProcessor::request(const QString &uri) {
    if (uri == m_wanted)
        return;
    m_wanted = uri;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (!m_worker.isRunning())
        begin();
}
void ArtworkProcessor::work(QByteArray data) {
    m_processing = m_wanted;
    m_worker.setFuture(QtConcurrent::run(&ArtworkProcessor::process, std::move(data)));
}
void ArtworkProcessor::begin() {
    if (m_wanted == m_cachedUri) {
        emit ready(m_wanted, m_cached);
        return;
    }
    QUrl url(m_wanted);
    if (m_wanted.isEmpty()) {
        m_cachedUri = m_wanted;
        m_cached = {};
        emit ready(m_wanted, {});
        return;
    }
    if (url.scheme() == "file" && url.host().isEmpty()) {
        // Opening and decoding local files also happens off the D-Bus event thread.
        m_processing = m_wanted;
        auto file = url.toLocalFile();
        m_worker.setFuture(QtConcurrent::run([file] {
            if (!QFileInfo(file).isFile())
                return Artwork();
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly) || f.isSequential() || f.size() > 4 * 1024 * 1024)
                return Artwork();
            return process(f.read(4 * 1024 * 1024 + 1));
        }));
        return;
    }
    // HTTPS only: redirects deliberately disabled, including HTTPS-to-HTTP downgrade.
    if (m_wanted.size() > 4096 || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty()) {
        m_cachedUri = m_wanted;
        m_cached = {};
        emit ready(m_wanted, {});
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(5000);
    m_download.clear();
    m_reply = m_network.get(req);
    m_reply->setReadBufferSize(65536);
    auto *reply = m_reply;
    QTimer::singleShot(5000, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_download += reply->readAll();
        if (m_download.size() > 4 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_download += reply->readAll();
        auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = reply->error() == QNetworkReply::NoError && status == 200 &&
                  m_download.size() <= 4 * 1024 * 1024;
        m_reply = nullptr;
        reply->deleteLater();
        if (ok)
            work(std::move(m_download));
        else {
            m_cachedUri = m_wanted;
            m_cached = {};
            emit ready(m_wanted, {});
        }
        m_download.clear();
    });
}
