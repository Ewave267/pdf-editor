// SPDX-License-Identifier: GPL-3.0-only
#include "PdfiumRuntime.h"
#include "WorkerPolicy.h"
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <cmath>
#include <iostream>
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
        const int count = FPDF_GetPageCount(document_);
        require(count > 0 && count <= 2000, "The viewer supports documents with 1 to 2000 pages.");
        QJsonArray pages;
        for (int index = 0; index < count; ++index)
        {
            FPDF_PAGE page = FPDF_LoadPage(document_, index);
            require(page, "Cannot load a document page.");
            const double width = FPDF_GetPageWidth(page);
            const double height = FPDF_GetPageHeight(page);
            FPDF_ClosePage(page);
            require(std::isfinite(width) && std::isfinite(height) && width >= 1 && height >= 1 &&
                        width <= 14400 && height <= 14400,
                    "The document contains an unsupported page size.");
            pages.append(QJsonObject{{"width", width}, {"height", height}});
        }
        return {
            {"pages", pages}, {"formType", type_}, {"deniedHostRequests", host_.deniedRequests}};
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
        host_.changed = false;
        if (action == "click")
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
        else if (action == "copy")
            handled = true;
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
                if (annotation)
                    FPDFPage_CloseAnnot(annotation);
            }
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
                {"fieldType", focusedType_},
                {"text", focusedText(page)},
                {"deniedHostRequests", host_.deniedRequests},
                {"selectedText", action == "copy" ? focusedText(page, true) : QString()}};
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
        QByteArray encoded;
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        require(image.save(&buffer, "PNG"), "Cannot encode the rendered page.");
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
    int type_ = 0, focusedType_ = -1, currentFormPage_ = -1;
    QMap<int, FPDF_PAGE> openPages_;
};
} // namespace

int main(int argc, char** argv)
{
    try
    {
#ifdef Q_OS_WIN
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        pdf::detail::installWorkerPolicy();
        QCoreApplication app(argc, argv);
        const auto arguments = app.arguments();
        QString inputPath = "/input.pdf";
        bool save = false;
        for (int index = 1; index < arguments.size(); ++index)
        {
            const QString argument = arguments[index];
            if (argument == "--save")
                save = true;
#if !defined(Q_OS_LINUX)
            else if (argument == "--input" && index + 1 < arguments.size())
                inputPath = arguments[++index];
#endif
            else
                throw std::runtime_error("Unknown worker argument.");
        }
        pdf::detail::Library library;
        PdfDocument document(inputPath);
        if (save)
        {
            document.initialize();
            QByteArray overlay;
            char block[65536];
            while (std::cin)
            {
                std::cin.read(block, sizeof(block));
                overlay.append(block, std::cin.gcount());
                require(overlay.size() <= 32 * 1024 * 1024,
                        "Added content exceeds the save limit.");
            }
            const QByteArray saved = document.save(overlay);
            std::cout.write(saved.constData(), saved.size());
            std::cout.flush();
            require(static_cast<bool>(std::cout), "Cannot return the saved PDF.");
            return 0;
        }
        reply(document.initialize());
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
                const QString op = request.value("op").toString();
                if (op == "event")
                    result = document.event(request);
                else if (op == "snapshot")
                    result = document.snapshot();
                else if (op.isEmpty() || op == "render")
                    result = document.render(request.value("page").toInt(-1),
                                             request.value("width").toInt());
                else
                    throw std::runtime_error("Unsupported worker command.");
                result.insert("id", id);
                reply(result);
            }
            catch (const std::exception& error)
            {
                reply({{"id", id}, {"error", error.what()}});
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
