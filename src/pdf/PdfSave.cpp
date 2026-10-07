// SPDX-License-Identifier: GPL-3.0-only
#include "PdfDocument.h"
#include <QBuffer>
#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QStandardPaths>
#include <sys/stat.h>

void PdfDocument::saveAs(const QUrl& url)
{
    if (saving_)
        return;
    saveError_.clear();
    const QFileInfo target(url.toLocalFile());
    struct stat sourceStat{}, targetStat{};
    const bool sameFile =
        ::stat(sourcePath_.toLocal8Bit().constData(), &sourceStat) == 0 &&
        ::stat(target.absoluteFilePath().toLocal8Bit().constData(), &targetStat) == 0 &&
        sourceStat.st_dev == targetStat.st_dev && sourceStat.st_ino == targetStat.st_ino;
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
                    painter.save();
                    painter.setClipRect(object.rect);
                    if (object.type == "text")
                    {
                        QFont font("Sans Serif");
                        font.setPixelSize(18);
                        painter.setFont(font);
                        painter.setPen(Qt::black);
                        painter.drawText(object.rect,
                                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                         object.text);
                    }
                    else
                        painter.drawImage(object.rect, object.image);
                    painter.restore();
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
    const int inputIndex = args.indexOf("/input.pdf");
    if (inputIndex < 1)
    {
        finishSave("The save sandbox is not configured.");
        return;
    }
    args[inputIndex - 1] = input;
    args << "--save";
    connect(saveWorker_, &QProcess::readyReadStandardError, this,
            [this]
            {
                if (saveWorker_)
                    saveWorker_->readAllStandardError();
            });
    connect(saveWorker_, &QProcess::readyReadStandardOutput, this,
            [this]
            {
                if (!saveWorker_)
                    return;
                saveBytes_ += saveWorker_->readAllStandardOutput();
                if (saveBytes_.size() > 64 * 1024 * 1024)
                    finishSave("The saved PDF exceeds the 64 MiB limit.");
            });
    connect(saveWorker_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError)
            { finishSave("Cannot run the isolated PDF save worker. Your changes are retained."); });
    connect(saveWorker_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status)
            {
                if (!saveWorker_)
                    return;
                saveBytes_ += saveWorker_->readAllStandardOutput();
                if (code != 0 || status != QProcess::NormalExit ||
                    !saveBytes_.startsWith("%PDF-") || saveBytes_.size() > 64 * 1024 * 1024)
                {
                    finishSave("The PDF could not be saved. Your changes are retained.");
                    return;
                }
                // Recheck immediately before commit: a destination can change while the worker
                // runs.
                struct stat original{}, destination{};
                if (saveDestination_ == sourcePath_ ||
                    (::stat(sourcePath_.toLocal8Bit().constData(), &original) == 0 &&
                     ::stat(saveDestination_.toLocal8Bit().constData(), &destination) == 0 &&
                     original.st_dev == destination.st_dev &&
                     original.st_ino == destination.st_ino))
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
                savedFormRevision_ = saveFormRevision_;
                savedRevision_ = saveRevision_;
                savedPath_ = saveDestination_;
                finishSave({});
            });
    saveDeadline_.start(30000);
    saveWorker_->start(QStandardPaths::findExecutable("bwrap"), args);
    if (saveWorker_)
    {
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
    saving_ = false;
    saveError_ = error;
    emit saveStateChanged();
    emit saveFinished(error.isEmpty());
}
