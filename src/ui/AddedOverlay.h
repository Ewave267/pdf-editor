// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "content/AddedContent.h"
#include <QPointer>
#include <QQuickPaintedItem>
class AddedOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(AddedContent* content READ content WRITE setContent NOTIFY contentChanged)
    Q_PROPERTY(int page MEMBER page_ NOTIFY pageChanged)
    Q_PROPERTY(bool selection MEMBER selection_ NOTIFY selectionChanged)
  public:
    explicit AddedOverlay(QQuickItem* parent = nullptr) : QQuickPaintedItem(parent)
    {
        connect(this, &AddedOverlay::pageChanged, this, &AddedOverlay::updateOverlay);
        connect(this, &AddedOverlay::selectionChanged, this, &AddedOverlay::updateOverlay);
    }
    AddedContent* content() const { return content_; }
    void setContent(AddedContent* content)
    {
        if (content_)
            disconnect(content_, nullptr, this, nullptr);
        content_ = content;
        if (content_)
            connect(content_, &AddedContent::changed, this, &AddedOverlay::updateOverlay);
        emit contentChanged();
        update();
    }
    void paint(QPainter* painter) override;
  signals:
    void contentChanged();
    void pageChanged();
    void selectionChanged();

  private:
    void updateOverlay() { update(); }
    QPointer<AddedContent> content_;
    int page_ = -1;
    bool selection_ = true;
};
