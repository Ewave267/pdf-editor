// SPDX-License-Identifier: GPL-3.0-only
#include "AddedContent.h"
#include "pdf/PdfDocument.h"
#include <QBuffer>
#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
AddedContent::AddedContent(PdfDocument* document) : QObject(document), document_(document) {}
AddedContent::State AddedContent::state() const
{
    auto objects = objects_;
    // Mutators hold references into objects_; detach the snapshot rather than
    // letting a later reference write also mutate its shared list storage.
    objects.detach();
    return {objects, selected_, revision_, selection_};
}
void AddedContent::restore(const State& state)
{
    objects_ = state.objects;
    selected_ = state.selected;
    revision_ = state.revision;
    selection_ = state.selection;
    error_.clear();
    emit changed();
}
void AddedContent::beginEdit()
{
    if (editing_)
        return;
    editing_ = true;
    editRecorded_ = false;
    editState_ = state();
    editUndo_ = undo_;
    editRedo_ = redo_;
    editRects_.clear();
    for (const auto& object : objects_)
        if (isSelected(object.id))
            editRects_.insert(object.id, object.rect);
    emit changed();
}
void AddedContent::endEdit()
{
    if (!editing_)
        return;
    editing_ = false;
    editRecorded_ = false;
    editState_.reset();
    editUndo_.clear();
    editRedo_.clear();
    editRects_.clear();
    emit changed();
}
void AddedContent::cancelEdit()
{
    if (!editing_ || !editState_)
        return;
    const auto previous = *editState_;
    undo_ = editUndo_;
    redo_ = editRedo_;
    editing_ = editRecorded_ = false;
    editState_.reset();
    editUndo_.clear();
    editRedo_.clear();
    editRects_.clear();
    restore(previous);
}
void AddedContent::recordEdit()
{
    if (!editing_ || !editRecorded_)
    {
        undo_.append(state());
        redo_.clear();
        editRecorded_ = true;
        trimHistory();
    }
    // State IDs stay unique on new branches, but undo restores a previous ID
    // so returning to the saved state also clears the unsaved marker.
    revision_ = ++nextRevision_;
}
void AddedContent::trimHistory()
{
    const auto bytes = [this]()
    {
        qint64 total = 0;
        QSet<qint64> images;
        for (const auto* history : {&undo_, &redo_})
            for (const auto& state : *history)
                for (const auto& object : state.objects)
                {
                    total += sizeof(Object) + 2 * (object.text.size() + object.fontFamily.size()) +
                             object.points.size() * sizeof(QPointF);
                    if (!object.image.isNull() && !images.contains(object.image.cacheKey()))
                    {
                        images.insert(object.image.cacheKey());
                        total += object.image.sizeInBytes();
                    }
                }
        return total;
    };
    while (undo_.size() + redo_.size() > 100 || bytes() > 64 * 1024 * 1024)
    {
        if (!undo_.isEmpty())
            undo_.removeFirst();
        else if (!redo_.isEmpty())
            redo_.removeFirst();
        else
            break;
    }
}
void AddedContent::undo()
{
    if (!canUndo() || document_->saving())
        return;
    const auto previous = undo_.takeLast();
    redo_.append(state());
    trimHistory();
    restore(previous);
}
void AddedContent::redo()
{
    if (!canRedo() || document_->saving())
        return;
    const auto next = redo_.takeLast();
    undo_.append(state());
    trimHistory();
    restore(next);
}
QStringList AddedContent::fontFamilies() const
{
    auto families = QFontDatabase::families();
    families.prepend("Monospace");
    families.prepend("Serif");
    families.prepend("Sans Serif");
    families.removeDuplicates();
    return families;
}
QFont AddedContent::textFont(const Object& object)
{
    QString family = object.fontFamily;
    const QString preferred = family == "Sans Serif"  ? "Liberation Sans"
                              : family == "Serif"     ? "Liberation Serif"
                              : family == "Monospace" ? "Liberation Mono"
                                                      : QString();
    // Prefer the bundled static faces for generic families. Some platform
    // aliases resolve to a regular-only face and synthesize bold by painting
    // text twice, which duplicates extracted text in the exported PDF.
    if (!preferred.isEmpty() && QFontDatabase::families().contains(preferred))
        family = preferred;
    QFont font(family);
    // Both painters use page-point coordinates; the PDF writer runs at 72 DPI.
    font.setPixelSize(object.fontSize);
    font.setBold(object.bold);
    font.setItalic(object.italic);
    font.setUnderline(object.underline);
    return font;
}
void AddedContent::paintObject(QPainter* painter, const Object& object)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(object.rect);
    painter->setPen(
        QPen(object.color, object.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const auto inset = object.rect.adjusted(object.lineWidth / 2, object.lineWidth / 2,
                                            -object.lineWidth / 2, -object.lineWidth / 2);
    if (object.type == "text" || object.type == "stamp")
    {
        painter->setFont(textFont(object));
        painter->setPen(object.color);
        if (object.type == "stamp")
        {
            painter->setPen(QPen(object.color, object.lineWidth));
            painter->drawRoundedRect(inset, 3, 3);
            painter->drawText(object.rect.adjusted(6, 4, -6, -4),
                              Qt::AlignCenter | Qt::TextWordWrap, object.text);
        }
        else
            painter->drawText(object.rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                              object.text);
    }
    else if (object.type == "image" || object.type == "signature")
        painter->drawImage(object.rect, object.image);
    else if (object.type == "highlight")
    {
        auto color = object.color;
        color.setAlpha(80);
        painter->fillRect(object.rect, color);
    }
    else if (object.type == "rectangle")
        painter->drawRect(inset);
    else if (object.type == "ellipse")
        painter->drawEllipse(inset);
    else if (object.type == "checkmark")
    {
        QPainterPath path;
        path.moveTo(inset.x() + inset.width() * .12, inset.y() + inset.height() * .5);
        path.lineTo(inset.x() + inset.width() * .4, inset.y() + inset.height() * .8);
        path.lineTo(inset.x() + inset.width() * .9, inset.y() + inset.height() * .12);
        painter->drawPath(path);
    }
    else if (!object.points.isEmpty())
    {
        const auto point = [&object](const QPointF& p)
        {
            return QPointF(object.rect.x() + p.x() * object.rect.width(),
                           object.rect.y() + p.y() * object.rect.height());
        };
        if (object.points.size() == 1)
            painter->drawPoint(point(object.points.first()));
        else
        {
            QPainterPath path;
            path.moveTo(point(object.points.first()));
            for (int index = 1; index < object.points.size(); ++index)
                path.lineTo(point(object.points[index]));
            painter->drawPath(path);
        }
    }
    painter->restore();
}
int AddedContent::addGraphic(int page, double x, double y, const QString& type,
                             const QString& color, double lineWidth, const QString& text)
{
    const QStringList types{"checkmark", "rectangle", "ellipse", "line",
                            "highlight", "stamp",     "drawing"};
    if (!types.contains(type) || !QColor(color).isValid() || !std::isfinite(lineWidth) ||
        lineWidth < 1 || lineWidth > 12 || text.size() > 10000 ||
        (type == "stamp" && text.trimmed().isEmpty()))
        return -1;
    QSizeF size(120, 70);
    if (type == "checkmark")
        size = {32, 32};
    else if (type == "highlight")
        size = {160, 24};
    else if (type == "stamp")
        size = {180, 60};
    else if (type == "line")
        size = {120, 30};
    else if (type == "drawing")
        size = {12, 12};
    const auto rect = bounded(page, {x - (type == "drawing" ? 6 : 0),
                                     y - (type == "drawing" ? 6 : 0), size.width(), size.height()});
    if (rect.isEmpty())
        return -1;
    recordEdit();
    Object object{++nextId_, page, type, text, rect, {}};
    object.color = QColor(color);
    object.color.setAlpha(255);
    object.lineWidth = lineWidth;
    object.bold = type == "stamp";
    if (type == "drawing")
        object.points = {{std::clamp((x - rect.x()) / rect.width(), 0., 1.),
                          std::clamp((y - rect.y()) / rect.height(), 0., 1.)}};
    else if (type == "line")
        object.points = {{.03, .1}, {.97, .9}};
    objects_.append(object);
    selected_ = object.id;
    selection_ = {selected_};
    error_.clear();
    emit changed();
    return selected_;
}
bool AddedContent::setAppearance(int id, const QString& color, double lineWidth)
{
    QColor chosen(color);
    if (!chosen.isValid() || !std::isfinite(lineWidth) || lineWidth < 1 || lineWidth > 12)
        return false;
    chosen.setAlpha(255);
    for (auto& object : objects_)
        if (object.id == id && object.type != "image" && object.type != "signature")
        {
            if (object.color == chosen && object.lineWidth == lineWidth)
                return true;
            recordEdit();
            object.color = chosen;
            object.lineWidth = lineWidth;
            emit changed();
            return true;
        }
    return false;
}
bool AddedContent::replacePath(int id, const QList<QPointF>& points)
{
    if (points.isEmpty() || points.size() > 4096)
        return false;
    for (auto& object : objects_)
        if (object.id == id && (object.type == "drawing" || object.type == "line"))
        {
            const auto page = document_->pages()[object.page].toMap();
            double left = page["width"].toDouble(), top = page["height"].toDouble(), right = 0,
                   bottom = 0;
            QList<QPointF> clamped;
            for (const auto& point : points)
            {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                    return false;
                const QPointF p(std::clamp(point.x(), 0., page["width"].toDouble()),
                                std::clamp(point.y(), 0., page["height"].toDouble()));
                left = std::min(left, p.x());
                top = std::min(top, p.y());
                right = std::max(right, p.x());
                bottom = std::max(bottom, p.y());
                clamped.append(p);
            }
            const double padding = object.lineWidth / 2 + 1;
            const auto rect = bounded(object.page, {left - padding, top - padding,
                                                    std::max(12., right - left + 2 * padding),
                                                    std::max(12., bottom - top + 2 * padding)});
            if (rect.isEmpty())
                return false;
            QList<QPointF> normalized;
            for (const auto& p : clamped)
                normalized.append(
                    {(p.x() - rect.x()) / rect.width(), (p.y() - rect.y()) / rect.height()});
            if (object.rect == rect && object.points == normalized)
                return true;
            recordEdit();
            object.rect = rect;
            object.points = normalized;
            emit changed();
            return true;
        }
    return false;
}
bool AddedContent::appendStroke(int id, double x, double y)
{
    if (!document_->ready())
        return false;
    for (const auto& object : objects_)
        if (object.id == id && object.type == "drawing")
        {
            QList<QPointF> points;
            for (const auto& p : object.points)
                points.append({object.rect.x() + p.x() * object.rect.width(),
                               object.rect.y() + p.y() * object.rect.height()});
            if (!points.isEmpty() && QLineF(points.last(), {x, y}).length() < .5)
                return true;
            points.append({x, y});
            return replacePath(id, points);
        }
    return false;
}
bool AddedContent::setLine(int id, double x0, double y0, double x1, double y1)
{
    if (!document_->ready() || object(id)["type"].toString() != "line")
        return false;
    return replacePath(id, {{x0, y0}, {x1, y1}});
}
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
    recordEdit();
    objects_.append({++nextId_, page, type, text, rect, image});
    selected_ = nextId_;
    selection_ = {selected_};
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
                    {"fontFamily", o.fontFamily},
                    {"fontSize", o.fontSize},
                    {"bold", o.bold},
                    {"italic", o.italic},
                    {"underline", o.underline},
                    {"color", o.color.name(QColor::HexArgb)},
                    {"lineWidth", o.lineWidth},
                    {"pointCount", o.points.size()},
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
            if (o.rect == r)
                return true;
            recordEdit();
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
        if (o.id == id && (o.type == "text" || o.type == "stamp"))
        {
            if (o.text == text)
                return true;
            recordEdit();
            o.text = text;
            emit changed();
            return true;
        }
    return false;
}
bool AddedContent::setTextStyle(int id, const QString& family, int size, bool bold, bool italic,
                                bool underline)
{
    if (size < 6 || size > 144 || !fontFamilies().contains(family))
        return false;
    for (auto& object : objects_)
        if (object.id == id && (object.type == "text" || object.type == "stamp"))
        {
            if (object.fontFamily == family && object.fontSize == size && object.bold == bold &&
                object.italic == italic && object.underline == underline)
                return true;
            recordEdit();
            object.fontFamily = family;
            object.fontSize = size;
            object.bold = bold;
            object.italic = italic;
            object.underline = underline;
            emit changed();
            return true;
        }
    return false;
}
void AddedContent::select(int id)
{
    selected_ = object(id).isEmpty() ? -1 : id;
    selection_ = selected_ < 0 ? QSet<int>{} : QSet<int>{selected_};
    emit changed();
}
void AddedContent::focusObject(int id)
{
    if (!isSelected(id))
    {
        select(id);
        return;
    }
    selected_ = id;
    emit changed();
}
void AddedContent::toggleSelection(int id)
{
    const auto target = object(id);
    if (target.isEmpty())
        return;
    if (selected_ >= 0 && object(selected_)["page"] != target["page"])
        selection_.clear();
    if (isSelected(id))
        selection_.remove(id);
    else
        selection_.insert(id);
    selected_ = -1;
    for (const auto& o : objects_)
        if (isSelected(o.id))
            selected_ = o.id;
    emit changed();
}
void AddedContent::selectAll(int page)
{
    selection_.clear();
    selected_ = -1;
    for (const auto& object : objects_)
        if (object.page == page)
        {
            selection_.insert(object.id);
            selected_ = object.id;
        }
    emit changed();
}
void AddedContent::selectRect(int page, double x, double y, double width, double height,
                              bool additive)
{
    if (!document_->ready() || page < 0 || page >= document_->pageCount() || !std::isfinite(x) ||
        !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
        height <= 0)
        return;
    if (!additive || (selected_ >= 0 && object(selected_)["page"].toInt() != page))
    {
        selection_.clear();
        selected_ = -1;
    }
    const QRectF rect(x, y, width, height);
    for (const auto& object : objects_)
        if (object.page == page && rect.intersects(object.rect))
        {
            selection_.insert(object.id);
            selected_ = object.id;
        }
    emit changed();
}
bool AddedContent::moveSelection(double dx, double dy)
{
    if (!document_->ready() || selection_.isEmpty() || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    QRectF bounds;
    QHash<int, QRectF> origins;
    int page = -1;
    for (const auto& object : objects_)
        if (isSelected(object.id))
        {
            if (page >= 0 && page != object.page)
                return false;
            page = object.page;
            const auto rect = editing_ ? editRects_.value(object.id, object.rect) : object.rect;
            origins.insert(object.id, rect);
            bounds = bounds.isNull() ? rect : bounds.united(rect);
        }
    if (page < 0)
        return false;
    const auto dimensions = document_->pages()[page].toMap();
    dx = std::clamp(dx, -bounds.left(), dimensions["width"].toDouble() - bounds.right());
    dy = std::clamp(dy, -bounds.top(), dimensions["height"].toDouble() - bounds.bottom());
    bool changed = false;
    for (const auto& object : objects_)
        if (isSelected(object.id) && object.rect != origins[object.id].translated(dx, dy))
            changed = true;
    if (!changed)
        return true;
    recordEdit();
    for (auto& object : objects_)
        if (isSelected(object.id))
            object.rect = origins[object.id].translated(dx, dy);
    emit this->changed();
    return true;
}
bool AddedContent::nudgeSelection(double dx, double dy)
{
    if (editing_ || document_->saving())
        return false;
    beginEdit();
    const bool result = moveSelection(dx, dy);
    endEdit();
    return result;
}
bool AddedContent::alignSelection(const QString& alignment)
{
    const QStringList allowed{"left", "center", "right", "top", "middle", "bottom"};
    if (!document_->ready() || document_->saving() || editing_ || selectionCount() < 2 ||
        !allowed.contains(alignment))
        return false;
    QRectF bounds;
    for (const auto& object : objects_)
        if (isSelected(object.id))
            bounds = bounds.isNull() ? object.rect : bounds.united(object.rect);
    QHash<int, QRectF> targets;
    bool changed = false;
    for (const auto& object : objects_)
        if (isSelected(object.id))
        {
            auto rect = object.rect;
            if (alignment == "left")
                rect.moveLeft(bounds.left());
            else if (alignment == "center")
                rect.moveLeft(bounds.center().x() - rect.width() / 2);
            else if (alignment == "right")
                rect.moveRight(bounds.right());
            else if (alignment == "top")
                rect.moveTop(bounds.top());
            else if (alignment == "middle")
                rect.moveTop(bounds.center().y() - rect.height() / 2);
            else
                rect.moveBottom(bounds.bottom());
            targets.insert(object.id, rect);
            changed |= rect != object.rect;
        }
    if (!changed)
        return true;
    recordEdit();
    for (auto& object : objects_)
        if (isSelected(object.id))
            object.rect = targets[object.id];
    emit this->changed();
    return true;
}
bool AddedContent::copySelection()
{
    if (selection_.isEmpty() || editing_)
        return false;
    if (selectionCount() > 100)
    {
        error_ = "Copy at most 100 objects at a time.";
        emit changed();
        return false;
    }
    QJsonArray copied;
    QStringList text;
    qint64 estimatedBytes = 0;
    for (const auto& object : objects_)
        if (isSelected(object.id))
        {
            auto value = QJsonObject::fromVariantMap(this->object(object.id));
            estimatedBytes += 2048 + object.text.size() * 6 + object.points.size() * 64;
            QJsonArray points;
            for (const auto& p : object.points)
                points.append(QJsonArray{p.x(), p.y()});
            value.insert("points", points);
            if (!object.image.isNull())
            {
                QByteArray image;
                QBuffer buffer(&image);
                buffer.open(QIODevice::WriteOnly);
                if (!object.image.save(&buffer, "PNG"))
                    return false;
                estimatedBytes += ((image.size() + 2) / 3) * 4;
                if (estimatedBytes > 64 * 1024 * 1024)
                {
                    error_ = "The selection is too large for the clipboard.";
                    emit changed();
                    return false;
                }
                value.insert("image", QString::fromLatin1(image.toBase64()));
            }
            copied.append(value);
            if (estimatedBytes > 64 * 1024 * 1024)
            {
                error_ = "The selection is too large for the clipboard.";
                emit changed();
                return false;
            }
            if (!object.text.isEmpty())
                text.append(object.text);
        }
    const auto payload = QJsonDocument(QJsonObject{{"version", 1}, {"objects", copied}})
                             .toJson(QJsonDocument::Compact);
    if (payload.size() > 64 * 1024 * 1024)
    {
        error_ = "The selection is too large for the clipboard.";
        emit changed();
        return false;
    }
    auto* mime = new QMimeData;
    mime->setData("application/vnd.pdf-editor.additions+json", payload);
    if (!text.isEmpty())
        mime->setText(text.join('\n'));
    if (selectionCount() == 1)
        for (const auto& object : objects_)
            if (isSelected(object.id) && !object.image.isNull())
                mime->setImageData(object.image);
    QGuiApplication::clipboard()->setMimeData(mime);
    return true;
}
bool AddedContent::cutSelection()
{
    if (document_->saving() || !copySelection())
        return false;
    removeSelected();
    return true;
}
bool AddedContent::paste(int page)
{
    if (!document_->ready() || document_->saving() || editing_ || page < 0 ||
        page >= document_->pageCount())
        return false;
    const auto reject = [this]()
    {
        error_ = "Clipboard content is invalid, too large, or does not fit this page.";
        emit changed();
        return false;
    };
    const auto* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return reject();
    if (!mime->hasFormat("application/vnd.pdf-editor.additions+json"))
    {
        if (mime->hasImage())
        {
            const auto image = QGuiApplication::clipboard()->image();
            if (image.isNull() || qint64(image.width()) * image.height() > 16000000)
                return reject();
            return insert(page, 40, 40, "image", {}, image) > 0;
        }
        if (mime->hasText())
            return addText(page, 40, 40, mime->text()) > 0 ? true : reject();
        return reject();
    }
    const auto payload = mime->data("application/vnd.pdf-editor.additions+json");
    return pastePayload(payload, page, false);
}
bool AddedContent::restoreCheckpoint(const QByteArray& payload)
{
    if (!document_->ready() || editing_)
        return false;
    return pastePayload(payload, 0, true);
}
bool AddedContent::pastePayload(const QByteArray& payload, int page, bool recovery)
{
    auto reject = [this]
    {
        error_ = "Invalid saved additions.";
        emit changed();
        return false;
    };
    if (payload.size() > 64 * 1024 * 1024)
        return reject();
    const auto parsed = QJsonDocument::fromJson(payload).object();
    const auto values = parsed["objects"].toArray();
    if (parsed["version"].toInt() != 1 || (!recovery && values.isEmpty()) ||
        values.size() > (recovery ? 1000 : 100))
        return reject();
    QList<Object> copied;
    QRectF bounds;
    qint64 imageBytes = 0;
    const double invalid = std::numeric_limits<double>::quiet_NaN();
    const auto families = fontFamilies();
    const QStringList types{"text",    "image", "signature", "checkmark", "rectangle",
                            "ellipse", "line",  "highlight", "stamp",     "drawing"};
    for (const auto& value : values)
    {
        const auto json = value.toObject();
        Object object{};
        object.page = recovery ? json["page"].toInt(-1) : page;
        if (object.page < 0 || object.page >= document_->pageCount())
            return reject();
        object.type = json["type"].toString();
        object.text = json["text"].toString();
        object.rect = {json["x"].toDouble(invalid), json["y"].toDouble(invalid),
                       json["width"].toDouble(invalid), json["height"].toDouble(invalid)};
        if (!types.contains(object.type) || object.text.size() > 10000 ||
            !std::isfinite(object.rect.x()) || !std::isfinite(object.rect.y()) ||
            !std::isfinite(object.rect.width()) || !std::isfinite(object.rect.height()) ||
            object.rect.x() < 0 || object.rect.y() < 0 || object.rect.width() < 1 ||
            object.rect.height() < 1 || object.rect.right() > 14400 || object.rect.bottom() > 14400)
            return reject();
        object.fontFamily = json["fontFamily"].toString("Sans Serif");
        if (!families.contains(object.fontFamily))
            object.fontFamily = "Sans Serif";
        object.fontSize = json["fontSize"].toInt();
        object.bold = json["bold"].toBool();
        object.italic = json["italic"].toBool();
        object.underline = json["underline"].toBool();
        object.color = QColor(json["color"].toString());
        object.lineWidth = json["lineWidth"].toDouble(invalid);
        if (object.fontSize < 6 || object.fontSize > 144 || !object.color.isValid() ||
            !std::isfinite(object.lineWidth) || object.lineWidth < 1 || object.lineWidth > 12 ||
            ((object.type == "text" || object.type == "stamp") && object.text.trimmed().isEmpty()))
            return reject();
        object.color.setAlpha(255);
        const auto points = json["points"].toArray();
        if (points.size() > 4096 || (object.type == "drawing" && points.isEmpty()) ||
            (object.type == "line" && points.size() != 2))
            return reject();
        for (const auto& point : points)
        {
            const auto p = point.toArray();
            const double x = p.size() == 2 ? p[0].toDouble(invalid) : invalid;
            const double y = p.size() == 2 ? p[1].toDouble(invalid) : invalid;
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
                return reject();
            object.points.append({x, y});
        }
        if (object.type == "image" || object.type == "signature")
        {
            const auto bytes = QByteArray::fromBase64(json["image"].toString().toLatin1());
            QBuffer buffer;
            buffer.setData(bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG");
            const auto size = reader.size();
            const qint64 pixels = qint64(size.width()) * size.height();
            if (!size.isValid() || pixels > 16000000 || imageBytes + pixels * 4 > 64 * 1024 * 1024)
                return reject();
            object.image = reader.read();
            if (object.image.isNull())
                return reject();
            imageBytes += object.image.sizeInBytes();
        }
        if (recovery)
        {
            const auto dimensions = document_->pages()[object.page].toMap();
            if (object.rect.right() > dimensions["width"].toDouble() ||
                object.rect.bottom() > dimensions["height"].toDouble())
                return reject();
        }
        bounds = bounds.isNull() ? object.rect : bounds.united(object.rect);
        copied.append(object);
    }
    const auto dimensions = document_->pages()[page].toMap();
    const double width = dimensions["width"].toDouble(), height = dimensions["height"].toDouble();
    if (!recovery && (bounds.width() > width || bounds.height() > height))
        return reject();
    QPointF offset;
    if (!recovery)
        offset = QPointF(std::clamp(bounds.x() + 20, 0., width - bounds.width()) - bounds.x(),
                         std::clamp(bounds.y() + 20, 0., height - bounds.height()) - bounds.y());
    recordEdit();
    if (recovery)
        objects_.clear();
    selection_.clear();
    for (auto& object : copied)
    {
        object.id = ++nextId_;
        if (!recovery)
        {
            object.page = page;
            object.rect.translate(offset);
        }
        selection_.insert(object.id);
        selected_ = object.id;
        objects_.append(object);
    }
    error_.clear();
    emit changed();
    return true;
}
QByteArray AddedContent::checkpoint() const
{
    if (objects_.size() > 1000)
        return {};
    QJsonArray values;
    qsizetype estimated = 0;
    for (const auto& o : objects_)
    {
        QJsonObject value = QJsonObject::fromVariantMap(object(o.id));
        value["page"] = o.page;
        QJsonArray points;
        for (const auto& p : o.points)
            points.append(QJsonArray{p.x(), p.y()});
        value["points"] = points;
        if (!o.image.isNull())
        {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (!o.image.save(&buffer, "PNG"))
                return {};
            value["image"] = QString::fromLatin1(png.toBase64());
        }
        estimated += QJsonDocument(value).toJson(QJsonDocument::Compact).size();
        if (estimated > 64 * 1024 * 1024)
            return {};
        values.append(value);
    }
    return QJsonDocument(QJsonObject{{"version", 1}, {"objects", values}})
        .toJson(QJsonDocument::Compact);
}

void AddedContent::removeSelected()
{
    if (!selection_.isEmpty())
        recordEdit();
    objects_.removeIf([this](const Object& o) { return isSelected(o.id); });
    selected_ = -1;
    selection_.clear();
    emit changed();
}
void AddedContent::clear()
{
    revision_ = ++nextRevision_;
    undo_.clear();
    redo_.clear();
    editing_ = editRecorded_ = false;
    editState_.reset();
    editUndo_.clear();
    editRedo_.clear();
    editRects_.clear();
    selection_.clear();
    objects_.clear();
    selected_ = -1;
    error_.clear();
    emit changed();
}
