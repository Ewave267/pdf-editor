// SPDX-License-Identifier: GPL-3.0-only
#include "XfaProbe.h"
#include "PdfiumRuntime.h"

#include <fpdf_formfill.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace pdf
{
namespace
{
template <typename Handle, auto Destroy>
using Owned = std::unique_ptr<std::remove_pointer_t<Handle>, decltype(Destroy)>;

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(file.is_open(), "cannot open input: " + path.string());
    const auto size = file.tellg();
    require(size > 0 && size <= 32 * 1024 * 1024, "input must be between 1 byte and 32 MiB");
    std::vector<char> data(static_cast<std::size_t>(size));
    file.seekg(0);
    require(static_cast<bool>(file.read(data.data(), size)), "cannot read input");
    return data;
}

void writeFile(const std::filesystem::path& path, const std::vector<char>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    require(file.is_open(), "cannot open output: " + path.string());
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    require(static_cast<bool>(file), "cannot write output: " + path.string());
}

using detail::Host;
using detail::Library;

class Session
{
  public:
    explicit Session(const std::vector<char>& bytes)
        : document_(FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr), FPDF_CloseDocument)
    {
        require(document_ != nullptr,
                "PDF open failed (PDFium error " + std::to_string(FPDF_GetLastError()) + ")");
        const int type = FPDF_GetFormType(document_.get());
        require(type == FORMTYPE_XFA_FULL || type == FORMTYPE_XFA_FOREGROUND,
                "expected an XFA document, got form type " + std::to_string(type));
        std::cout << "form_type=" << (type == FORMTYPE_XFA_FULL ? "XFA_FULL" : "XFA_FOREGROUND")
                  << '\n';
        form_.reset(FPDFDOC_InitFormFillEnvironment(document_.get(), &host_));
        require(form_ != nullptr, "form environment initialization failed");
        require(FPDF_LoadXFA(document_.get()), "XFA load failed; check PDFium XFA/V8 support");
        require(FPDF_GetPageCount(document_.get()) == 1, "probe expects exactly one XFA page");
        page_.reset(FPDF_LoadPage(document_.get(), 0));
        require(page_ != nullptr, "page load failed");
        host_.page = page_.get();
        FORM_OnAfterLoadPage(page_.get(), form_.get());
        FORM_DoDocumentJSAction(form_.get());
        FORM_DoDocumentOpenAction(form_.get());
        FORM_DoPageAAction(page_.get(), form_.get(), FPDFPAGE_AACTION_OPEN);
        require(host_.deniedRequests > 0,
                "fixture's scripted URL request was not denied by the host");
        std::cout << "denied_host_requests=" << host_.deniedRequests << '\n';
    }

    ~Session()
    {
        if (page_)
        {
            FORM_DoPageAAction(page_.get(), form_.get(), FPDFPAGE_AACTION_CLOSE);
            FORM_OnBeforeClosePage(page_.get(), form_.get());
            page_.reset();
            host_.page = nullptr;
        }
        if (form_)
            FORM_DoDocumentAAction(form_.get(), FPDFDOC_AACTION_WC);
    }

    void focus(double y)
    {
        const int type = FPDFPage_HasFormFieldAtPoint(form_.get(), page_.get(), 90, y);
        require(type == FPDF_FORMFIELD_XFA_TEXTFIELD, "expected XFA text field at (90," +
                                                          std::to_string(y) + "), got type " +
                                                          std::to_string(type));
        require(FORM_OnLButtonDown(form_.get(), page_.get(), 0, 90, y), "field mouse-down failed");
        require(FORM_OnLButtonUp(form_.get(), page_.get(), 0, 90, y), "field mouse-up failed");
    }

    std::string focusedText()
    {
        const auto size = FORM_GetFocusedText(form_.get(), page_.get(), nullptr, 0);
        require(size >= 2 && size % 2 == 0 && size < 8192, "invalid focused text size");
        std::vector<unsigned char> text(size);
        require(FORM_GetFocusedText(form_.get(), page_.get(), text.data(), size) == size,
                "focused text read failed");
        std::string result;
        for (std::size_t i = 0; i + 2 < text.size(); i += 2)
        {
            require(text[i + 1] == 0 && text[i] < 128, "fixture text must be ASCII");
            result.push_back(static_cast<char>(text[i]));
        }
        return result;
    }

    void expect(double y, const std::string& expected)
    {
        focus(y);
        const auto actual = focusedText();
        require(actual == expected,
                "field verification failed: expected '" + expected + "', got '" + actual + "'");
    }

    void edit(const std::string& value)
    {
        // Dynamic XFA field event coordinates use its top-left layout origin.
        focus(87);
        require(FORM_SelectAllText(form_.get(), page_.get()), "select-all failed");
        const unsigned short empty[] = {0};
        FORM_ReplaceSelection(form_.get(), page_.get(), empty);
        for (unsigned char character : value)
        {
            require(character >= 32 && character < 127, "probe value must be printable ASCII");
            require(FORM_OnChar(form_.get(), page_.get(), character, 0),
                    "field character event failed");
        }
        const auto actual = focusedText();
        require(actual == value, "input value did not change: got '" + actual + "'");
        require(FORM_ForceToKillFocus(form_.get()), "field commit failed");
        expect(147, "JS:" + value);
        require(FORM_ForceToKillFocus(form_.get()), "calculated field commit failed");
    }

    void render(const std::filesystem::path& path)
    {
        constexpr int width = 612;
        constexpr int height = 792;
        Owned<FPDF_BITMAP, FPDFBitmap_Destroy> bitmap(FPDFBitmap_Create(width, height, 0),
                                                      FPDFBitmap_Destroy);
        require(bitmap != nullptr, "bitmap allocation failed");
        FPDFBitmap_FillRect(bitmap.get(), 0, 0, width, height, 0xFFFFFFFF);
        FPDF_RenderPageBitmap(bitmap.get(), page_.get(), 0, 0, width, height, 0, FPDF_ANNOT);
        FPDF_FFLDraw(form_.get(), bitmap.get(), page_.get(), 0, 0, width, height, 0, FPDF_ANNOT);
        const auto* buffer = static_cast<const unsigned char*>(FPDFBitmap_GetBuffer(bitmap.get()));
        const int stride = FPDFBitmap_GetStride(bitmap.get());
        std::vector<char> image;
        const std::string header = "P6\n612 792\n255\n";
        image.insert(image.end(), header.begin(), header.end());
        std::size_t nonwhite = 0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto* pixel = buffer + y * stride + x * 4;
                image.push_back(static_cast<char>(pixel[2]));
                image.push_back(static_cast<char>(pixel[1]));
                image.push_back(static_cast<char>(pixel[0]));
                if (pixel[0] != 255 || pixel[1] != 255 || pixel[2] != 255)
                    ++nonwhite;
            }
        }
        require(nonwhite > 100, "render produced a blank page");
        writeFile(path, image);
        std::cout << "render_nonwhite_pixels=" << nonwhite << '\n';
    }

    void save(const std::filesystem::path& path)
    {
        struct Writer : FPDF_FILEWRITE
        {
            std::vector<char> bytes;
            Writer() : FPDF_FILEWRITE{}
            {
                version = 1;
                WriteBlock = [](FPDF_FILEWRITE* base, const void* data, unsigned long size)
                {
                    auto& writer = *static_cast<Writer*>(base);
                    if (size > 32 * 1024 * 1024 - writer.bytes.size())
                        return 0;
                    try
                    {
                        const auto* start = static_cast<const char*>(data);
                        writer.bytes.insert(writer.bytes.end(), start, start + size);
                        return 1;
                    }
                    catch (...)
                    {
                        return 0; // Exceptions must never cross PDFium's C callback boundary.
                    }
                };
            }
        } writer;
        FORM_DoDocumentAAction(form_.get(), FPDFDOC_AACTION_WS);
        require(FPDF_SaveAsCopy(document_.get(), &writer, FPDF_NO_INCREMENTAL), "PDF save failed");
        FORM_DoDocumentAAction(form_.get(), FPDFDOC_AACTION_DS);
        writeFile(path, writer.bytes);
    }

  private:
    Host host_;
    Owned<FPDF_DOCUMENT, FPDF_CloseDocument> document_{nullptr, FPDF_CloseDocument};
    Owned<FPDF_FORMHANDLE, FPDFDOC_ExitFormFillEnvironment> form_{nullptr,
                                                                  FPDFDOC_ExitFormFillEnvironment};
    Owned<FPDF_PAGE, FPDF_ClosePage> page_{nullptr, FPDF_ClosePage};
};
} // namespace

void runXfaProbe(const ProbeOptions& options)
{
    require(!options.value.empty() && options.value.size() <= 64,
            "value must contain 1–64 characters");
    require(std::filesystem::is_directory(options.outputDirectory), "output directory must exist");
    for (const auto* name :
         {"saved.pdf", "resaved.pdf", "before.ppm", "edited.ppm", "reopened.ppm"})
        require(!std::filesystem::exists(options.outputDirectory / name),
                "output file already exists");
    Library library;
    const auto input = readFile(options.input);
    const auto savedPath = options.outputDirectory / "saved.pdf";
    {
        Session original(input);
        // Read calculation before touching input so its exit event cannot mask
        // a missing initial JavaScript calculation.
        original.expect(147, "JS:original");
        original.expect(87, "original");
        std::cout << "initial_javascript=verified\n";
        original.render(options.outputDirectory / "before.ppm");
        original.edit(options.value);
        std::cout << "edited_input_and_exit_javascript=verified\n";
        original.render(options.outputDirectory / "edited.ppm");
        original.save(savedPath);
        std::cout << "save_as_copy=written\n";
    }
    // Read the saved file after all original document/form/page handles close.
    const auto saved = readFile(savedPath);
    {
        Session reopened(saved);
        reopened.expect(147, "JS:" + options.value);
        reopened.expect(87, options.value);
        reopened.render(options.outputDirectory / "reopened.ppm");
        reopened.edit(options.value + "-again");
        reopened.save(options.outputDirectory / "resaved.pdf");
    }
    const auto resaved = readFile(options.outputDirectory / "resaved.pdf");
    {
        Session reopenedAgain(resaved);
        reopenedAgain.expect(147, "JS:" + options.value + "-again");
        reopenedAgain.expect(87, options.value + "-again");
    }
    std::cout << "repeated_save_and_reopen=verified\n";
    std::cout << "PASS: XFA input, JavaScript calculation, save, and reopen verified\n";
}
} // namespace pdf
