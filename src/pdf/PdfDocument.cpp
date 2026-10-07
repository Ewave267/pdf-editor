// SPDX-License-Identifier: GPL-3.0-only
#include "PdfDocument.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

PdfDocument::PdfDocument(QObject* parent) : QObject(parent), additions_(new AddedContent(this))
{
    savedRevision_ = additions_->revision();
    connect(additions_, &AddedContent::changed, this, &PdfDocument::saveStateChanged);
    saveDeadline_.setSingleShot(true);
    connect(&saveDeadline_, &QTimer::timeout, this,
            [this] { finishSave("Saving took too long. Your changes are retained."); });
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this]
            { fail("The document took too long to respond. Close it and try another PDF."); });
}
PdfDocument::~PdfDocument()
{
    if (saving_)
        finishSave("Save cancelled.");
    stopWorker();
}

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
    if (saving_)
        finishSave("Save cancelled.");
    additions_->clear();
    savedRevision_ = additions_->revision();
    formRevision_ = savedFormRevision_ = saveFormRevision_ = 0;
    pendingFormEvents_ = 0;
    formFieldType_ = -1;
    formText_.clear();
    formError_.clear();
    emit formsChanged();
    saveError_.clear();
    savedPath_.clear();
    snapshot_.reset();
    sourcePath_.clear();
    emit saveStateChanged();
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
    const bool lostFormEdits = ready_ && (formRevision_ != savedFormRevision_ || formBusy());
    if (saving_)
        finishSave(message);
    stopWorker();
    // Application-owned additions survive a parser crash; native in-memory form
    // state cannot be recovered. Make that loss explicit instead of clearing edits.
    if (lostFormEdits)
        ++formRevision_;
    pendingFormEvents_ = 0;
    ready_ = loading_ = false;
    pages_.clear();
    formText_.clear();
    formFieldType_ = -1;
    error_ = message;
    if (lostFormEdits)
        error_ += " Unsaved form edits could not be recovered. The source PDF is unchanged.";
    if (additions_->count())
        error_ += " Added content is retained until you close or replace the document.";
    emit formsChanged();
    emit saveStateChanged();
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
    sourcePath_ = input.canonicalFilePath();
    if (input.size() <= 0 || input.size() > 64 * 1024 * 1024)
    {
        fail("The viewer supports PDF files up to 64 MiB.");
        return;
    }
    snapshot_ = std::make_unique<QTemporaryDir>();
    QFile source, copy(snapshot_->filePath("input.pdf"));
    const int descriptor =
        ::open(sourcePath_.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat opened{};
    if (descriptor < 0 || ::fstat(descriptor, &opened) != 0 || !S_ISREG(opened.st_mode) ||
        opened.st_size <= 0 || opened.st_size > 64 * 1024 * 1024)
    {
        if (descriptor >= 0)
            ::close(descriptor);
        fail("Select a regular PDF file of at most 64 MiB.");
        return;
    }
    if (!source.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
    {
        ::close(descriptor);
        fail("Cannot read the selected PDF.");
        return;
    }
    if (!snapshot_->isValid() || !copy.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        fail("Cannot create a private document snapshot.");
        return;
    }
    qint64 copied = 0;
    while (!source.atEnd())
    {
        const auto block = source.read(65536);
        copied += block.size();
        if (block.isEmpty() || copied > 64 * 1024 * 1024 || copy.write(block) != block.size())
        {
            copy.close();
            fail("Cannot snapshot this PDF within the 64 MiB input limit.");
            return;
        }
    }
    copy.close();
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
                     "--size",        "67108864",
                     "--tmpfs",       "/tmp"};
    for (const QString& path : {QString("/etc/fonts"), QString("/var/cache/fontconfig")})
        if (QFileInfo::exists(path))
            args << "--ro-bind" << path << path;
    args << "--ro-bind" << binary << "/probe"
         << "--ro-bind" << QString(PDFIUM_LIBRARY_PATH) << "/pdfium/libpdfium.so"
         << "--ro-bind" << snapshot_->filePath("input.pdf") << "/input.pdf"
         << "--chdir" << "/tmp" << "/probe";
    sandboxArgs_ = args;
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
    if (incoming_.size() > 96 * 1024 * 1024)
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
    if (response.contains("error") && !active_.command.isEmpty())
    {
        const auto id = active_.id;
        const auto op = active_.command.value("op").toString();
        active_ = {};
        if (op == "snapshot")
            finishSave(response["error"].toString());
        else
        {
            --pendingFormEvents_;
            formError_ = response["error"].toString();
            emit formsChanged();
            emit saveStateChanged();
            emit formEventFinished(id, false);
        }
        nextRequest();
        return;
    }
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
            if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
                width > 14400 || height > 14400)
            {
                fail("The PDF renderer returned an invalid page size.");
                return;
            }
            pages_.append(page.toVariantMap());
        }
        const int type = response.value("formType").toInt();
        xfaFull_ = type == 2;
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
    if (!active_.command.isEmpty())
    {
        const auto command = active_.command;
        const auto id = active_.id;
        active_ = {};
        if (command["op"].toString() == "snapshot")
        {
            const QByteArray baseline =
                QByteArray::fromBase64(response["pdf"].toString().toLatin1());
            if (!baseline.startsWith("%PDF-") || baseline.size() > 64 * 1024 * 1024)
                finishSave("The renderer returned invalid PDF data. Your edits are retained.");
            else
            {
                if (response["changed"].toBool())
                    ++formRevision_;
                saveFormRevision_ = formRevision_;
                startSaveWorker(baseline);
            }
        }
        else
        {
            if (command["action"].toString() == "copy")
                QGuiApplication::clipboard()->setText(response["selectedText"].toString());
            --pendingFormEvents_;
            formText_ = response["text"].toString();
            formFieldType_ = response["fieldType"].toInt(-1);
            formError_.clear();
            if (response["changed"].toBool())
                ++formRevision_;
            cache_.clear();
            emit formsChanged();
            emit saveStateChanged();
            emit formRepaint();
            emit formEventFinished(id, response["handled"].toBool());
        }
        nextRequest();
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
            queue_.append({id, page, width, {id}, {}});
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
        if (it->command.isEmpty() && it->listeners.isEmpty())
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
    QJsonObject request = active_.command;
    if (request.isEmpty())
        request = {{"page", active_.page}, {"width", active_.width}};
    request.insert("id", static_cast<double>(active_.id));
    worker_->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    deadline_.start(active_.command.value("op").toString() == "snapshot" ? 30000 : 10000);
}

quint64 PdfDocument::formEvent(int page, const QString& action, double x, double y, int key,
                               const QString& text, int flags)
{
    if (!ready_ || saving_ || page < 0 || page >= pageCount() || formType_ == "PDF" ||
        text.size() > 4096)
        return 0;
    if (pendingFormEvents_ >= 128)
    {
        formError_ = "The form is busy. Wait for pending input to finish.";
        emit formsChanged();
        return 0;
    }
    const QStringList allowed{"click", "text", "key", "selectAll", "blur", "copy"};
    if (!allowed.contains(action))
        return 0;
    const quint64 id = ++nextId_;
    formPage_ = page;
    QJsonObject command{{"op", "event"}, {"page", page}, {"action", action}, {"x", x},
                        {"y", y},        {"key", key},   {"text", text},     {"flags", flags}};
    queue_.append({id, page, 0, {}, command});
    ++pendingFormEvents_;
    emit formsChanged();
    emit saveStateChanged();
    nextRequest();
    return id;
}
void PdfDocument::commitForm() { formEvent(formPage_, "blur"); }

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
