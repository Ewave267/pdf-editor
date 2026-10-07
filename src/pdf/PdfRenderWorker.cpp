// SPDX-License-Identifier: GPL-3.0-only
#include "PdfiumRuntime.h"

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
        reply({{"error", error.what()}});
        return 1;
    }
}
