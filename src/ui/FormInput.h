// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pdf/PdfDocument.h"
#include <QPointer>
#include <QQuickItem>
class FormInput : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(PdfDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int page MEMBER page_ NOTIFY pageChanged)
  public:
    explicit FormInput(QQuickItem* parent = nullptr);
    PdfDocument* document() const { return document_; }
    void setDocument(PdfDocument* document)
    {
        document_ = document;
        emit documentChanged();
    }
  signals:
    void documentChanged();
    void pageChanged();

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusOutEvent(QFocusEvent* event) override;

  private:
    bool usable() const;
    int flags(Qt::KeyboardModifiers modifiers) const;
    QPointer<PdfDocument> document_;
    int page_ = 0;
};
