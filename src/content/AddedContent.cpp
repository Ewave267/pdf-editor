// SPDX-License-Identifier: GPL-3.0-only
#include "AddedContent.h"
#include "pdf/PdfDocument.h"
#include <QFontDatabase>
#include <QImageReader>
#include <QSet>
#include <algorithm>
#include <cmath>
AddedContent::AddedContent(PdfDocument* document) : QObject(document), document_(document) {}
AddedContent::State AddedContent::state() const
{
    auto objects = objects_;
    // Mutators hold references into objects_; detach the snapshot rather than
    // letting a later reference write also mutate its shared list storage.
    objects.detach();
    return {objects, selected_, revision_};
}
void AddedContent::restore(const State& state)
{
    objects_ = state.objects;
    selected_ = state.selected;
    revision_ = state.revision;
    error_.clear();
    emit changed();
}
void AddedContent::beginEdit()
{
    if (editing_)
        return;
    editing_ = true;
    editRecorded_ = false;
    emit changed();
}
void AddedContent::endEdit()
{
    if (!editing_)
        return;
    editing_ = false;
    editRecorded_ = false;
    emit changed();
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
                    total += sizeof(Object) + 2 * (object.text.size() + object.fontFamily.size());
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
    QFont font(object.fontFamily);
    // Both painters use page-point coordinates; the PDF writer runs at 72 DPI.
    font.setPixelSize(object.fontSize);
    font.setBold(object.bold);
    font.setItalic(object.italic);
    font.setUnderline(object.underline);
    return font;
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
        if (o.id == id && o.type == "text")
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
        if (object.id == id && object.type == "text")
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
    emit changed();
}
void AddedContent::removeSelected()
{
    if (selected_ >= 0)
        recordEdit();
    objects_.removeIf([this](const Object& o) { return o.id == selected_; });
    selected_ = -1;
    emit changed();
}
void AddedContent::clear()
{
    revision_ = ++nextRevision_;
    undo_.clear();
    redo_.clear();
    editing_ = editRecorded_ = false;
    objects_.clear();
    selected_ = -1;
    error_.clear();
    emit changed();
}
