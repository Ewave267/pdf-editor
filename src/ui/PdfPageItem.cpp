// SPDX-License-Identifier: GPL-3.0-only
#include "PdfPageItem.h"
#include <QPainter>
#include <QQuickWindow>
#include <algorithm>

PdfPageItem::PdfPageItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    debounce_.setSingleShot(true);
    debounce_.setInterval(50);
    connect(&debounce_, &QTimer::timeout, this, &PdfPageItem::request);
    connect(this, &QQuickItem::windowChanged, this, [this] { schedule(); });
}
PdfPageItem::~PdfPageItem()
{
    if (document_)
        document_->cancelRender(request_);
}
void PdfPageItem::setDocument(PdfDocument* document)
{
    if (document_ == document)
        return;
    if (document_)
    {
        document_->cancelRender(request_);
        disconnect(document_, nullptr, this, nullptr);
    }
    document_ = document;
    if (document_)
    {
        connect(document_, &PdfDocument::stateChanged, this,
                [this]
                {
                    image_ = {};
                    emit renderedChanged();
                    schedule();
                });
        connect(document_, &PdfDocument::formRepaint, this, &PdfPageItem::schedule);
        connect(document_, &PdfDocument::rendered, this,
                [this](quint64 id, const QImage& image)
                {
                    if (id != request_)
                        return;
                    image_ = image;
                    emit renderedChanged();
                    update();
                });
    }
    image_ = {};
    emit documentChanged();
    emit renderedChanged();
    schedule();
}
void PdfPageItem::setPage(int page)
{
    if (page_ == page)
        return;
    page_ = page;
    image_ = {};
    emit pageChanged();
    emit renderedChanged();
    schedule();
}
void PdfPageItem::geometryChange(const QRectF& next, const QRectF& previous)
{
    QQuickPaintedItem::geometryChange(next, previous);
    if (next.size() != previous.size())
        schedule();
}
void PdfPageItem::schedule()
{
    if (document_)
        document_->cancelRender(request_);
    request_ = 0;
    update();
    debounce_.start();
}
void PdfPageItem::request()
{
    if (!document_ || !document_->ready() || width() <= 0 || height() <= 0)
        return;
    const double scale = window() ? window()->devicePixelRatio() : 1;
    request_ =
        document_->requestRender(page_, std::clamp(static_cast<int>(width() * scale), 96, 2400));
}
void PdfPageItem::paint(QPainter* painter)
{
    painter->fillRect(boundingRect(), Qt::white);
    if (!image_.isNull())
    {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(boundingRect(), image_);
    }
    else
    {
        painter->setPen(QColor("#69717c"));
        painter->drawText(boundingRect(), Qt::AlignCenter, "Rendering…");
    }
}
