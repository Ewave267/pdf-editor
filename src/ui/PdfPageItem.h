// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pdf/PdfDocument.h"
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

class PdfPageItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(PdfDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(bool rendered READ rendered NOTIFY renderedChanged)
  public:
    explicit PdfPageItem(QQuickItem* parent = nullptr);
    ~PdfPageItem() override;
    PdfDocument* document() const { return document_; }
    int page() const { return page_; }
    bool rendered() const { return !image_.isNull(); }
    void setDocument(PdfDocument* document);
    void setPage(int page);
    void paint(QPainter* painter) override;
  signals:
    void documentChanged();
    void pageChanged();
    void renderedChanged();

  protected:
    void geometryChange(const QRectF& next, const QRectF& previous) override;

  private:
    void schedule();
    void request();
    QPointer<PdfDocument> document_;
    QTimer debounce_;
    QImage image_;
    int page_ = -1;
    quint64 request_ = 0;
};
