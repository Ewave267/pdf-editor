// SPDX-License-Identifier: GPL-3.0-only
#include "PdfiumRuntime.h"
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
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>

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
    PdfDocument()
    {
        QFile input("/input.pdf");
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
            FPDFDOC_ExitFormFillEnvironment(form_);
        if (document_)
            FPDF_CloseDocument(document_);
    }

    QJsonObject initialize()
    {
        type_ = FPDF_GetFormType(document_);
        form_ = FPDFDOC_InitFormFillEnvironment(document_, &host_);
        require(form_, "Cannot initialize the document's form environment.");
        if (type_ == FORMTYPE_XFA_FULL || type_ == FORMTYPE_XFA_FOREGROUND)
            require(FPDF_LoadXFA(document_), "Cannot load this document's XFA form.");
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
            require(std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0 &&
                        width <= 14400 && height <= 14400,
                    "The document contains an unsupported page size.");
            pages.append(QJsonObject{{"width", width}, {"height", height}});
        }
        return {{"pages", pages}, {"formType", type_}};
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
        FPDF_PAGE page = FPDF_LoadPage(document_, index);
        require(page, "Cannot load the requested page.");
        struct PageGuard
        {
            FPDF_PAGE page;
            FPDF_FORMHANDLE form;
            pdf::detail::Host& host;
            ~PageGuard()
            {
                FORM_OnBeforeClosePage(page, form);
                FPDF_ClosePage(page);
                host.page = nullptr;
            }
        } guard{page, form_, host_};
        host_.page = page;
        host_.pageIndex = index;
        FORM_OnAfterLoadPage(page, form_);
        const double ratio = FPDF_GetPageHeight(page) / FPDF_GetPageWidth(page);
        const int height = static_cast<int>(std::ceil(width * ratio));
        require(height > 0 && height <= 12000 && static_cast<qint64>(width) * height <= 16000000,
                "This page is too large to render at the requested zoom.");
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
        return {{"png", QString::fromLatin1(encoded.toBase64())}};
    }

  private:
    QByteArray bytes_;
    pdf::detail::Host host_;
    FPDF_DOCUMENT document_ = nullptr;
    FPDF_FORMHANDLE form_ = nullptr;
    int type_ = 0;
};
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    rlimit cpu{300, 300}, core{0, 0}, files{128, 128};
    setrlimit(RLIMIT_CPU, &cpu);
    setrlimit(RLIMIT_CORE, &core);
    setrlimit(RLIMIT_NOFILE, &files);
    try
    {
        pdf::detail::Library library;
        PdfDocument document;
        if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--save")
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
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line.size() > 4096)
                return 2;
            const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
            const auto id = request.value("id");
            try
            {
                auto result = document.render(request.value("page").toInt(-1),
                                              request.value("width").toInt());
                result.insert("id", id);
                reply(result);
            }
            catch (const std::exception& error)
            {
                reply({{"id", id}, {"error", error.what()}});
            }
        }
    }
    catch (const std::exception& error)
    {
        if (argc == 2)
            std::cerr << error.what() << '\n';
        else
            reply({{"error", error.what()}});
        return 1;
    }
}
