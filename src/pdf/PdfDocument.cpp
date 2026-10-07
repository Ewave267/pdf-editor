// SPDX-License-Identifier: GPL-3.0-only
#include "PdfDocument.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

PdfDocument::PdfDocument(QObject* parent) : QObject(parent), additions_(new AddedContent(this))
{
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this]
            { fail("The document took too long to respond. Close it and try another PDF."); });
}
PdfDocument::~PdfDocument() { stopWorker(); }

qint64 PdfDocument::workerPid() const { return worker_ ? worker_->processId() : 0; }

double PdfDocument::maxPageWidth() const
{
    double width = 1;
    for (const auto& page : pages_)
        width = std::max(width, page.toMap().value("width").toDouble());
    return width;
}

void PdfDocument::stopWorker()
{
    deadline_.stop();
    if (worker_)
    {
        disconnect(worker_, nullptr, this, nullptr);
        worker_->closeWriteChannel();
        if (!worker_->waitForFinished(100))
        {
            worker_->kill();
            worker_->waitForFinished(1000);
        }
        worker_->deleteLater();
        worker_ = nullptr;
    }
    incoming_.clear();
    diagnostics_.clear();
    queue_.clear();
    active_ = {};
    cache_.clear();
    ++generation_;
}

void PdfDocument::close()
{
    additions_->clear();
    stopWorker();
    ready_ = loading_ = false;
    pages_.clear();
    error_.clear();
    fileName_.clear();
    formType_.clear();
    currentPage_ = 1;
    emit stateChanged();
    emit currentPageChanged();
}

void PdfDocument::fail(const QString& message)
{
    close();
    error_ = message;
    emit stateChanged();
}

void PdfDocument::open(const QUrl& url)
{
    const QFileInfo input(url.toLocalFile());
    if (!url.isLocalFile() || !input.isFile() || !input.isReadable())
    {
        fail("Select a readable PDF on this computer.");
        return;
    }
    close();
    fileName_ = input.fileName();
    loading_ = true;
    fitting_ = true;
    emit stateChanged();
    const QString bwrap = QStandardPaths::findExecutable("bwrap");
    const QString binary = QCoreApplication::applicationDirPath() + "/pdf-render-worker";
    if (bwrap.isEmpty() || !QFileInfo::exists(binary) || !QFileInfo::exists(PDFIUM_LIBRARY_PATH))
    {
        fail("The PDF renderer or sandbox is missing. Check the viewer installation.");
        return;
    }
    QStringList args{"--unshare-all", "--die-with-parent",
                     "--new-session", "--cap-drop",
                     "ALL",           "--clearenv",
                     "--setenv",      "LD_LIBRARY_PATH",
                     "/pdfium",       "--setenv",
                     "HOME",          "/tmp",
                     "--setenv",      "LANG",
                     "C.UTF-8",       "--ro-bind",
                     "/usr",          "/usr",
                     "--symlink",     "usr/lib",
                     "/lib",          "--symlink",
                     "usr/lib64",     "/lib64",
                     "--proc",        "/proc",
                     "--dev",         "/dev",
                     "--tmpfs",       "/tmp"};
    for (const QString& path : {QString("/etc/fonts"), QString("/var/cache/fontconfig")})
        if (QFileInfo::exists(path))
            args << "--ro-bind" << path << path;
    args << "--ro-bind" << binary << "/probe"
         << "--ro-bind" << QString(PDFIUM_LIBRARY_PATH) << "/pdfium/libpdfium.so"
         << "--ro-bind" << input.canonicalFilePath() << "/input.pdf"
         << "--chdir" << "/tmp" << "/probe";
    worker_ = new QProcess(this);
    worker_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(worker_, &QProcess::readyReadStandardOutput, this, &PdfDocument::receive);
    // Drain diagnostics so a noisy parser cannot fill its stderr pipe.
    connect(worker_, &QProcess::readyReadStandardError, this,
            [this]
            {
                if (worker_)
                    diagnostics_ += worker_->readAllStandardError().left(
                        std::max(0, 4096 - static_cast<int>(diagnostics_.size())));
            });
    connect(worker_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { fail("Cannot start the isolated PDF renderer."); });
    connect(worker_, &QProcess::finished, this,
            [this](int, QProcess::ExitStatus)
            {
                fail(loading_ && diagnostics_.contains("bwrap:")
                         ? "Your system could not start the PDF sandbox. Check the viewer setup "
                           "instructions."
                         : "The isolated PDF renderer stopped unexpectedly.");
            });
    deadline_.start(15000);
    worker_->start(bwrap, args);
}

void PdfDocument::receive()
{
    incoming_ += worker_->readAllStandardOutput();
    if (incoming_.size() > 64 * 1024 * 1024)
    {
        fail("The PDF renderer returned too much data.");
        return;
    }
    const auto end = incoming_.indexOf('\n');
    if (end < 0)
        return;
    const auto line = incoming_.first(end);
    incoming_.remove(0, end + 1);
    QJsonParseError parseError;
    const auto response = QJsonDocument::fromJson(line, &parseError).object();
    if (parseError.error != QJsonParseError::NoError || response.isEmpty())
    {
        fail("The PDF renderer returned an invalid response.");
        return;
    }
    deadline_.stop();
    if (response.contains("error"))
    {
        fail(response.value("error").toString("Cannot render this PDF."));
        return;
    }
    if (loading_)
    {
        const auto pages = response.value("pages").toArray();
        if (pages.isEmpty() || pages.size() > 2000)
        {
            fail("The PDF renderer returned an invalid page count.");
            return;
        }
        for (const auto& value : pages)
        {
            const auto page = value.toObject();
            const double width = page.value("width").toDouble(),
                         height = page.value("height").toDouble();
            if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
                width > 14400 || height > 14400)
            {
                fail("The PDF renderer returned an invalid page size.");
                return;
            }
            pages_.append(page.toVariantMap());
        }
        const int type = response.value("formType").toInt();
        formType_ = type == 0 ? "PDF" : type == 1 ? "AcroForm" : "XFA";
        loading_ = false;
        ready_ = true;
        updateFit();
        emit stateChanged();
        return;
    }
    if (!active_.id || response.value("id").toDouble() != static_cast<double>(active_.id))
    {
        fail("The PDF renderer returned an unexpected page response.");
        return;
    }
    const auto bytes = QByteArray::fromBase64(response.value("png").toString().toLatin1());
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const auto size = reader.size();
    const auto info = pages_[active_.page].toMap();
    const int expectedHeight = static_cast<int>(std::ceil(
        active_.width * info.value("height").toDouble() / info.value("width").toDouble()));
    if (size.width() != active_.width || size.height() != expectedHeight ||
        static_cast<qint64>(size.width()) * size.height() > 16000000)
    {
        fail("The PDF renderer returned an invalid image size.");
        return;
    }
    const auto image = reader.read();
    if (image.isNull())
    {
        fail("Cannot display the rendered page.");
        return;
    }
    cache_.insert(active_.key(), new QImage(image),
                  std::max(1, static_cast<int>(image.sizeInBytes() / 1024)));
    const auto listeners = active_.listeners;
    active_ = {};
    for (const auto id : listeners)
        emit rendered(id, image);
    nextRequest();
}

quint64 PdfDocument::requestRender(int page, int width)
{
    if (!ready_ || page < 0 || page >= pageCount())
        return 0;
    width = std::clamp(width, 96, 2400);
    const quint64 id = ++nextId_;
    const QString key = QString::number(page) + "/" + QString::number(width);
    if (const auto* cached = cache_.object(key))
    {
        const QImage image = *cached;
        const auto generation = generation_;
        QMetaObject::invokeMethod(
            this,
            [this, generation, id, image]
            {
                if (generation == generation_)
                    emit rendered(id, image);
            },
            Qt::QueuedConnection);
        return id;
    }
    if (active_.id && active_.key() == key)
        active_.listeners.append(id);
    else
    {
        auto it = std::find_if(queue_.begin(), queue_.end(),
                               [&key](const auto& request) { return request.key() == key; });
        if (it != queue_.end())
            it->listeners.append(id);
        else
            queue_.append({id, page, width, {id}});
    }
    nextRequest();
    return id;
}

void PdfDocument::cancelRender(quint64 id)
{
    active_.listeners.removeAll(id);
    for (auto it = queue_.begin(); it != queue_.end();)
    {
        it->listeners.removeAll(id);
        if (it->listeners.isEmpty())
            it = queue_.erase(it);
        else
            ++it;
    }
}

void PdfDocument::nextRequest()
{
    if (!worker_ || active_.id || queue_.isEmpty())
        return;
    active_ = queue_.takeFirst();
    const QJsonObject request{
        {"id", static_cast<double>(active_.id)}, {"page", active_.page}, {"width", active_.width}};
    worker_->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    deadline_.start(10000);
}

void PdfDocument::setCurrentPage(int page)
{
    page = std::clamp(page, 1, std::max(1, pageCount()));
    if (page == currentPage_)
        return;
    currentPage_ = page;
    emit currentPageChanged();
}
void PdfDocument::setZoom(double zoom)
{
    if (!std::isfinite(zoom))
        return;
    fitting_ = false;
    zoom_ = std::clamp(zoom, 0.25, 3.0);
    emit zoomChanged();
}
void PdfDocument::fitToPage()
{
    fitting_ = true;
    updateFit();
}
void PdfDocument::updateViewport(double width, double height)
{
    viewportWidth_ = std::max(100.0, width);
    viewportHeight_ = std::max(100.0, height);
    if (fitting_)
        updateFit();
}
void PdfDocument::updateFit()
{
    double maxHeight = 1;
    for (const auto& page : pages_)
        maxHeight = std::max(maxHeight, page.toMap().value("height").toDouble());
    zoom_ = std::clamp(
        std::min((viewportWidth_ - 48) / maxPageWidth(), (viewportHeight_ - 32) / maxHeight), 0.1,
        3.0);
    emit zoomChanged();
}
