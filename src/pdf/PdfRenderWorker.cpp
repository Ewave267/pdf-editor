// SPDX-License-Identifier: GPL-3.0-only
#include "PdfiumRuntime.h"
#include "WorkerPolicy.h"
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QDate>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void reply(const QJsonObject& result)
{
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::cout.write(bytes.data(), bytes.size());
    std::cout << '\n' << std::flush;
}

// All PDFium handles stay in this isolated process, on its single request thread.
class PdfDocument
{
  public:
    explicit PdfDocument(const QString& inputPath)
    {
        host_.lookupPage = [this](int index) -> FPDF_PAGE
        {
            if (index < 0 || index >= FPDF_GetPageCount(document_))
                return nullptr;
            return loadPage(index);
        };
        host_.caret = [this](FPDF_PAGE page, double left, double top, double right, double bottom)
        {
            if (type_ != FORMTYPE_XFA_FULL)
                return;
            const int index = openPages_.key(page, -1);
            if (index < 0)
                return;
            currentFormPage_ = index;
            focusRect_ = {{"x", std::min(left, right)},
                          {"y", std::min(top, bottom)},
                          {"width", std::abs(right - left)},
                          {"height", std::abs(bottom - top)}};
            caretUpdated_ = true;
        };
        host_.changePage = [this](int index)
        {
            if (type_ == FORMTYPE_XFA_FULL && index >= 0 && index < FPDF_GetPageCount(document_))
            {
                currentFormPage_ = index;
                loadPage(index);
            }
        };
        host_.FFI_GetRotation = [](FPDF_FORMFILLINFO*, FPDF_PAGE page)
        { return FPDFPage_GetRotation(page); };
        QFile input(inputPath);
        require(input.open(QIODevice::ReadOnly), "Cannot read the selected PDF.");
        require(input.size() > 0 && input.size() <= 64 * 1024 * 1024,
                "The viewer currently supports PDF files up to 64 MiB.");
        bytes_ = input.readAll();
        document_ = FPDF_LoadMemDocument64(bytes_.data(), bytes_.size(), nullptr);
        require(document_, "Cannot open this PDF. It may be damaged or require a password.");
    }
    ~PdfDocument()
    {
        if (form_)
        {
            FORM_ForceToKillFocus(form_);
            while (!openPages_.isEmpty())
            {
                auto it = openPages_.begin();
                auto page = it.value();
                FORM_OnBeforeClosePage(page, form_);
                openPages_.erase(it);
                FPDF_ClosePage(page);
            }
            host_.page = nullptr;
            FPDFDOC_ExitFormFillEnvironment(form_);
        }
        if (document_)
            FPDF_CloseDocument(document_);
    }

    bool isXfa() const { return type_ == FORMTYPE_XFA_FULL || type_ == FORMTYPE_XFA_FOREGROUND; }
    QJsonObject initialize()
    {
        type_ = FPDF_GetFormType(document_);
        host_.topLeftCoordinates = type_ == FORMTYPE_XFA_FULL;
        form_ = FPDFDOC_InitFormFillEnvironment(document_, &host_);
        require(form_, "Cannot initialize the document's form environment.");
        if (type_ == FORMTYPE_XFA_FULL || type_ == FORMTYPE_XFA_FOREGROUND)
            require(FPDF_LoadXFA(document_), "Cannot load this document's XFA form.");
        FORM_DoDocumentJSAction(form_);
        FORM_DoDocumentOpenAction(form_);
        FPDF_SetFormFieldHighlightColor(form_, FPDF_FORMFIELD_UNKNOWN, 0xD9EAF8);
        FPDF_SetFormFieldHighlightAlpha(form_, 60);
        const int count = FPDF_GetPageCount(document_);
        require(count > 0 && count <= 2000, "The viewer supports documents with 1 to 2000 pages.");
        QJsonArray pages;
        for (int index = 0; index < count; ++index)
        {
            FPDF_PAGE page = FPDF_LoadPage(document_, index);
            require(page, "Cannot load a document page.");
            const double width = FPDF_GetPageWidth(page);
            const double height = FPDF_GetPageHeight(page);
            collectFields(page, index);
            FPDF_ClosePage(page);
            require(std::isfinite(width) && std::isfinite(height) && width >= 1 && height >= 1 &&
                        width <= 14400 && height <= 14400,
                    "The document contains an unsupported page size.");
            pages.append(QJsonObject{{"width", width}, {"height", height}});
        }
        initialFields_ = fields_;
        return {{"pages", pages},
                {"formType", type_},
                {"fields", fields_},
                {"fieldsComplete", fieldsComplete_},
                {"deniedHostRequests", host_.deniedRequests}};
    }

    FPDF_PAGE loadPage(int index)
    {
        require(index >= 0 && index < FPDF_GetPageCount(document_), "Invalid form page.");
        auto page = openPages_.value(index);
        if (!page)
        {
            page = FPDF_LoadPage(document_, index);
            require(page, "Cannot load the form page.");
            openPages_.insert(index, page);
            host_.page = page;
            host_.pageIndex = index;
            FORM_OnAfterLoadPage(page, form_);
        }
        host_.page = page;
        host_.pageIndex = index;
        return page;
    }
    template <typename Read> QString fieldString(Read read)
    {
        const auto size = read(static_cast<unsigned short*>(nullptr), 0);
        if (size < 2 || size > 8192 || size % 2)
            return {};
        QList<unsigned short> bytes(size / 2);
        if (read(bytes.data(), size) != size)
            return {};
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),
                                  bytes.size() - 1);
    }
    QJsonObject describe(FPDF_PAGE page, int pageIndex, int index)
    {
        auto annot = FPDFPage_GetAnnot(page, index);
        if (!annot)
            return {};
        const int type = FPDFAnnot_GetFormFieldType(form_, annot);
        const int flags = FPDFAnnot_GetFormFieldFlags(form_, annot);
        FS_RECTF rect{};
        if (type <= 0 ||
            (FPDFAnnot_GetFlags(annot) &
             (FPDF_ANNOT_FLAG_HIDDEN | FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_NOVIEW)) ||
            !FPDFAnnot_GetRect(annot, &rect))
        {
            FPDFPage_CloseAnnot(annot);
            return {};
        }
        for (float coordinate : {rect.left, rect.right, rect.top, rect.bottom})
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000)
            {
                fieldsComplete_ = false;
                FPDFPage_CloseAnnot(annot);
                return {};
            }
        int x0, y0, x1, y1;
        if (!FPDF_PageToDevice(page, 0, 0, 10000, 10000, 0, rect.left, rect.top, &x0, &y0) ||
            !FPDF_PageToDevice(page, 0, 0, 10000, 10000, 0, rect.right, rect.bottom, &x1, &y1))
        {
            FPDFPage_CloseAnnot(annot);
            return {};
        }
        const auto name =
            fieldString([&](auto* buffer, auto size)
                        { return FPDFAnnot_GetFormFieldName(form_, annot, buffer, size); });
        const auto label = fieldString(
            [&](auto* buffer, auto size)
            { return FPDFAnnot_GetFormFieldAlternateName(form_, annot, buffer, size); });
        const auto value =
            fieldString([&](auto* buffer, auto size)
                        { return FPDFAnnot_GetFormFieldValue(form_, annot, buffer, size); });
        QJsonArray options, selected;
        const int optionCount = FPDFAnnot_GetOptionCount(form_, annot);
        for (int i = 0; i < std::min(optionCount, 100); ++i)
        {
            options.append(
                fieldString([&](auto* buffer, auto size)
                            { return FPDFAnnot_GetOptionLabel(form_, annot, i, buffer, size); })
                    .left(512));
            if (FPDFAnnot_IsOptionSelected(form_, annot, i))
                selected.append(i);
        }
        const auto script = fieldString(
            [&](auto* buffer, auto size)
            {
                return FPDFAnnot_GetFormAdditionalActionJavaScript(
                    form_, annot, FPDF_ANNOT_AACTION_FORMAT, buffer, size);
            });
        const auto match =
            QRegularExpression("AFDate_FormatEx\\s*\\(\\s*[\"']([^\"']+)[\"']").match(script);
        const QString format = match.hasMatch() ? match.captured(1) : QString();
        const QStringList dateFormats{"yyyy-mm-dd", "mm/dd/yyyy", "dd/mm/yyyy", "mm/dd/yy",
                                      "dd/mm/yy"};
        const double w = FPDF_GetPageWidth(page), h = FPDF_GetPageHeight(page);
        QJsonObject field{
            {"page", pageIndex},
            {"annot", index},
            {"type", type},
            {"name", name},
            {"label", label.isEmpty() ? name.left(512) : label.left(512)},
            {"value", value},
            {"valueComplete", FPDFAnnot_GetFormFieldValue(form_, annot, nullptr, 0) <= 8192},
            {"required", bool(flags & FPDF_FORMFLAG_REQUIRED)},
            {"readOnly", bool(flags & FPDF_FORMFLAG_READONLY)},
            {"checked", bool(FPDFAnnot_IsChecked(form_, annot))},
            {"options", options},
            {"selectedOptions", selected},
            {"choicesComplete", optionCount <= 100},
            {"dateFormat", dateFormats.contains(format) ? format : QString()},
            {"x", std::min(x0, x1) * w / 10000},
            {"y", std::min(y0, y1) * h / 10000},
            {"width", std::abs(double(x1) - x0) * w / 10000},
            {"height", std::abs(double(y1) - y0) * h / 10000}};
        FPDFPage_CloseAnnot(annot);
        return field;
    }
    void collectFields(FPDF_PAGE page, int pageIndex)
    {
        if (type_ != FORMTYPE_ACRO_FORM)
            return;
        for (int index = 0; index < FPDFPage_GetAnnotCount(page); ++index)
        {
            if (fields_.size() >= 512)
            {
                fieldsComplete_ = false;
                return;
            }
            auto field = describe(page, pageIndex, index);
            if (field.isEmpty())
                continue;
            const auto bytes = QJsonDocument(field).toJson(QJsonDocument::Compact).size();
            if (metadataBytes_ + bytes > 4 * 1024 * 1024)
            {
                fieldsComplete_ = false;
                return;
            }
            metadataBytes_ += bytes;
            field.insert("id", fields_.size());
            fields_.append(field);
        }
        initialFields_ = fields_;
    }
    void refreshFields()
    {
        qsizetype total = 0;
        for (int i = 0; i < fields_.size(); ++i)
        {
            const auto old = fields_[i].toObject();
            auto field =
                describe(loadPage(old["page"].toInt()), old["page"].toInt(), old["annot"].toInt());
            auto size = QJsonDocument(field).toJson(QJsonDocument::Compact).size();
            if (total + size > 4 * 1024 * 1024)
            {
                fieldsComplete_ = false;
                field.insert("options", QJsonArray{});
                field.insert("choicesComplete", false);
                size = QJsonDocument(field).toJson(QJsonDocument::Compact).size();
            }
            total += size;
            field.insert("id", i);
            fields_[i] = field;
        }
    }
    FPDF_PAGE focusField(int id)
    {
        require(type_ == FORMTYPE_ACRO_FORM && id >= 0 && id < fields_.size(),
                "This form does not expose that field to the helper.");
        const auto field = fields_[id].toObject();
        require(!field["readOnly"].toBool(), "This field is read-only.");
        auto page = loadPage(field["page"].toInt());
        auto annot = FPDFPage_GetAnnot(page, field["annot"].toInt());
        const bool focused = annot && FORM_SetFocusedAnnot(form_, annot);
        // Entry actions can change access after the metadata was collected.
        const bool readOnly =
            annot && (FPDFAnnot_GetFormFieldFlags(form_, annot) & FPDF_FORMFLAG_READONLY);
        if (annot)
            FPDFPage_CloseAnnot(annot);
        require(focused && !readOnly, "The document did not allow editing this field.");
        currentFormPage_ = field["page"].toInt();
        focusedId_ = id;
        return page;
    }
    void replaceText(FPDF_PAGE page, const QString& text)
    {
        require(text.size() <= 4096, "Text input is too large.");
        require(FORM_SelectAllText(form_, page), "This field does not accept text.");
        const unsigned short empty[] = {0};
        FORM_ReplaceSelection(form_, page, empty);
        for (QChar c : text)
            FORM_OnChar(form_, page, c.unicode(), 0);
    }
    QJsonObject errorState(const QJsonObject& request)
    {
        refreshFields();
        const QString action = request["action"].toString();
        const bool mayHaveChanged = action == "resetField" || action == "resetForm" ||
                                    action == "setFieldText" || action == "chooseOption";
        return {{"fields", fields_},
                {"fieldsComplete", fieldsComplete_},
                {"changed", host_.changed || mayHaveChanged}};
    }
    void resetField(int id)
    {
        auto field = fields_[id].toObject();
        auto current = describe(loadPage(field["page"].toInt()), field["page"].toInt(),
                                field["annot"].toInt());
        current.insert("id", id);
        fields_[id] = current;
        field = current;
        const auto initial = initialFields_[id].toObject();
        if (field["type"].toInt() == FPDF_FORMFIELD_RADIOBUTTON && !initial["checked"].toBool())
        {
            require(!initial["name"].toString().isEmpty(),
                    "This unnamed radio group cannot be reset safely.");
            for (int i = 0; i < initialFields_.size(); ++i)
            {
                const auto candidate = initialFields_[i].toObject();
                if (candidate["type"].toInt() == FPDF_FORMFIELD_RADIOBUTTON &&
                    candidate["name"] == initial["name"] && candidate["checked"].toBool())
                {
                    resetField(i);
                    return;
                }
            }
            require(field["checked"] == initial["checked"],
                    "This radio group cannot be cleared by the native helper.");
            return;
        }
        const int type = field["type"].toInt();
        require(type >= 2 && type <= 6 && !field["readOnly"].toBool(),
                "This field cannot be reset by the helper.");
        auto page = focusField(id);
        if (type == FPDF_FORMFIELD_TEXTFIELD)
        {
            require(initial["valueComplete"].toBool(),
                    "The original value is too large to restore through this helper.");
            replaceText(page, initial["value"].toString());
        }
        else if (type == FPDF_FORMFIELD_CHECKBOX || type == FPDF_FORMFIELD_RADIOBUTTON)
        {
            if (initial["checked"].toBool() != field["checked"].toBool())
            {
                require(
                    type != FPDF_FORMFIELD_RADIOBUTTON || initial["checked"].toBool(),
                    "An unselected radio button must be reset through its selected group member.");
                FS_RECTF rect{};
                auto annot = FPDFPage_GetAnnot(page, field["annot"].toInt());
                require(annot && FPDFAnnot_GetRect(annot, &rect), "Cannot locate this field.");
                FPDFPage_CloseAnnot(annot);
                FORM_OnLButtonDown(form_, page, 0, (rect.left + rect.right) / 2,
                                   (rect.top + rect.bottom) / 2);
                FORM_OnLButtonUp(form_, page, 0, (rect.left + rect.right) / 2,
                                 (rect.top + rect.bottom) / 2);
            }
        }
        else
        {
            const auto selected = initial["selectedOptions"].toArray();
            if (type == FPDF_FORMFIELD_LISTBOX)
                for (int i = 0; i < field["options"].toArray().size(); ++i)
                    FORM_SetIndexSelected(form_, page, i, false);
            for (const auto option : selected)
                require(FORM_SetIndexSelected(form_, page, option.toInt(), true),
                        "Cannot reset this choice.");
            if (selected.isEmpty() && type == FPDF_FORMFIELD_COMBOBOX)
                require(initial["value"] == field["value"],
                        "This empty or editable choice cannot be reset safely.");
        }
        FORM_ForceToKillFocus(form_);
    }
    QString focusedText(FPDF_PAGE page, bool selected = false)
    {
        const auto size = selected ? FORM_GetSelectedText(form_, page, nullptr, 0)
                                   : FORM_GetFocusedText(form_, page, nullptr, 0);
        require(size <= 128 * 1024 && size % 2 == 0, "The field text is too large.");
        if (size < 2)
            return {};
        QByteArray bytes(size, 0);
        if (selected)
            FORM_GetSelectedText(form_, page, bytes.data(), size);
        else
            FORM_GetFocusedText(form_, page, bytes.data(), size);
        QString text;
        for (int i = 0; i + 2 < bytes.size(); i += 2)
            text.append(
                QChar(static_cast<uchar>(bytes[i]) | (static_cast<uchar>(bytes[i + 1]) << 8)));
        return text;
    }
    QJsonObject event(const QJsonObject& request)
    {
        const int index = request.value("page").toInt(-1);
        currentFormPage_ = index;
        FPDF_PAGE page = loadPage(index);
        const QString action = request.value("action").toString();
        const int flags = request.value("flags").toInt() & 7;
        bool handled = false;
        QString copiedText;
        host_.changed = false;
        caretUpdated_ = false;
        host_.validationMessage.clear();
        if (action == "highlight")
        {
            FPDF_SetFormFieldHighlightAlpha(form_, request["key"].toInt() ? 60 : 0);
            handled = true;
        }
        else if (action == "focusField" || action == "nextField")
        {
            int id = request["key"].toInt();
            if (action == "nextField")
            {
                require(!fields_.isEmpty(), "This form uses native Tab navigation.");
                id = focusedId_;
                if (id < 0 && (flags & 1))
                    id = 0;
                for (int i = 0; i < fields_.size(); ++i)
                {
                    id = (id + (flags & 1 ? -1 : 1) + fields_.size()) % fields_.size();
                    const auto candidate = fields_[id].toObject();
                    if (!candidate["readOnly"].toBool() && candidate["type"].toInt() >= 1 &&
                        candidate["type"].toInt() <= 7)
                        break;
                }
            }
            page = focusField(id);
            handled = true;
        }
        else if (action == "setFieldText")
        {
            page = focusField(request["key"].toInt());
            require(fields_[focusedId_].toObject()["type"].toInt() == FPDF_FORMFIELD_TEXTFIELD,
                    "This helper requires a text field.");
            replaceText(page, request["text"].toString());
            FORM_ForceToKillFocus(form_);
            handled = true;
            host_.changed = true;
        }
        else if (action == "chooseOption")
        {
            page = focusField(request["key"].toInt());
            bool valid = false;
            const int option = request["text"].toString().toInt(&valid);
            require(valid && option >= 0 &&
                        option < fields_[focusedId_].toObject()["options"].toArray().size(),
                    "Invalid choice.");
            require(FORM_SetIndexSelected(form_, page, option, true),
                    "This choice cannot be changed by the helper.");
            FORM_ForceToKillFocus(form_);
            handled = true;
            host_.changed = true;
        }
        else if (action == "resetField" || action == "resetForm")
        {
            require(
                type_ == FORMTYPE_ACRO_FORM && fieldsComplete_,
                "Reset helpers are unavailable for this form; use the document's native controls.");
            if (action == "resetField")
            {
                const int id = request["key"].toInt();
                require(id >= 0 && id < fields_.size(), "Invalid field.");
                resetField(id);
            }
            else
            {
                FORM_ForceToKillFocus(form_);
                refreshFields();
                for (int i = 0; i < fields_.size(); ++i)
                {
                    const auto field = fields_[i].toObject();
                    const int type = field["type"].toInt();
                    if (field["readOnly"].toBool() || type < 2 || type > 6)
                        continue;
                    if (type == FPDF_FORMFIELD_RADIOBUTTON &&
                        !initialFields_[i].toObject()["checked"].toBool())
                        continue;
                    resetField(i);
                }
            }
            if (action == "resetForm")
            {
                focusedId_ = -1;
                currentFormPage_ = 0;
                page = loadPage(0);
            }
            handled = true;
            host_.changed = true;
        }
        else if (action == "validate")
        {
            handled = FORM_ForceToKillFocus(form_);
        }
        else if (action == "click")
        {
            double x = request.value("x").toDouble(), y = request.value("y").toDouble();
            require(std::isfinite(x) && std::isfinite(y), "Invalid field coordinates.");
            if (type_ != FORMTYPE_XFA_FULL)
            {
                const double width = FPDF_GetPageWidth(page), height = FPDF_GetPageHeight(page);
                double x0, y0, x1, y1, x2, y2;
                require(FPDF_DeviceToPage(page, 0, 0, 10000, 10000, 0, 0, 0, &x0, &y0) &&
                            FPDF_DeviceToPage(page, 0, 0, 10000, 10000, 0, 10000, 0, &x1, &y1) &&
                            FPDF_DeviceToPage(page, 0, 0, 10000, 10000, 0, 0, 10000, &x2, &y2),
                        "Cannot map form coordinates.");
                const double px = x0 + x / width * (x1 - x0) + y / height * (x2 - x0);
                y = y0 + x / width * (y1 - y0) + y / height * (y2 - y0);
                x = px;
            }
            selectAllPending_ = false;
            focusedType_ = FPDFPage_HasFormFieldAtPoint(form_, page, x, y);
            FORM_OnMouseMove(form_, page, flags, x, y);
            handled = FORM_OnLButtonDown(form_, page, flags, x, y);
            handled = FORM_OnLButtonUp(form_, page, flags, x, y) || handled;
        }
        else if (action == "text")
        {
            const auto text = request.value("text").toString();
            require(text.size() <= 4096, "Text input is too large.");
            if (type_ == FORMTYPE_XFA_FULL && selectAllPending_)
            {
                const unsigned short empty[] = {0};
                FORM_ReplaceSelection(form_, page, empty);
            }
            selectAllPending_ = false;
            for (QChar character : text)
                handled = FORM_OnChar(form_, page, character.unicode(), flags) || handled;
        }
        else if (action == "key")
        {
            const int key = request.value("key").toInt();
            require(key >= 0 && key <= 255, "Invalid field key.");
            if (type_ == FORMTYPE_XFA_FULL && selectAllPending_ &&
                (key == 8 || key == 46 || key == 32 || key == 13))
            {
                const unsigned short empty[] = {0};
                FORM_ReplaceSelection(form_, page, empty);
                handled = true;
            }
            selectAllPending_ = false;
            handled = FORM_OnKeyDown(form_, page, key, flags) || handled;
            // PDFium's page-local Tab handler reports an exhausted page when
            // there is no next native widget. Try adjacent pages without
            // guessing XFA field rectangles or overriding document rules.
            if (type_ == FORMTYPE_XFA_FULL && key == 9 && !handled && FORM_ForceToKillFocus(form_))
            {
                const int count = FPDF_GetPageCount(document_);
                for (int step = 1; step <= std::min(count, 32); ++step)
                {
                    const int next = (index + ((flags & 1) ? -step : step) + count) % count;
                    auto candidate = loadPage(next);
                    if (FORM_OnKeyDown(form_, candidate, key, flags))
                    {
                        page = candidate;
                        currentFormPage_ = next;
                        handled = true;
                        if (!caretUpdated_)
                            focusRect_ = {};
                        caretUpdated_ = true;
                        break;
                    }
                }
            }
            if (key == 8 || key == 13 || key == 32)
                handled = FORM_OnChar(form_, page, key, flags) || handled;
        }
        else if (action == "selectAll")
        {
            handled = FORM_SelectAllText(form_, page);
            selectAllPending_ = handled;
        }
        else if (action == "blur")
        {
            selectAllPending_ = false;
            handled = FORM_ForceToKillFocus(form_);
            focusedType_ = -1;
        }
        else if (action == "copy" || action == "cut")
        {
            copiedText = focusedText(page, true);
            handled = true;
            if (action == "cut" && !copiedText.isEmpty())
            {
                if (type_ == FORMTYPE_XFA_FULL)
                {
                    // XFA's public replacement path checks CanPaste/read-only
                    // access. Its Delete key path does not clear this selection.
                    const auto before = focusedText(page);
                    const unsigned short empty[] = {0};
                    FORM_ReplaceSelection(form_, page, empty);
                    handled = focusedText(page) != before;
                    host_.changed = host_.changed || handled;
                }
                else
                {
                    // AcroForm deletion uses native access checks/actions.
                    handled = FORM_OnKeyDown(form_, page, 46, flags);
                }
                selectAllPending_ = false;
            }
        }
        else
            throw std::runtime_error("Unsupported form event.");
        // Public focused-annotation enumeration supports AcroForm, but not full XFA.
        if (type_ == FORMTYPE_ACRO_FORM)
        {
            int focusedPage = -1;
            FPDF_ANNOTATION annotation = nullptr;
            if (FORM_GetFocusedAnnot(form_, &focusedPage, &annotation))
            {
                focusedType_ = annotation ? FPDFAnnot_GetFormFieldType(form_, annotation) : -1;
                // Retain the last selected field while a toolbar/dialog commits
                // native focus, so Next and field helpers keep their target.
                if (annotation)
                {
                    focusedId_ = -1;
                    const int index = FPDFPage_GetAnnotIndex(loadPage(focusedPage), annotation);
                    for (int i = 0; i < fields_.size(); ++i)
                    {
                        const auto field = fields_[i].toObject();
                        if (field["page"].toInt() == focusedPage && field["annot"].toInt() == index)
                            focusedId_ = i;
                    }
                    currentFormPage_ = focusedPage;
                    page = loadPage(focusedPage);
                }
                if (annotation)
                    FPDFPage_CloseAnnot(annotation);
            }
        }
        if (type_ == FORMTYPE_XFA_FULL && currentFormPage_ >= 0)
            page = loadPage(currentFormPage_);
        refreshFields();
        // The annotation value is committed on blur. Publish the active edit
        // buffer for text fields so helper feedback follows ongoing typing.
        if (type_ == FORMTYPE_ACRO_FORM && focusedType_ == FPDF_FORMFIELD_TEXTFIELD &&
            focusedId_ >= 0)
        {
            auto field = fields_[focusedId_].toObject();
            field.insert("value", focusedText(page));
            fields_[focusedId_] = field;
        }
        const bool mutation =
            host_.changed ||
            (handled &&
             (action == "text" ||
              (action == "key" &&
               (request.value("key").toInt() == 8 || request.value("key").toInt() == 46)) ||
              (action == "click" && (focusedType_ == 2 || focusedType_ == 3 || focusedType_ == 4 ||
                                     focusedType_ == 9 || focusedType_ == 10))));
        return {{"handled", handled},
                {"changed", mutation},
                {"fields", fields_},
                {"fieldsComplete", fieldsComplete_},
                {"focusedField", focusedId_},
                {"focusPage", currentFormPage_},
                {"focusRect", focusRect_},
                {"caretUpdated", caretUpdated_},
                {"validationMessage", QString::fromUtf16(host_.validationMessage.data(),
                                                         host_.validationMessage.size())},
                {"fieldType", focusedType_},
                {"text", focusedText(page)},
                {"deniedHostRequests", host_.deniedRequests},
                {"selectedText", copiedText}};
    }

    QJsonObject snapshot()
    {
        host_.changed = false;
        const auto bytes = save({});
        return {{"pdf", QString::fromLatin1(bytes.toBase64())}, {"changed", host_.changed}};
    }
    QByteArray save(const QByteArray& overlayBytes)
    {
        require(type_ != FORMTYPE_XFA_FULL || overlayBytes.isEmpty(),
                "Dynamic XFA additions cannot be saved safely yet.");
        FPDF_DOCUMENT overlay = nullptr;
        struct OverlayGuard
        {
            FPDF_DOCUMENT& document;
            ~OverlayGuard()
            {
                if (document)
                    FPDF_CloseDocument(document);
            }
        } overlayGuard{overlay};
        if (!overlayBytes.isEmpty())
        {
            overlay =
                FPDF_LoadMemDocument64(overlayBytes.constData(), overlayBytes.size(), nullptr);
            require(overlay, "Cannot open the added-content PDF.");
            require(FPDF_GetPageCount(overlay) == FPDF_GetPageCount(document_),
                    "Added-content page count mismatch.");
            for (int index = 0; index < FPDF_GetPageCount(document_); ++index)
            {
                FPDF_PAGE page = FPDF_LoadPage(document_, index);
                require(page, "Cannot load a page for saving.");
                struct PageGuard
                {
                    FPDF_PAGE page;
                    ~PageGuard() { FPDF_ClosePage(page); }
                } guard{page};
                FPDF_PAGE overlayPage = FPDF_LoadPage(overlay, index);
                require(overlayPage, "Cannot load an added-content page.");
                const double ow = FPDF_GetPageWidth(overlayPage),
                             oh = FPDF_GetPageHeight(overlayPage);
                FPDF_ClosePage(overlayPage);
                double x0, y0, x1, y1, x2, y2;
                require(
                    FPDF_DeviceToPage(page, 0, 0, 10000, 10000, 0, 0, 10000, &x0, &y0) &&
                        FPDF_DeviceToPage(page, 0, 0, 10000, 10000, 0, 10000, 10000, &x1, &y1) &&
                        FPDF_DeviceToPage(page, 0, 0, 10000, 10000, 0, 0, 0, &x2, &y2),
                    "Cannot map added-content coordinates.");
                FPDF_XOBJECT xobject = FPDF_NewXObjectFromPage(document_, overlay, index);
                require(xobject, "Cannot import added content.");
                FPDF_PAGEOBJECT object = FPDF_NewFormObjectFromXObject(xobject);
                FPDF_CloseXObject(xobject);
                require(object, "Cannot create the added-content object.");
                FPDFPageObj_Transform(object, (x1 - x0) / ow, (y1 - y0) / ow, (x2 - x0) / oh,
                                      (y2 - y0) / oh, x0, y0);
                require(FPDFPage_InsertObject(page, object), "Cannot insert added content.");
                require(FPDFPage_GenerateContent(page), "Cannot update page content.");
            }
        }
        FORM_ForceToKillFocus(form_);
        struct Writer : FPDF_FILEWRITE
        {
            QByteArray bytes;
            Writer()
            {
                version = 1;
                WriteBlock = [](FPDF_FILEWRITE* base, const void* data, unsigned long size)
                {
                    auto& writer = *static_cast<Writer*>(base);
                    if (size > 64 * 1024 * 1024 - static_cast<unsigned long>(writer.bytes.size()))
                        return 0;
                    try
                    {
                        writer.bytes.append(static_cast<const char*>(data), size);
                        return 1;
                    }
                    catch (...)
                    {
                        return 0;
                    }
                };
            }
        } writer;
        FORM_DoDocumentAAction(form_, FPDFDOC_AACTION_WS);
        require(FPDF_SaveAsCopy(document_, &writer, FPDF_NO_INCREMENTAL), "PDF save failed.");
        FORM_DoDocumentAAction(form_, FPDFDOC_AACTION_DS);
        return writer.bytes;
    }

    QJsonObject render(int index, int width)
    {
        QElapsedTimer renderTimer;
        renderTimer.start();
        require(index >= 0 && index < FPDF_GetPageCount(document_), "Invalid page number.");
        require(width >= 96 && width <= 2400, "Unsupported rendering resolution.");
        FPDF_PAGE page = loadPage(index);
        const double pageWidth = FPDF_GetPageWidth(page), pageHeight = FPDF_GetPageHeight(page);
        require(std::isfinite(pageWidth) && std::isfinite(pageHeight) && pageWidth >= 1 &&
                    pageHeight >= 1 && pageWidth <= 14400 && pageHeight <= 14400,
                "This page has unsupported dimensions.");
        const double rasterHeight = std::ceil(width * pageHeight / pageWidth);
        require(rasterHeight >= 1 && rasterHeight <= 12000 && width * rasterHeight <= 16000000,
                "This page is too large to render at the requested zoom.");
        const int height = static_cast<int>(rasterHeight);
        QImage image(width, height, QImage::Format_ARGB32);
        require(!image.isNull(), "Cannot allocate the page image.");
        image.fill(Qt::white);
        auto bitmap =
            FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA, image.bits(), image.bytesPerLine());
        require(bitmap, "Cannot initialize the page renderer.");
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, FPDF_ANNOT);
        FPDF_FFLDraw(form_, bitmap, page, 0, 0, width, height, 0, FPDF_ANNOT);
        FPDFBitmap_Destroy(bitmap);
        const auto rasterMs = renderTimer.elapsed();
        QByteArray encoded;
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        require(image.save(&buffer, "PNG"), "Cannot encode the rendered page.");
        if (std::getenv("PDF_EDITOR_WORKER_DIAGNOSTICS"))
            std::cerr << "Native render: page=" << index << " width=" << width
                      << " raster_ms=" << rasterMs << " png_ms=" << renderTimer.elapsed() - rasterMs
                      << std::endl;
        if (currentFormPage_ >= 0)
        {
            host_.page = openPages_.value(currentFormPage_);
            host_.pageIndex = currentFormPage_;
        }
        return {{"png", QString::fromLatin1(encoded.toBase64())}};
    }

  private:
    QByteArray bytes_;
    pdf::detail::Host host_;
    FPDF_DOCUMENT document_ = nullptr;
    FPDF_FORMHANDLE form_ = nullptr;
    bool selectAllPending_ = false;
    QJsonArray fields_, initialFields_;
    QJsonObject focusRect_;
    bool caretUpdated_ = false;
    bool fieldsComplete_ = true;
    qsizetype metadataBytes_ = 0;
    int focusedId_ = -1;
    int type_ = 0, focusedType_ = -1, currentFormPage_ = -1;
    QMap<int, FPDF_PAGE> openPages_;
};
} // namespace

int main(int argc, char** argv)
{
    try
    {
        QElapsedTimer startupTimer;
        startupTimer.start();
        auto stage = [&startupTimer](const char* message)
        {
            if (std::getenv("PDF_EDITOR_WORKER_DIAGNOSTICS"))
                std::cerr << "Native worker startup: elapsed_ms=" << startupTimer.elapsed() << " "
                          << message << std::endl;
        };
        stage("entered main");
#ifdef Q_OS_WIN
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
#ifdef Q_OS_MACOS
        // V8 reserves its trusted, empty heap cages before Darwin's VM growth
        // ceiling is measured. No document bytes are read before Seatbelt.
        stage("initializing trusted PDFium runtime");
        pdf::detail::Library library;
        // Apply Seatbelt before Qt startup and all document reads.
        for (int index = 1; index < argc; ++index)
        {
            if (std::string(argv[index]) == "--sandbox-profile")
            {
                if (++index >= argc)
                    throw std::runtime_error("Missing macOS sandbox profile.");
                stage("applying Seatbelt");
                pdf::detail::applyMacWorkerSandbox(argv[index]);
                break;
            }
        }
#endif
        stage("verifying isolation and resource limits");
        pdf::detail::installWorkerPolicy();
        stage("starting Qt Core");
        QCoreApplication app(argc, argv);
        const auto arguments = app.arguments();
        QString inputPath = "/input.pdf";
        bool save = false;
        for (int index = 1; index < arguments.size(); ++index)
        {
            const QString argument = arguments[index];
#ifdef Q_OS_MACOS
            if (argument == "--sandbox-profile" && index + 1 < arguments.size())
            {
                ++index;
                continue;
            }
#endif
            if (argument == "--save")
                save = true;
#if !defined(Q_OS_LINUX)
            else if (argument == "--input" && index + 1 < arguments.size())
                inputPath = arguments[++index];
#endif
            else
                throw std::runtime_error("Unknown worker argument.");
        }
        stage("initializing PDFium and V8");
#ifndef Q_OS_MACOS
        pdf::detail::Library library;
#endif
        stage("opening document snapshot");
        auto document = std::make_unique<PdfDocument>(inputPath);
        if (save)
        {
            document->initialize();
            QByteArray overlay;
            char block[65536];
            while (std::cin)
            {
                std::cin.read(block, sizeof(block));
                overlay.append(block, std::cin.gcount());
                require(overlay.size() <= 32 * 1024 * 1024,
                        "Added content exceeds the save limit.");
            }
            const QByteArray saved = document->save(overlay);
            std::cout.write(saved.constData(), saved.size());
            std::cout.flush();
            require(static_cast<bool>(std::cout), "Cannot return the saved PDF.");
            return 0;
        }
        reply(document->initialize());
        stage("document initialized");
        char line[64 * 1024 + 1];
        while (std::cin.getline(line, sizeof(line)))
        {
            QJsonParseError parseError;
            const auto request = QJsonDocument::fromJson(QByteArray(line), &parseError).object();
            const auto id = request.value("id");
            try
            {
                require(parseError.error == QJsonParseError::NoError && !request.isEmpty() &&
                            id.isDouble() && id.toDouble() > 0 &&
                            id.toDouble() <= 9007199254740991.0 &&
                            std::floor(id.toDouble()) == id.toDouble(),
                        "Invalid worker request.");
                QJsonObject result;
                QElapsedTimer operationTimer;
                operationTimer.start();
                const QString op = request.value("op").toString();
                if (op == "event" && request["action"] == "resetForm" && document->isXfa())
                {
                    // Recreate the native environment from the immutable opening
                    // snapshot. Never rewrite XFA XML or bypass its scripts.
                    auto replacement = std::make_unique<PdfDocument>(inputPath);
                    result = replacement->initialize();
                    document = std::move(replacement);
                    result.insert("handled", true);
                    result.insert("changed", true);
                    result.insert("focusedField", -1);
                    result.insert("fieldType", -1);
                    result.insert("focusPage", 0);
                    result.insert("resetPages", result["pages"]);
                }
                else if (op == "event")
                    result = document->event(request);
                else if (op == "snapshot")
                    result = document->snapshot();
                else if (op.isEmpty() || op == "render")
                    result = document->render(request.value("page").toInt(-1),
                                              request.value("width").toInt());
                else
                    throw std::runtime_error("Unsupported worker command.");
                result.insert("id", id);
                if (std::getenv("PDF_EDITOR_WORKER_DIAGNOSTICS"))
                    result.insert("worker_ms", static_cast<double>(operationTimer.elapsed()));
                reply(result);
            }
            catch (const std::exception& error)
            {
                QJsonObject result{{"id", id}, {"error", error.what()}};
                if (request["op"] == "event")
                {
                    const auto state = document->errorState(request);
                    for (auto it = state.begin(); it != state.end(); ++it)
                        result.insert(it.key(), it.value());
                }
                reply(result);
            }
        }
        if (!std::cin.eof())
            return 2; // Oversized/incomplete protocol requests never grow an unbounded string.
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        if (argc != 2)
            reply({{"error", error.what()}});
        return 1;
    }
}
