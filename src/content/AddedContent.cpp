// SPDX-License-Identifier: GPL-3.0-only
#include "AddedContent.h"
#include "pdf/PdfDocument.h"
#include <QImageReader>
#include <algorithm>
#include <cmath>
AddedContent::AddedContent(PdfDocument* document) : QObject(document), document_(document) {}
QRectF AddedContent::bounded(int page, const QRectF& r) const
{
    if (!document_->ready() || page < 0 || page >= document_->pageCount() ||
        !std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.width()) ||
        !std::isfinite(r.height()) || r.width() <= 0 || r.height() <= 0)
        return {};
    const auto p = document_->pages()[page].toMap();
    const double pw = p["width"].toDouble(), ph = p["height"].toDouble();
    const double w = std::clamp(r.width(), std::min(12.0, pw), pw);
    const double h = std::clamp(r.height(), std::min(12.0, ph), ph);
    return {std::clamp(r.x(), 0.0, pw - w), std::clamp(r.y(), 0.0, ph - h), w, h};
}
int AddedContent::insert(int page, double x, double y, const QString& type, const QString& text,
                         const QImage& image)
{
    const double w = image.isNull() ? 220 : 160;
    const double h = image.isNull() ? 72 : w * image.height() / image.width();
    const QRectF rect = bounded(page, {x, y, w, h});
    if (rect.isEmpty())
        return -1;
    ++revision_;
    objects_.append({++nextId_, page, type, text, rect, image});
    selected_ = nextId_;
    error_.clear();
    emit changed();
    return selected_;
}
int AddedContent::addText(int page, double x, double y, const QString& text)
{
    if (text.trimmed().isEmpty() || text.size() > 10000)
        return -1;
    return insert(page, x, y, "text", text, {});
}
int AddedContent::addImage(int page, double x, double y, const QUrl& file, bool signature)
{
    if (!file.isLocalFile())
    {
        error_ = "Choose an image on this computer.";
        emit changed();
        return -1;
    }
    QImageReader reader(file.toLocalFile());
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 16000000)
    {
        error_ = "Choose a local image with at most 16 million pixels.";
        emit changed();
        return -1;
    }
    const QImage image = reader.read();
    if (image.isNull())
    {
        error_ = "The image could not be read.";
        emit changed();
        return -1;
    }
    return insert(page, x, y, signature ? "signature" : "image", {}, image);
}
QVariantMap AddedContent::object(int id) const
{
    for (const auto& o : objects_)
        if (o.id == id)
            return {{"id", o.id},
                    {"page", o.page},
                    {"type", o.type},
                    {"text", o.text},
                    {"x", o.rect.x()},
                    {"y", o.rect.y()},
                    {"width", o.rect.width()},
                    {"height", o.rect.height()}};
    return {};
}
int AddedContent::hit(int page, double x, double y) const
{
    for (auto it = objects_.crbegin(); it != objects_.crend(); ++it)
        if (it->page == page && it->rect.contains({x, y}))
            return it->id;
    return -1;
}
bool AddedContent::geometry(int id, double x, double y, double w, double h)
{
    for (auto& o : objects_)
        if (o.id == id)
        {
            const auto r = bounded(o.page, {x, y, w, h});
            if (r.isEmpty())
                return false;
            ++revision_;
            o.rect = r;
            emit changed();
            return true;
        }
    return false;
}
bool AddedContent::setText(int id, const QString& text)
{
    if (text.trimmed().isEmpty() || text.size() > 10000)
        return false;
    for (auto& o : objects_)
        if (o.id == id && o.type == "text")
        {
            ++revision_;
            o.text = text;
            emit changed();
            return true;
        }
    return false;
}
void AddedContent::select(int id)
{
    selected_ = object(id).isEmpty() ? -1 : id;
    emit changed();
}
void AddedContent::removeSelected()
{
    if (selected_ >= 0)
        ++revision_;
    objects_.removeIf([this](const Object& o) { return o.id == selected_; });
    selected_ = -1;
    emit changed();
}
void AddedContent::clear()
{
    ++revision_;
    objects_.clear();
    selected_ = -1;
    error_.clear();
    emit changed();
}
