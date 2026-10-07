// SPDX-License-Identifier: GPL-3.0-only
#include "FormInput.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
FormInput::FormInput(QQuickItem* parent) : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    setFlag(ItemAcceptsInputMethod, true);
}
bool FormInput::usable() const
{
    return document_ && document_->ready() && !document_->saving() &&
           document_->formType() != "PDF";
}
int FormInput::flags(Qt::KeyboardModifiers modifiers) const
{
    // PDFium's public FWL modifiers: Shift=1, Control=2, Alt=4.
    return (modifiers.testFlag(Qt::ShiftModifier) ? 1 : 0) |
           (modifiers.testFlag(Qt::ControlModifier) ? 2 : 0) |
           (modifiers.testFlag(Qt::AltModifier) ? 4 : 0);
}
void FormInput::mousePressEvent(QMouseEvent* event)
{
    if (!usable())
    {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    const auto dimensions = document_->pages()[page_].toMap();
    document_->setCurrentPage(page_ + 1);
    document_->formEvent(page_, "click",
                         event->position().x() / width() * dimensions["width"].toDouble(),
                         event->position().y() / height() * dimensions["height"].toDouble(), 0, {},
                         flags(event->modifiers()));
    event->accept();
}
void FormInput::keyPressEvent(QKeyEvent* event)
{
    if (!usable())
    {
        event->ignore();
        return;
    }
    const int modifiers = flags(event->modifiers());
    if (event->matches(QKeySequence::SelectAll))
        document_->formEvent(page_, "selectAll");
    else if (event->matches(QKeySequence::Copy))
        document_->formEvent(page_, "copy");
    else if (event->matches(QKeySequence::Paste))
        document_->formEvent(page_, "text", 0, 0, 0, QGuiApplication::clipboard()->text());
    else
    {
        int key = 0;
        switch (event->key())
        {
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            key = 9;
            break;
        case Qt::Key_Backspace:
            key = 8;
            break;
        case Qt::Key_Delete:
            key = 46;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            key = 13;
            break;
        case Qt::Key_Escape:
            key = 27;
            break;
        case Qt::Key_Left:
            key = 37;
            break;
        case Qt::Key_Up:
            key = 38;
            break;
        case Qt::Key_Right:
            key = 39;
            break;
        case Qt::Key_Down:
            key = 40;
            break;
        case Qt::Key_Home:
            key = 36;
            break;
        case Qt::Key_End:
            key = 35;
            break;
        case Qt::Key_PageUp:
            key = 33;
            break;
        case Qt::Key_PageDown:
            key = 34;
            break;
        case Qt::Key_Space:
            key = 32;
            break;
        }
        if (key)
            document_->formEvent(page_, "key", 0, 0, key, {},
                                 modifiers | (event->key() == Qt::Key_Backtab ? 1 : 0));
        else if (!event->text().isEmpty() && !(modifiers & 6))
            document_->formEvent(page_, "text", 0, 0, 0, event->text());
        else
        {
            event->ignore();
            return;
        }
    }
    event->accept();
}
void FormInput::inputMethodEvent(QInputMethodEvent* event)
{
    if (usable() && !event->commitString().isEmpty())
    {
        document_->formEvent(page_, "text", 0, 0, 0, event->commitString());
        event->accept();
    }
    else
        event->ignore();
}
QVariant FormInput::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
        return usable();
    if (query == Qt::ImCursorRectangle)
        return QRectF(0, 0, 1, 20);
    return QQuickItem::inputMethodQuery(query);
}
void FormInput::focusOutEvent(QFocusEvent* event)
{
    if (usable())
        document_->formEvent(page_, "blur");
    QQuickItem::focusOutEvent(event);
}
