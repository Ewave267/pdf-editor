// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
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
    wait([&] { return document.ready() || !document.error().isEmpty(); }, "open PDF");
    require(document.ready(), document.error());
    require(document.pageCount() > 0, "No pages");
}
void render(PdfDocument& document)
{
    bool received = false;
    const auto connection =
        QObject::connect(&document, &PdfDocument::rendered,
                         [&](quint64, const QImage& image) { received = !image.isNull(); });
    require(document.requestRender(0, 612) != 0, "Render request failed");
    wait([&] { return received || !document.ready(); }, "render page");
    QObject::disconnect(connection);
    require(received, "Rendering failed: " + document.error());
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
    QGuiApplication app(argc, argv);
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
        PdfDocument invalid;
        invalid.open(QUrl::fromLocalFile(fixtures.filePath("malformed/not-a-pdf.pdf")));
        wait([&] { return !invalid.loading(); }, "reject malformed PDF");
        require(!invalid.ready() && !invalid.error().isEmpty(), "Malformed PDF was accepted");
        invalid.open(QUrl::fromLocalFile(fixtures.filePath("normal/single-page.pdf")));
        ready(invalid);
        render(invalid);
        std::cout << "PASS: malformed-document rejection and recovery\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Native release validation: " << error.what() << '\n';
        return 1;
    }
}
