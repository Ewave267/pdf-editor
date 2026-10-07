// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "content/AddedContent.h"
#include <QCache>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

// Application-facing document state. Only the worker includes PDFium headers.
class PdfDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString formText READ formText NOTIFY formsChanged)
    Q_PROPERTY(QString formError READ formError NOTIFY formsChanged)
    Q_PROPERTY(int formFieldType READ formFieldType NOTIFY formsChanged)
    Q_PROPERTY(bool formBusy READ formBusy NOTIFY formsChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY saveStateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY saveStateChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveStateChanged)
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY saveStateChanged)
    Q_PROPERTY(AddedContent* additions READ additions CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString formType READ formType NOTIFY stateChanged)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY stateChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY stateChanged)
    Q_PROPERTY(double maxPageWidth READ maxPageWidth NOTIFY stateChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(bool fitting READ fitting NOTIFY zoomChanged)
  public:
    explicit PdfDocument(QObject* parent = nullptr);
    ~PdfDocument() override;
    AddedContent* additions() const { return additions_; }
    bool saving() const { return saving_; }
    bool dirty() const
    {
        return pendingFormEvents_ > 0 || additions_->revision() != savedRevision_ ||
               formRevision_ != savedFormRevision_;
    }
    QString saveError() const { return saveError_; }
    QString savedPath() const { return savedPath_; }
    QString formText() const { return formText_; }
    QString formError() const { return formError_; }
    int formFieldType() const { return formFieldType_; }
    bool formBusy() const { return pendingFormEvents_ > 0; }
    Q_INVOKABLE quint64 formEvent(int page, const QString& action, double x = 0, double y = 0,
                                  int key = 0, const QString& text = {}, int flags = 0);
    Q_INVOKABLE void commitForm();
    Q_INVOKABLE void saveAs(const QUrl& url);
    bool ready() const { return ready_; }
    bool loading() const { return loading_; }
    QString error() const { return error_; }
    QString fileName() const { return fileName_; }
    QString formType() const { return formType_; }
    QVariantList pages() const { return pages_; }
    int pageCount() const { return pages_.size(); }
    double maxPageWidth() const;
    int currentPage() const { return currentPage_; }
    double zoom() const { return zoom_; }
    bool fitting() const { return fitting_; }
    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void close();
    Q_INVOKABLE void fitToPage();
    Q_INVOKABLE void updateViewport(double width, double height);
    void setCurrentPage(int page);
    void setZoom(double zoom);
    quint64 requestRender(int page, int width);
    void cancelRender(quint64 id);
    qint64 workerPid() const;
  signals:
    void formsChanged();
    void formRepaint();
    void formEventFinished(quint64 id, bool handled);
    void saveStateChanged();
    void saveFinished(bool success);
    void stateChanged();
    void currentPageChanged();
    void zoomChanged();
    void rendered(quint64 request, const QImage& image);

  private:
    struct Request
    {
        quint64 id;
        int page;
        int width;
        QList<quint64> listeners;
        QJsonObject command;
        QString key() const { return QString::number(page) + "/" + QString::number(width); }
    };
    void stopWorker();
    void fail(const QString& message);
    void receive();
    void nextRequest();
    void updateFit();
    void startSaveWorker(const QByteArray& baseline);
    void finishSave(const QString& error);
    AddedContent* additions_;
    QProcess* saveWorker_ = nullptr;
    QTimer saveDeadline_;
    QByteArray saveBytes_, saveOverlay_;
    QString sourcePath_, saveDestination_, saveError_, savedPath_;
    QStringList sandboxArgs_;
    std::unique_ptr<QTemporaryDir> snapshot_;
    quint64 savedRevision_ = 0, saveRevision_ = 0;
    bool saving_ = false, xfaFull_ = false;
    quint64 formRevision_ = 0, savedFormRevision_ = 0, saveFormRevision_ = 0;
    int pendingFormEvents_ = 0, formFieldType_ = -1, formPage_ = 0;
    QString formText_, formError_;
    QProcess* worker_ = nullptr;
    QTimer deadline_;
    QByteArray incoming_, diagnostics_;
    QList<Request> queue_;
    Request active_{};
    QCache<QString, QImage> cache_{64 * 1024}; // Cost in KiB; maximum 64 MiB.
    QVariantList pages_;
    quint64 nextId_ = 0;
    quint64 generation_ = 0;
    bool ready_ = false;
    bool loading_ = false;
    bool fitting_ = true;
    QString error_, fileName_, formType_;
    int currentPage_ = 1;
    double zoom_ = 1;
    double viewportWidth_ = 800, viewportHeight_ = 600;
};
