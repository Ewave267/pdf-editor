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
            p->save();
            p->setClipRect(o.rect);
            if (o.type == "text")
            {
                p->setFont(AddedContent::textFont(o));
                p->setPen(Qt::black);
                p->drawText(o.rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, o.text);
            }
            else
                p->drawImage(o.rect, o.image);
            p->restore();
            if (selection_ && o.id == content_->selected())
            {
                p->setPen(QPen(QColor("#225a91"), 1.5 / scale));
                p->setBrush(Qt::NoBrush);
                p->drawRect(o.rect);
                p->fillRect(QRectF(o.rect.right() - 5 / scale, o.rect.bottom() - 5 / scale,
                                   10 / scale, 10 / scale),
                            QColor("#225a91"));
            }
        }
}
