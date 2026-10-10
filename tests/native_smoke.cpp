// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
QJsonArray timings;
void require(bool value, const QString& message)
{
    if (!value)
        throw std::runtime_error(message.toStdString());
}
void wait(const std::function<bool()>& condition, const QString& operation, int timeout = 30000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
    }
    require(condition(), "Timed out: " + operation);
}
void ready(PdfDocument& document)
{
    QElapsedTimer elapsed;
    elapsed.start();
    wait([&] { return document.ready() || !document.error().isEmpty(); }, "open PDF");
    require(document.ready(), document.error());
    require(document.pageCount() > 0, "No pages");
    timings.append(QJsonObject{{"operation", "open"},
                               {"milliseconds", static_cast<double>(elapsed.elapsed())},
                               {"pages", document.pageCount()}});
}
void render(PdfDocument& document)
{
    QElapsedTimer elapsed;
    elapsed.start();
    bool received = false;
    const auto connection =
        QObject::connect(&document, &PdfDocument::rendered,
                         [&](quint64, const QImage& image) { received = !image.isNull(); });
    require(document.requestRender(0, 612) != 0, "Render request failed");
    wait([&] { return received || !document.ready(); }, "render page");
    QObject::disconnect(connection);
    require(received, "Rendering failed: " + document.error());
    timings.append(QJsonObject{{"operation", "render"},
                               {"milliseconds", static_cast<double>(elapsed.elapsed())},
                               {"width", 612}});
}
void event(PdfDocument& document, const QString& action, double x = 0, double y = 0, int key = 0,
           const QString& text = {})
{
    bool done = false, handled = false;
    const auto connection = QObject::connect(&document, &PdfDocument::formEventFinished,
                                             [&](quint64, bool result)
                                             {
                                                 done = true;
                                                 handled = result;
                                             });
    require(document.formEvent(0, action, x, y, key, text) != 0, "Form event not queued");
    wait([&] { return done || !document.ready(); }, "form event");
    QObject::disconnect(connection);
    require(done && handled && document.formError().isEmpty(),
            "Form event failed: " + document.formError());
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("PDF_EDITOR_WORKER_DIAGNOSTICS", "1");
    QApplication app(argc, argv);
    try
    {
        require(!QFontDatabase::families().isEmpty(), "Validation has no fonts for added text");
        const auto arguments = app.arguments();
        require(arguments.size() == 3, "Usage: native-smoke FIXTURES_DIRECTORY OUTPUT_DIRECTORY");
        const QDir fixtures(arguments[1]), output(arguments[2]);
        require(QDir().mkpath(output.absolutePath()), "Cannot create validation output directory");
#ifdef Q_OS_WIN
        if (!qEnvironmentVariable("PDF_EDITOR_TEST_PREWARM").isEmpty())
        {
            PdfDocument idle;
            idle.prepareWindowsWorker();
            if (qEnvironmentVariable("PDF_EDITOR_TEST_PREWARM") == "ready")
                wait([&] { return idle.windowsWorkerPrepared() || !idle.error().isEmpty(); },
                     "warm idle worker before closing", 60000);
            require(idle.error().isEmpty(), idle.error());
            require(!idle.ready() && !idle.loading(), "Idle warm-up appeared as an open PDF");
            idle.close();
            require(idle.workerPid() == 0 && !idle.windowsWorkerPrepared(),
                    "Closing retained the idle worker");
        }
#endif
        for (const QString& kind : {QString("normal"), QString("acroform"), QString("xfa")})
        {
            const QString source = fixtures.filePath(kind == "normal" ? "normal/single-page.pdf"
                                                     : kind == "xfa"  ? "xfa-dynamic/controls.pdf"
                                                                      : "acroform/controls.pdf");
            QFile original(source);
            require(original.open(QIODevice::ReadOnly), "Fixture is missing");
            const QByteArray originalBytes = original.readAll();
            original.close();
            PdfDocument document;
            const auto prewarm = qEnvironmentVariable("PDF_EDITOR_TEST_PREWARM");
            if (!prewarm.isEmpty())
            {
                document.prepareWindowsWorker();
                if (prewarm == "ready")
                    wait(
                        [&]
                        { return document.windowsWorkerPrepared() || !document.error().isEmpty(); },
                        "prepare idle Windows renderer", 60000);
                require(document.error().isEmpty(), document.error());
            }
            const auto preparedPid = document.workerPid();
            document.open(QUrl::fromLocalFile(source));
            ready(document);
            if (!prewarm.isEmpty() && preparedPid > 0)
                require(document.workerPid() == preparedPid, "Open replaced the prepared worker");
            render(document);
            if (kind == "normal")
                require(document.additions()->addText(0, 40, 120, "Native release smoke test") > 0,
                        "Cannot add text to the normal PDF");
            else
            {
                const bool xfa = kind == "xfa";
                event(document, "click", 100, 87);
                event(document, "selectAll");
                event(document, "text", 0, 0, 0, "edited");
                event(document, "key", 0, 0, 9);
                event(document, "click", 82, xfa ? 212 : 150);
                event(document, "click", 152, xfa ? 272 : 212);
                event(document, "click", xfa ? 268 : 262, xfa ? 337 : 275);
                render(document);
                event(document, "key", 0, 0, 40);
                event(document, "key", 0, 0, 13);
                event(document, "blur");
            }
            if (kind == "normal")
            {
                document.printToPdf(QUrl::fromLocalFile(output.filePath("printed.pdf")));
                wait([&] { return !document.saving(); }, "print to PDF", 45000);
                require(document.saveError().isEmpty() &&
                            QFile::exists(output.filePath("printed.pdf")) && document.dirty(),
                        "Print-to-PDF failed or marked edits saved");
            }
            for (int generation = 1; generation <= 2; ++generation)
            {
                const QString path =
                    output.filePath(kind + "-" + QString::number(generation) + ".pdf");
                document.saveAs(QUrl::fromLocalFile(path));
                wait([&] { return !document.saving(); }, "save PDF", 45000);
                require(document.saveError().isEmpty(), document.saveError());
                QFile saved(path);
                require(saved.open(QIODevice::ReadOnly) && saved.read(5) == "%PDF-",
                        "Saved PDF is invalid");
                document.close();
                document.open(QUrl::fromLocalFile(path));
                ready(document);
                render(document);
            }
            document.close();
            require(original.open(QIODevice::ReadOnly) && original.readAll() == originalBytes,
                    "Source fixture was modified");
            std::cout << "PASS: native " << kind.toStdString() << " edit/save/reopen twice\n";
        }
        {
            PdfDocument workflow;
            workflow.open(QUrl::fromLocalFile(fixtures.filePath("normal/single-page.pdf")));
            ready(workflow);
            require(workflow.additions()->addText(0, 40, 120, "Export retained") > 0,
                    "Cannot add export text");
            const auto exported = output.filePath("pages-export.pdf");
            workflow.exportPages(
                QUrl::fromLocalFile(exported), "1,blank,1", 90, false,
                {QUrl::fromLocalFile(fixtures.filePath("normal/single-page.pdf"))});
            wait([&] { return !workflow.saving(); }, "export pages", 45000);
            require(workflow.saveError().isEmpty() && workflow.dirty(),
                    "Export failed or marked source saved");
            workflow.close();
            workflow.open(QUrl::fromLocalFile(exported));
            ready(workflow);
            require(workflow.pageCount() == 4, "Incorrect exported page count");
            workflow.search("retained");
            wait([&] { return !workflow.searching(); }, "search exported content");
            require(workflow.searchResults().size() == 2, "Added text lost during page export");
            render(workflow);
            workflow.close();
            workflow.open(QUrl::fromLocalFile(fixtures.filePath("acroform/text.pdf")));
            ready(workflow);
            event(workflow, "click", 100, 152);
            event(workflow, "selectAll");
            event(workflow, "text", 0, 0, 0, "mixed Case");
            require(workflow.formText() == "MIXED CASE", "AcroForm keystroke conversion failed");
            workflow.exportPages(QUrl::fromLocalFile(output.filePath("forms-flat.pdf")), "1", 0,
                                 true);
            wait([&] { return !workflow.saving(); }, "flatten AcroForm", 45000);
            require(workflow.saveError().isEmpty(), workflow.saveError());
            workflow.close();
            workflow.open(QUrl::fromLocalFile(output.filePath("forms-flat.pdf")));
            ready(workflow);
            require(workflow.formType() == "PDF", "Flattened form remains interactive");
            workflow.search("MIXED CASE");
            wait([&] { return !workflow.searching(); }, "search flattened value");
            require(workflow.searchResults().size() == 1, "Flattened form value lost");
            render(workflow);
        }
        std::cout << "PASS: page export, merging, rotation, searchable additions and AcroForm "
                     "flattening\n";
        PdfDocument invalid;
        invalid.open(QUrl::fromLocalFile(fixtures.filePath("malformed/not-a-pdf.pdf")));
        wait([&] { return !invalid.loading(); }, "reject malformed PDF");
        require(!invalid.ready() && !invalid.error().isEmpty(), "Malformed PDF was accepted");
        invalid.open(QUrl::fromLocalFile(fixtures.filePath("normal/single-page.pdf")));
        ready(invalid);
        render(invalid);
        std::cout << "PASS: malformed-document rejection and recovery\n";
        QFile performance(output.filePath("performance.json"));
        require(performance.open(QIODevice::WriteOnly), "Cannot write timing report");
        performance.write(
            QJsonDocument(QJsonObject{{"architecture", QSysInfo::currentCpuArchitecture()},
                                      {"kernel", QSysInfo::kernelType()},
                                      {"qt", QString(qVersion())},
                                      {"measurements", timings}})
                .toJson());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Native release validation: " << error.what() << '\n';
        return 1;
    }
}
