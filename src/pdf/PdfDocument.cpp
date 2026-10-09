// SPDX-License-Identifier: GPL-3.0-only
#include "PdfDocument.h"
#include "NativeFile.h"
#include <QDate>
#include <QDateTime>

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

PdfDocument::PdfDocument(QObject* parent) : QObject(parent), additions_(new AddedContent(this))
{
    performanceClock_.start();
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
#ifdef Q_OS_WIN
        constexpr int cleanupGraceMs = 2000; // Broker also removes its copied runtime.
#else
        constexpr int cleanupGraceMs = 100;
#endif
        if (!worker_->waitForFinished(cleanupGraceMs))
        {
            if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
                qWarning() << "PDF timing: forcing worker shutdown after cleanup grace_ms"
                           << cleanupGraceMs;
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
    cachedRequests_.clear();
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
    formFields_.clear();
    focusedField_ = -1;
    formPage_ = 0;
    fieldsComplete_ = formValidated_ = false;
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
    formFields_.clear();
    focusedField_ = -1;
    formPage_ = 0;
    fieldsComplete_ = formValidated_ = false;
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
    const auto reject = [this](const QString& message)
    {
        // Preflight failures must not destroy the document or its unsaved edits.
        error_ = message;
        emit stateChanged();
    };
    const QFileInfo input(url.toLocalFile());
    if (!url.isLocalFile() || !input.isFile() || !input.isReadable())
    {
        reject("Select a readable PDF on this computer.");
        return;
    }
    const QString sourcePath = input.canonicalFilePath();
    if (input.size() <= 0 || input.size() > 64 * 1024 * 1024)
    {
        reject("The viewer supports PDF files up to 64 MiB.");
        return;
    }
    auto snapshot = std::make_unique<QTemporaryDir>();
    QFile source, copy(snapshot->filePath("input.pdf"));
    if (!pdf::detail::openRegularInput(source, sourcePath))
    {
        reject("Select a regular PDF file of at most 64 MiB.");
        return;
    }
    if (!snapshot->isValid() || !copy.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        reject("Cannot create a private document snapshot.");
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
            reject("Cannot snapshot this PDF within the 64 MiB input limit.");
            return;
        }
    }
    copy.close();
    source.close();
    close();
    snapshot_ = std::move(snapshot);
    sourcePath_ = sourcePath;
    fileName_ = input.fileName();
    loading_ = true;
    fitting_ = true;
    emit stateChanged();
#ifdef Q_OS_LINUX
    const QString bwrap = QStandardPaths::findExecutable("bwrap");
    const QString binary = QCoreApplication::applicationDirPath() + "/pdf-render-worker";
    const QString installedLibrary = QDir(QCoreApplication::applicationDirPath())
                                         .absoluteFilePath("../lib/pdf-form-editor/libpdfium.so");
    const QString library =
        QFileInfo::exists(installedLibrary) ? installedLibrary : QString(PDFIUM_LIBRARY_PATH);
    if (bwrap.isEmpty() || !QFileInfo::exists(binary) || !QFileInfo::exists(library))
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
                     "--dir",         "/proc",
                     "--dir",         "/dev",
                     "--size",        "67108864",
                     "--tmpfs",       "/tmp"};
    for (const QString& device : {QString("/dev/null"), QString("/dev/zero"),
                                  QString("/dev/urandom"), QString("/dev/random")})
        args << "--ro-bind" << device << device;
    for (const QString& path : {QString("/etc/fonts"), QString("/var/cache/fontconfig")})
        if (QFileInfo::exists(path))
            args << "--ro-bind" << path << path;
    // Portable packages carry Qt and its dependency closure. The worker's
    // cleared environment must explicitly select those same libraries.
    const QDir installation(QCoreApplication::applicationDirPath() + "/..");
    const QString runtime = installation.absoluteFilePath("lib/runtime");
    if (QFileInfo(runtime).isDir())
        args << "--ro-bind" << runtime << "/runtime/lib"
             << "--setenv" << "LD_LIBRARY_PATH" << "/pdfium:/runtime/lib";
    const QString fonts = installation.absoluteFilePath("share/fonts");
    const QString fontConfig = installation.absoluteFilePath("share/fonts/sandbox.conf");
    if (QFileInfo(fonts).isDir() && QFileInfo::exists(fontConfig))
        args << "--ro-bind" << fonts << "/runtime/fonts"
             << "--setenv" << "FONTCONFIG_FILE" << "/runtime/fonts/sandbox.conf";
    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        args << "--setenv" << "PDF_EDITOR_WORKER_DIAGNOSTICS" << "1";
    args << "--ro-bind" << binary << "/probe"
         << "--ro-bind" << library << "/pdfium/libpdfium.so"
         << "--ro-bind" << snapshot_->filePath("input.pdf") << "/input.pdf"
         << "--chdir" << "/tmp" << "/probe";
    sandboxProgram_ = bwrap;
#elif defined(Q_OS_WIN)
    const QString binary = QCoreApplication::applicationDirPath() + "/pdf-render-worker.exe";
    sandboxProgram_ = QCoreApplication::applicationDirPath() + "/pdf-sandbox.exe";
    if (!QFileInfo::exists(binary) || !QFileInfo::exists(sandboxProgram_))
    {
        fail("The native PDF worker or sandbox launcher is missing.");
        return;
    }
    QStringList args{"--input", snapshot_->filePath("input.pdf")};
#elif defined(Q_OS_MACOS)
    const QString binary = QCoreApplication::applicationDirPath() + "/pdf-render-worker";
    sandboxProgram_ = binary;
    if (!QFileInfo::exists(binary) || !QFileInfo::exists(sandboxProgram_))
    {
        fail("The native PDF worker or macOS sandbox is missing.");
        return;
    }
    auto quoted = [](QString path)
    {
        path.replace("\\", "\\\\");
        path.replace("\"", "\\\"");
        return "\"" + path + "\"";
    };
    const QString resources =
        QFileInfo(QCoreApplication::applicationDirPath() + "/..").canonicalFilePath();
    const QString snapshotRoot = QFileInfo(snapshot_->path()).canonicalFilePath();
    const QString profile =
        "(version 1)(deny default)(allow sysctl-read)(allow file-read-metadata)"
        // PDFium/V8 maps its bundled code and generates JavaScript JIT code.
        // File reads remain limited to the paths below.
        "(allow file-map-executable)(allow dynamic-code-generation)"
        "(allow process-info* (target same-sandbox))(allow signal (target same-sandbox))"
        "(allow mach-lookup (global-name \"com.apple.FontObjectsServer\")"
        " (global-name \"com.apple.fontd\"))"
        "(allow file-read* (subpath \"/System\") (subpath \"/usr/lib\")"
        " (subpath \"/Library/Fonts\") (subpath \"/Library/Apple/System/Library\")"
        " (literal \"/dev/null\") (literal \"/dev/random\") (literal \"/dev/urandom\")"
        " (subpath " +
        quoted(resources) + ") (subpath " + quoted(snapshotRoot) +
        "))"
        "(allow process-exec (literal " +
        quoted(QFileInfo(binary).canonicalFilePath()) + "))";
    QStringList args{"--sandbox-profile", profile, "--input", snapshot_->filePath("input.pdf")};
#else
#error "No native worker sandbox for this platform"
#endif
    sandboxArgs_ = args;
    worker_ = new QProcess(this);
    connect(worker_, &QProcess::started, this,
            [this]
            {
                if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
                    qWarning() << "PDF timing: broker process started pid" << worker_->processId()
                               << "launch_ms" << performanceClock_.elapsed() - requestStartedMs_;
            });
    worker_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(worker_, &QProcess::readyReadStandardOutput, this, &PdfDocument::receive);
    // Drain diagnostics so a noisy parser cannot fill its stderr pipe.
    connect(worker_, &QProcess::readyReadStandardError, this,
            [this]
            {
                if (worker_)
                {
                    const auto bytes = worker_->readAllStandardError();
                    diagnostics_ +=
                        bytes.left(std::max(0, 4096 - static_cast<int>(diagnostics_.size())));
                    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
                        qWarning().noquote() << "PDF worker stages:" << QString::fromUtf8(bytes);
                }
            });
    auto reportFailure = [this]
    {
        if (!worker_)
            return;
        diagnostics_ += worker_->readAllStandardError().left(
            std::max(0, 4096 - static_cast<int>(diagnostics_.size())));
        if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
            qWarning().noquote() << "PDF worker: process error" << worker_->error()
                                 << worker_->errorString() << "exit" << worker_->exitCode() << "hex"
                                 << QString::number(static_cast<quint32>(worker_->exitCode()), 16)
                                 << "status" << worker_->exitStatus()
                                 << QString::fromUtf8(diagnostics_);
    };
    connect(worker_, &QProcess::errorOccurred, this,
            [this, reportFailure](QProcess::ProcessError)
            {
                reportFailure();
                fail("Cannot start the isolated PDF renderer.");
            });
    connect(worker_, &QProcess::finished, this,
            [this, reportFailure](int, QProcess::ExitStatus)
            {
                reportFailure();
                fail(loading_ && diagnostics_.contains("bwrap:")
                         ? "Your system could not start the PDF sandbox. Check the viewer setup "
                           "instructions."
                         : "The isolated PDF renderer stopped unexpectedly.");
            });
    requestStartedMs_ = performanceClock_.elapsed();
#ifdef Q_OS_WIN
    deadline_.start(60000); // Cold runtime preparation can include antivirus scans.
#else
    deadline_.start(15000);
#endif
#ifdef Q_OS_MACOS
    QProcessEnvironment environment;
    environment.insert("PATH", "/usr/bin:/bin");
    environment.insert("HOME", "/tmp");
    environment.insert("LANG", "en_US.UTF-8");
    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        environment.insert("PDF_EDITOR_WORKER_DIAGNOSTICS", "1");
    environment.insert("QT_PLUGIN_PATH", QCoreApplication::applicationDirPath() + "/../PlugIns");
    worker_->setProcessEnvironment(environment);
#endif
    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        qWarning() << "PDF timing: launching broker epoch_ms"
                   << QDateTime::currentMSecsSinceEpoch();
    worker_->start(sandboxProgram_, args);
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
    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        qWarning().noquote() << "PDF timing: reply"
                             << (loading_ ? QString("startup")
                                          : active_.command.value("op").toString("render"))
                             << "id" << active_.id << "page" << active_.page << "width"
                             << active_.width << "roundtrip_ms"
                             << performanceClock_.elapsed() - requestStartedMs_ << "worker_ms"
                             << response.value("worker_ms").toDouble(-1);
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
            if (response.contains("fields"))
                formFields_ = response["fields"].toArray().toVariantList();
            if (response.contains("fieldsComplete"))
                fieldsComplete_ = response["fieldsComplete"].toBool();
            if (response["changed"].toBool())
            {
                ++formRevision_;
                cache_.clear();
                cachedRequests_.clear();
                emit formRepaint();
            }
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
        formFields_ = response["fields"].toArray().toVariantList();
        fieldsComplete_ = response["fieldsComplete"].toBool();
        emit formsChanged();
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
            if (command["action"].toString() == "copy" || command["action"].toString() == "cut")
                QGuiApplication::clipboard()->setText(response["selectedText"].toString());
            --pendingFormEvents_;
            formText_ = response["text"].toString();
            formFieldType_ = response["fieldType"].toInt(-1);
            formFields_ = response["fields"].toArray().toVariantList();
            if (response.contains("fieldsComplete"))
                fieldsComplete_ = response["fieldsComplete"].toBool();
            if (response.contains("resetPages"))
            {
                pages_ = response["resetPages"].toArray().toVariantList();
                setCurrentPage(1);
                updateFit();
                emit stateChanged();
            }
            const int previousFocus = focusedField_;
            focusedField_ = response["focusedField"].toInt(-1);
            formPage_ = response["focusPage"].toInt(formPage_);
            if (command["action"].toString() == "validate")
                formValidated_ = true;
            if (focusedField_ >= 0 &&
                (focusedField_ != previousFocus || command["action"] == "focusField" ||
                 command["action"] == "nextField"))
            {
                setCurrentPage(formPage_ + 1);
                emit formFocusRequested(formPage_, focusedField());
            }
            if (xfaFull_ && response["caretUpdated"].toBool() && command["action"] == "key" &&
                command["key"].toInt() == 9)
            {
                setCurrentPage(formPage_ + 1);
                emit formFocusRequested(formPage_, response["focusRect"].toObject().toVariantMap());
            }
            formError_ = response["validationMessage"].toString();
            if (response["changed"].toBool())
                ++formRevision_;
            cache_.clear();
            cachedRequests_.clear();
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
        cachedRequests_.insert(id);
        const QImage image = *cached;
        const auto generation = generation_;
        QMetaObject::invokeMethod(
            this,
            [this, generation, id, image]
            {
                if (generation == generation_ && cachedRequests_.remove(id))
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
            queue_.append({id, page, width, {id}, {}, performanceClock_.elapsed()});
    }
    nextRequest();
    return id;
}

void PdfDocument::cancelRender(quint64 id)
{
    cachedRequests_.remove(id);
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
    // Preserve input order, but don't make typing wait behind queued thumbnails.
    int next = 0;
    if (!saving_)
    {
        for (int index = 0; index < queue_.size(); ++index)
            if (queue_[index].command.isEmpty() && queue_[index].page == currentPage_ - 1 &&
                (queue_[next].page != currentPage_ - 1 || queue_[index].width > queue_[next].width))
                next = index;
        for (int index = 0; index < queue_.size(); ++index)
            if (queue_[index].command.value("op") == "event")
            {
                next = index;
                break;
            }
    }
    active_ = queue_.takeAt(next);
    requestStartedMs_ = performanceClock_.elapsed();
    if (qEnvironmentVariableIsSet("PDF_EDITOR_WORKER_DIAGNOSTICS"))
        qWarning().noquote() << "PDF timing: send" << active_.command.value("op").toString("render")
                             << "action" << active_.command.value("action").toString() << "id"
                             << active_.id << "page" << active_.page << "width" << active_.width
                             << "queue_ms" << requestStartedMs_ - active_.queuedMs << "pending"
                             << queue_.size();
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
    const QStringList allowed{"click",      "text",      "key",          "selectAll",
                              "blur",       "copy",      "cut",          "highlight",
                              "focusField", "nextField", "setFieldText", "chooseOption",
                              "resetField", "resetForm", "validate"};
    if (!allowed.contains(action))
        return 0;
    const quint64 id = ++nextId_;
    formPage_ = page;
    QJsonObject command{{"op", "event"}, {"page", page}, {"action", action}, {"x", x},
                        {"y", y},        {"key", key},   {"text", text},     {"flags", flags}};
    queue_.append({id, page, 0, {}, command, performanceClock_.elapsed()});
    ++pendingFormEvents_;
    emit formsChanged();
    emit saveStateChanged();
    nextRequest();
    return id;
}
QVariantMap PdfDocument::focusedField() const
{
    return focusedField_ >= 0 && focusedField_ < formFields_.size()
               ? formFields_[focusedField_].toMap()
               : QVariantMap{};
}
QVariantList PdfDocument::formValidation() const
{
    QVariantList issues;
    if (!formValidated_)
        return issues;
    for (const auto& value : formFields_)
    {
        const auto field = value.toMap();
        if (field["readOnly"].toBool())
            continue;
        const QString text = field["value"].toString().trimmed();
        const int type = field["type"].toInt();
        QString message;
        if (field["required"].toBool() &&
            (text.isEmpty() || (type == 2 && !field["checked"].toBool()) ||
             (type == 3 && text == "Off")))
            message = "Required field is empty";
        if (!field["valueComplete"].toBool() && type != 7)
            message = "Field value is too large to check";
        const auto format = field["dateFormat"].toString();
        if (!text.isEmpty() && !format.isEmpty() &&
            !QDate::fromString(text, QString(format).replace("mm", "MM")).isValid())
            message = "Date must use " + format;
        if (!message.isEmpty())
            issues.append(QVariantMap{{"id", field["id"]},
                                      {"page", field["page"]},
                                      {"label", field["label"]},
                                      {"message", message}});
    }
    return issues;
}
quint64 PdfDocument::focusFormField(int id) { return formEvent(formPage_, "focusField", 0, 0, id); }
quint64 PdfDocument::navigateForm(bool backwards)
{
    return formEvent(formPage_, "nextField", 0, 0, 0, {}, backwards ? 1 : 0);
}
quint64 PdfDocument::resetFormField(int id) { return formEvent(formPage_, "resetField", 0, 0, id); }
quint64 PdfDocument::resetForm() { return formEvent(formPage_, "resetForm"); }
quint64 PdfDocument::validateForm() { return formEvent(formPage_, "validate"); }
quint64 PdfDocument::chooseFormOption(int id, int option)
{
    return formEvent(formPage_, "chooseOption", 0, 0, id, QString::number(option));
}
quint64 PdfDocument::setFormFieldText(int id, const QString& text)
{
    return formEvent(formPage_, "setFieldText", 0, 0, id, text);
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
