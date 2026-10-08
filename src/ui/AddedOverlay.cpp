// SPDX-License-Identifier: GPL-3.0-only
#include "AddedOverlay.h"
#include "pdf/PdfDocument.h"
#include <QPainter>
void AddedOverlay::paint(QPainter* p)
{
    if (width() <= 0 || height() <= 0 || !content_ || page_ < 0 ||
        page_ >= content_->document()->pageCount())
        return;
    const auto page = content_->document()->pages()[page_].toMap();
    const double scale = width() / page["width"].toDouble();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->scale(scale, scale);
    for (const auto& o : content_->objects())
        if (o.page == page_)
        {
            AddedContent::paintObject(p, o);
            if (selection_ && content_->isSelected(o.id))
            {
                p->setPen(QPen(QColor("#225a91"), 1.5 / scale));
                p->setBrush(Qt::NoBrush);
                p->drawRect(o.rect);
                if (content_->selectionCount() == 1)
                    for (double x : {o.rect.left(), o.rect.center().x(), o.rect.right()})
                        for (double y : {o.rect.top(), o.rect.center().y(), o.rect.bottom()})
                            if (x != o.rect.center().x() || y != o.rect.center().y())
                                p->fillRect(
                                    QRectF(x - 4 / scale, y - 4 / scale, 8 / scale, 8 / scale),
                                    QColor("#225a91"));
            }
        }
}
