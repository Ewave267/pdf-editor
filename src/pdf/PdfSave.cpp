// SPDX-License-Identifier: GPL-3.0-only
#include "NativeFile.h"
#include "PdfDocument.h"
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

void PdfDocument::exportPages(const QUrl& url, const QString& specification, int rotation,
                              bool flattenForms, const QVariantList& mergeFiles)
{
    if (saving_)
        return;
    auto reject = [this](const QString& message)
    {
        saveError_ = message;
        emit saveStateChanged();
        emit saveFinished(false);
    };
    if (!ready_ || formType_ == "XFA")
    {
        reject("Page export supports ordinary PDFs and flattened AcroForms. XFA page export is not "
               "supported.");
        return;
    }
    if (formType_ == "AcroForm" && !flattenForms)
    {
        reject("Select Flatten annotations and forms before changing AcroForm pages.");
        return;
    }
    if (rotation < 0 || rotation > 270 || rotation % 90 || specification.size() > 16000)
    {
        reject("Choose a rotation of 0, 90, 180 or 270 degrees and at most 2,000 pages.");
        return;
    }
    QJsonArray order;
    const auto entries = specification.split(',', Qt::KeepEmptyParts);
    const QRegularExpression range("^([1-9][0-9]*)(?:-([1-9][0-9]*))?$");
    for (const auto& entry : entries)
    {
        const QString token = entry.trimmed();
        if (token == "blank")
            order.append(-1);
        else
        {
            const auto match = range.match(token);
            bool firstOk = false, lastOk = false;
            const int first = match.captured(1).toInt(&firstOk);
            const int last = match.captured(2).isEmpty() ? first : match.captured(2).toInt(&lastOk);
            if (!match.hasMatch() || !firstOk || (!match.captured(2).isEmpty() && !lastOk) ||
                first > pageCount() || last > pageCount())
            {
                reject("Enter page numbers or ranges within this document, separated by commas; "
                       "use blank to insert a blank page.");
                return;
            }
            const int direction = last >= first ? 1 : -1;
            for (int page = first;; page += direction)
            {
                order.append(page - 1);
                if (page == last)
                    break;
            }
        }
        if (order.size() > 2000)
        {
            reject("Export is limited to 2,000 pages.");
            return;
        }
    }
    QByteArray sources;
    QJsonArray lengths;
    QStringList sourcePaths;
    if (mergeFiles.size() > 16)
    {
        reject("Merge is limited to 16 input PDFs and 64 MiB of input data.");
        return;
    }
    for (const auto& item : mergeFiles)
    {
        const QUrl source = item.toUrl();
        QFile file(source.toLocalFile());
        if (!source.isLocalFile() || !pdf::detail::openRegularInput(file, source.toLocalFile()))
        {
            reject("Choose readable local PDF files to merge.");
            return;
        }
        const auto bytes = file.read(64 * 1024 * 1024 - sources.size() + 1);
        if (!bytes.startsWith("%PDF-") || bytes.size() > 64 * 1024 * 1024 - sources.size() ||
            pdf::detail::sameFile(source.toLocalFile(), url.toLocalFile()))
        {
            reject("Merged inputs must be PDFs within the 64 MiB total limit. Choose a destination "
                   "different from every input.");
            return;
        }
        lengths.append(bytes.size());
        sources += bytes;
        sourcePaths.append(QFileInfo(file).absoluteFilePath());
    }
    exportSources_ = sources;
    exportSourcePaths_ = sourcePaths;
    exportOptions_ = {{"mergeLengths", lengths},
                      {"pages", order},
                      {"rotation", rotation / 90},
                      {"flatten", flattenForms}};
    saveAs(url);
    if (!saving_)
    {
        exportOptions_ = {};
        exportSources_.clear();
        exportSourcePaths_.clear();
    }
}

void PdfDocument::saveAs(const QUrl& url)
{
    if (saving_)
        return;
    if (additions_->editing())
    {
        saveError_ = "Finish the current edit before saving.";
        emit saveStateChanged();
        emit saveFinished(false);
        return;
    }
    saveError_.clear();
    const QFileInfo target(url.toLocalFile());
    const bool sameFile = pdf::detail::sameFile(sourcePath_, target.absoluteFilePath());
    if (!ready_ || !url.isLocalFile() || url.toLocalFile().isEmpty() || sameFile ||
        target.absoluteFilePath() == sourcePath_ ||
        (!target.canonicalFilePath().isEmpty() &&
         target.canonicalFilePath() == QFileInfo(sourcePath_).canonicalFilePath()))
    {
        saveError_ = "Choose a new local PDF filename. The source PDF cannot be overwritten.";
        emit saveStateChanged();
        emit saveFinished(false);
        return;
    }
    QByteArray overlay;
    if (additions_->count())
    {
        if (formType_ == "XFA" && xfaFull_)
        {
            saveError_ = "This dynamic XFA document cannot safely save additions yet. Your "
                         "additions are retained. Saving a copy without additions is supported.";
            emit saveStateChanged();
            emit saveFinished(false);
            return;
        }
        QBuffer buffer(&overlay);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setResolution(72);
        writer.setTitle("Added content");
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        auto pageSize = [this](int page)
        {
            const auto info = pages_[page].toMap();
            return QPageSize(QSizeF(info["width"].toDouble(), info["height"].toDouble()),
                             QPageSize::Point, QString(), QPageSize::ExactMatch);
        };
        writer.setPageSize(pageSize(0));
        QPainter painter(&writer);
        painter.setRenderHint(QPainter::LosslessImageRendering);
        if (!painter.isActive())
        {
            saveError_ = "Cannot prepare added content for saving.";
            emit saveStateChanged();
            emit saveFinished(false);
            return;
        }
        for (int page = 0; page < pageCount(); ++page)
        {
            if (page)
            {
                writer.setPageSize(pageSize(page));
                if (!writer.newPage())
                {
                    saveError_ = "Cannot prepare a PDF page.";
                    emit saveStateChanged();
                    emit saveFinished(false);
                    return;
                }
            }
            for (const auto& object : additions_->objects())
                if (object.page == page)
                {
                    AddedContent::paintObject(&painter, object);
                }
        }
        painter.end();
    }
    if (overlay.size() > 32 * 1024 * 1024)
    {
        saveError_ = "The added content is too large to save (32 MiB limit).";
        emit saveStateChanged();
        emit saveFinished(false);
        return;
    }
    saving_ = true;
    saveRevision_ = additions_->revision();
    saveDestination_ = target.absoluteFilePath();
    saveBytes_.clear();
    emit saveStateChanged();
    saveOverlay_ = overlay;
    const quint64 id = ++nextId_;
    queue_.append({id, 0, 0, {}, QJsonObject{{"op", "snapshot"}}});
    nextRequest();
}
void PdfDocument::startSaveWorker(const QByteArray& baseline)
{
    if (!saving_)
        return;
    const QString input = snapshot_->filePath("form-state.pdf");
    QSaveFile snapshot(input);
    if (!snapshot.open(QIODevice::WriteOnly) || snapshot.write(baseline) != baseline.size() ||
        !snapshot.commit())
    {
        finishSave("Cannot prepare the current form state. Your edits are retained.");
        return;
    }
    saveWorker_ = new QProcess(this);
    auto args = sandboxArgs_;
#ifdef Q_OS_LINUX
    const int inputIndex = args.indexOf("/input.pdf") - 1;
#else
    const int marker = args.indexOf("--input");
    const int inputIndex = marker < 0 ? -1 : marker + 1;
#endif
    if (inputIndex < 0 || inputIndex >= args.size())
    {
        finishSave("The save sandbox is not configured.");
        return;
    }
    args[inputIndex] = input;
    args << "--save";
    auto drainDiagnostics = [this]
    {
        if (!saveWorker_)
            return;
        auto diagnostics = saveWorker_->readAllStandardError();
        if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        {
            const int used = saveWorker_->property("diagnosticBytes").toInt();
            diagnostics = diagnostics.left(std::max(0, 4096 - used));
            saveWorker_->setProperty("diagnosticBytes",
                                     used + static_cast<int>(diagnostics.size()));
            if (!diagnostics.isEmpty())
                qWarning().noquote() << "PDF save worker:" << QString::fromUtf8(diagnostics);
        }
    };
    auto reportFailure = [this, drainDiagnostics]
    {
        drainDiagnostics();
        if (saveWorker_ && qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
            qWarning().noquote() << "PDF save worker: process error" << saveWorker_->error()
                                 << saveWorker_->errorString() << "exit" << saveWorker_->exitCode()
                                 << "hex"
                                 << QString::number(static_cast<quint32>(saveWorker_->exitCode()),
                                                    16)
                                 << "status" << saveWorker_->exitStatus();
    };
    connect(saveWorker_, &QProcess::readyReadStandardError, this, drainDiagnostics);
    connect(saveWorker_, &QProcess::readyReadStandardOutput, this,
            [this]
            {
                if (!saveWorker_)
                    return;
                saveBytes_ += saveWorker_->readAllStandardOutput();
                if (saveBytes_.size() > 64 * 1024 * 1024)
                    finishSave("The saved PDF exceeds the 64 MiB limit.");
            });
    connect(saveWorker_, &QProcess::errorOccurred, this,
            [this, reportFailure](QProcess::ProcessError)
            {
                reportFailure();
                finishSave("Cannot run the isolated PDF save worker. Your changes are retained.");
            });
    connect(saveWorker_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, reportFailure, drainDiagnostics](int code, QProcess::ExitStatus status)
            {
                if (!saveWorker_)
                    return;
                drainDiagnostics();
                saveBytes_ += saveWorker_->readAllStandardOutput();
                if (code != 0 || status != QProcess::NormalExit ||
                    !saveBytes_.startsWith("%PDF-") || saveBytes_.size() > 64 * 1024 * 1024)
                {
                    reportFailure();
                    finishSave("The PDF could not be saved. Your changes are retained.");
                    return;
                }
                // Recheck immediately before commit: a destination can change while the worker
                // runs.
                for (const auto& input : exportSourcePaths_)
                    if (pdf::detail::sameFile(input, saveDestination_))
                    {
                        finishSave("Merged input PDFs cannot be overwritten.");
                        return;
                    }
                if (saveDestination_ == sourcePath_ ||
                    pdf::detail::sameFile(sourcePath_, saveDestination_))
                {
                    finishSave("The source PDF cannot be overwritten.");
                    return;
                }
                QSaveFile output(saveDestination_);
                output.setDirectWriteFallback(false);
                if (!output.open(QIODevice::WriteOnly) ||
                    output.write(saveBytes_) != saveBytes_.size() || !output.commit())
                {
                    finishSave("Cannot write the chosen PDF. Check its folder and permissions. "
                               "Your changes are retained.");
                    return;
                }
                if (exportOptions_.isEmpty())
                {
                    savedFormRevision_ = saveFormRevision_;
                    savedRevision_ = saveRevision_;
                    if (!dirty() && !recoveryPath_.isEmpty())
                    {
                        QFile::remove(recoveryPath_);
                        recoveryPath_.clear();
                        emit recoveryChanged();
                    }
                }
                savedPath_ = saveDestination_;
                finishSave({});
            });
    saveDeadline_.start(30000);
#ifdef Q_OS_LINUX
    // Preserve the missing-sandbox failure behavior of native development tests.
    saveWorker_->start(QStandardPaths::findExecutable("bwrap"), args);
#else
#ifdef Q_OS_MACOS
    QProcessEnvironment environment;
    environment.insert("PATH", "/usr/bin:/bin");
    environment.insert("HOME", "/tmp");
    environment.insert("LANG", "en_US.UTF-8");
    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        environment.insert("PDF_EDITOR_WORKER_DIAGNOSTICS", "1");
    environment.insert("QT_PLUGIN_PATH", QCoreApplication::applicationDirPath() + "/../PlugIns");
    saveWorker_->setProcessEnvironment(environment);
#endif
    saveWorker_->start(sandboxProgram_, args);
#endif
    if (saveWorker_)
    {
        if (!exportOptions_.isEmpty())
            saveWorker_->write("PDFEDITOR-EXPORT\n" +
                               QJsonDocument(exportOptions_).toJson(QJsonDocument::Compact) + "\n");
        saveWorker_->write(exportSources_);
        saveWorker_->write(saveOverlay_);
        saveWorker_->closeWriteChannel();
    }
}
void PdfDocument::finishSave(const QString& error)
{
    saveDeadline_.stop();
    if (saveWorker_)
    {
        disconnect(saveWorker_, nullptr, this, nullptr);
        if (saveWorker_->state() != QProcess::NotRunning)
        {
            saveWorker_->kill();
            saveWorker_->waitForFinished(1000);
        }
        saveWorker_->deleteLater();
        saveWorker_ = nullptr;
    }
    saveBytes_.clear();
    saveOverlay_.clear();
    exportOptions_ = {};
    exportSources_.clear();
    exportSourcePaths_.clear();
    saving_ = false;
    saveError_ = error;
    emit saveStateChanged();
    emit saveFinished(error.isEmpty());
}

void PdfDocument::print() { printDocument({}); }
void PdfDocument::printToPdf(const QUrl& url)
{
    if (!url.isLocalFile() || url.toLocalFile().isEmpty())
    {
        saveError_ = "Choose a local PDF filename for printing.";
        emit saveStateChanged();
        emit saveFinished(false);
        return;
    }
    printDocument(url);
}
void PdfDocument::printDocument(const QUrl& destination)
{
    if (!ready_ || saving_ || additions_->editing() || formBusy() ||
        !qobject_cast<QApplication*>(QCoreApplication::instance()))
        return;
    struct Job : QObject
    {
        QPrinter printer{QPrinter::HighResolution};
        QPainter painter;
        int page = 0, last = 0;
        quint64 request = 0;
        QTimer timeout;
        QTemporaryDir outputDirectory;
        QString destination;
        explicit Job(QObject* parent) : QObject(parent) { timeout.setSingleShot(true); }
        ~Job() override
        {
            if (painter.isActive())
                painter.end();
        }
    };
    commitForm();
    auto* job = new Job(this);
    if (destination.isEmpty())
    {
        QPrintDialog dialog(&job->printer);
        dialog.setMinMax(1, pageCount());
        dialog.setOption(QAbstractPrintDialog::PrintPageRange);
        if (dialog.exec() != QDialog::Accepted)
        {
            delete job;
            return;
        }
    }
    else
    {
        job->printer.setOutputFormat(QPrinter::PdfFormat);
        job->printer.setOutputFileName(destination.toLocalFile());
    }
    // A print-to-file destination must not alias the source document.
    const auto target = job->printer.outputFileName();
    if (!target.isEmpty() && (QFileInfo(target).absoluteFilePath() == sourcePath_ ||
                              pdf::detail::sameFile(sourcePath_, target)))
    {
        delete job;
        saveError_ = "Printing cannot overwrite the source PDF.";
        emit saveStateChanged();
        emit saveFinished(false);
        return;
    }
    if (!target.isEmpty())
    {
        if (!job->outputDirectory.isValid())
        {
            delete job;
            return;
        }
        job->destination = target;
        job->printer.setOutputFileName(job->outputDirectory.filePath("printed.pdf"));
    }
    job->page = job->printer.fromPage() > 0 ? job->printer.fromPage() - 1 : 0;
    job->last = job->printer.toPage() > 0 ? job->printer.toPage() - 1 : pageCount() - 1;
    saving_ = true;
    saveError_.clear();
    emit saveStateChanged();
    const auto finish = [this, job](QString error)
    {
        job->timeout.stop();
        cancelRender(job->request);
        if (job->painter.isActive())
            job->painter.end();
        if (error.isEmpty() && !job->destination.isEmpty())
        {
            QFile input;
            if (pdf::detail::sameFile(sourcePath_, job->destination) ||
                !pdf::detail::openRegularInput(input, job->printer.outputFileName()))
                error = "Cannot safely write the printed PDF.";
            else
            {
                const auto bytes = input.read(64 * 1024 * 1024 + 1);
                QSaveFile output(job->destination);
                output.setDirectWriteFallback(false);
                if (bytes.size() > 64 * 1024 * 1024 || !bytes.startsWith("%PDF-") ||
                    !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
                    !output.commit())
                    error = "Cannot write the printed PDF.";
            }
        }
        disconnect(this, nullptr, job, nullptr);
        job->deleteLater();
        saving_ = false;
        saveError_ = error;
        emit saveStateChanged();
        emit saveFinished(error.isEmpty());
    };
    connect(&job->timeout, &QTimer::timeout, job,
            [finish] { finish("Printing timed out. Your edits are retained."); });
    connect(this, &PdfDocument::stateChanged, job,
            [this, finish]
            {
                if (!ready_)
                    finish("Printing cancelled.");
            });
    const auto request = [this, job]
    {
        job->timeout.start(30000);
        job->request = requestRender(job->page, 2400);
    };
    connect(this, &PdfDocument::rendered, job,
            [this, job, finish, request](quint64 id, const QImage& image)
            {
                if (id != job->request)
                    return;
                const auto info = pages_[job->page].toMap();
                const QSizeF size(info["width"].toDouble(), info["height"].toDouble());
                job->printer.setPageSize(
                    QPageSize(size, QPageSize::Point, QString(), QPageSize::ExactMatch));
                if ((!job->painter.isActive() && !job->painter.begin(&job->printer)) ||
                    (job->painter.isActive() &&
                     job->page > (job->printer.fromPage() > 0 ? job->printer.fromPage() - 1 : 0) &&
                     !job->printer.newPage()))
                {
                    finish("Cannot print the PDF page.");
                    return;
                }
                const QRectF area = job->printer.pageRect(QPrinter::DevicePixel);
                job->painter.drawImage(area, image);
                job->painter.save();
                job->painter.translate(area.topLeft());
                job->painter.scale(area.width() / size.width(), area.height() / size.height());
                for (const auto& object : additions_->objects())
                    if (object.page == job->page)
                        AddedContent::paintObject(&job->painter, object);
                job->painter.restore();
                if (job->page++ >= job->last)
                    finish({});
                else
                    request();
            });
    request();
}
