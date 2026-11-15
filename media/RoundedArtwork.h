#pragma once
#include "MediaReceiver.h"
#include <QPainter>
#include <QPainterPath>
#include <QQuickPaintedItem>
class RoundedArtwork : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(
        MediaReceiver *controller READ controller WRITE setController NOTIFY controllerChanged)
  public:
    explicit RoundedArtwork(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {
        setAntialiasing(true);
    }
    MediaReceiver *controller() const { return m_receiver; }
    void setController(MediaReceiver *r) {
        if (m_receiver == r)
            return;
        if (m_receiver)
            disconnect(m_receiver, nullptr, this, nullptr);
        m_receiver = r;
        if (r)
            connect(r, &MediaReceiver::changed, this, &RoundedArtwork::refresh);
        refresh();
        emit controllerChanged();
    }
    void paint(QPainter *p) override {
        p->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(boundingRect(), 22, 22);
        p->setClipPath(path);
        p->fillRect(boundingRect(), QColor("#2e3544"));
        if (!m_image.isNull()) {
            p->setRenderHint(QPainter::SmoothPixmapTransform);
            auto size = m_image.size().scaled(boundingRect().size().toSize(),
                                              Qt::KeepAspectRatioByExpanding);
            QRectF target((width() - size.width()) / 2., (height() - size.height()) / 2.,
                          size.width(), size.height());
            p->drawImage(target, m_image);
        } else {
            p->setPen(QColor("#b5becb"));
            auto f = p->font();
            f.setPixelSize(100);
            p->setFont(f);
            p->drawText(boundingRect(), Qt::AlignCenter, QString::fromUtf8("♫"));
        }
    }
  signals:
    void controllerChanged();

  private:
    MediaReceiver *m_receiver = nullptr;
    QImage m_image;
    QString m_id;
    void refresh() {
        auto id = m_receiver && m_receiver->options().value("showArtwork").toBool()
                      ? m_receiver->artwork()
                      : QString();
        if (id == m_id)
            return;
        m_id = id;
        m_image = id.isEmpty() ? QImage() : m_receiver->image();
        update();
    }
};
