// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_accessibility.h"

#include "page_text_view.h"
#include "write_document.h"

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

namespace QindaOffice::Apps::Write {

AccessiblePageTextView::AccessiblePageTextView(PageTextView *view)
    : QAccessibleWidget(view, QAccessible::StaticText)
    , m_view(view)
{
}

void *AccessiblePageTextView::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::TextInterface) {
        return static_cast<QAccessibleTextInterface *>(this);
    }
    if (type == QAccessible::EditableTextInterface) {
        return static_cast<QAccessibleEditableTextInterface *>(this);
    }
    return QAccessibleWidget::interface_cast(type);
}

QAccessible::Role AccessiblePageTextView::role() const
{
    // The paged editor is a document, not a static label.
    return QAccessible::Document;
}

QAccessible::State AccessiblePageTextView::state() const
{
    QAccessible::State s = QAccessibleWidget::state();
    s.readOnly = false;
    s.multiLine = true;
    s.focusable = true;
    s.editable = true;
    return s;
}

QString AccessiblePageTextView::text(QAccessible::Text t) const
{
    if (t == QAccessible::Value && m_view->writeDocument() != nullptr
        && m_view->writeDocument()->textDocument() != nullptr) {
        return m_view->writeDocument()->textDocument()->toPlainText();
    }
    return QAccessibleWidget::text(t);
}

void AccessiblePageTextView::selection(int selectionIndex, int *startOffset,
                                       int *endOffset) const
{
    const QTextCursor cursor = m_view->control()->textCursor();
    if (selectionIndex == 0 && cursor.hasSelection()) {
        *startOffset = cursor.selectionStart();
        *endOffset = cursor.selectionEnd();
    } else {
        *startOffset = 0;
        *endOffset = 0;
    }
}

QRect AccessiblePageTextView::characterRect(int offset) const
{
    const QRectF local = m_view->control()->cursorRect(cursorForPosition(offset));
    const QPoint global = m_view->viewport()->mapToGlobal(local.topLeft().toPoint());
    return QRect(global, local.size().toSize());
}

int AccessiblePageTextView::offsetAtPoint(const QPoint &point) const
{
    const QPoint local = m_view->mapFromGlobal(point);
    return m_view->control()->cursorForPosition(
               m_view->viewport()->mapTo(m_view, local))
        .position();
}

QString AccessiblePageTextView::attributes(int offset, int *startOffset, int *endOffset) const
{
    *startOffset = 0;
    *endOffset = characterCount();
    const QTextCursor cursor = cursorForPosition(offset);
    const QTextCharFormat format = cursor.charFormat();
    QStringList attrs;
    if (format.hasProperty(QTextFormat::FontWeight)
        && format.fontWeight() >= QFont::Bold) {
        attrs.append(QStringLiteral("font-weight:bold"));
    }
    if (format.fontItalic()) {
        attrs.append(QStringLiteral("font-style:italic"));
    }
    return attrs.join(QLatin1Char(';'));
}

void AccessiblePageTextView::deleteText(int startOffset, int endOffset)
{
    QTextCursor cursor = cursorForPosition(startOffset);
    cursor.setPosition(qBound(0, endOffset, characterCount()), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
}

void AccessiblePageTextView::insertText(int offset, const QString &text)
{
    QTextCursor cursor = cursorForPosition(offset);
    cursor.insertText(text);
}

void AccessiblePageTextView::replaceText(int startOffset, int endOffset, const QString &text)
{
    QTextCursor cursor = cursorForPosition(startOffset);
    cursor.setPosition(qBound(0, endOffset, characterCount()), QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

QTextCursor AccessiblePageTextView::cursorForPosition(int offset) const
{
    QTextCursor cursor(m_view->writeDocument()->textDocument());
    cursor.setPosition(qBound(0, offset, characterCount()));
    return cursor;
}

void AccessiblePageTextView::addSelection(int startOffset, int endOffset)
{
    QTextCursor cursor = cursorForPosition(startOffset);
    cursor.setPosition(qBound(0, endOffset, characterCount()), QTextCursor::KeepAnchor);
    m_view->control()->setTextCursor(cursor);
}

void AccessiblePageTextView::removeSelection(int selectionIndex)
{
    Q_UNUSED(selectionIndex);
    QTextCursor cursor = m_view->control()->textCursor();
    cursor.clearSelection();
    m_view->control()->setTextCursor(cursor);
}

void AccessiblePageTextView::setSelection(int selectionIndex, int startOffset, int endOffset)
{
    Q_UNUSED(selectionIndex);
    addSelection(startOffset, endOffset);
}

int AccessiblePageTextView::selectionCount() const
{
    return m_view->control()->textCursor().hasSelection() ? 1 : 0;
}

QString AccessiblePageTextView::text(int startOffset, int endOffset) const
{
    const QString plain =
        m_view->writeDocument()->textDocument()->toPlainText();
    const int from = qBound(0, startOffset, plain.size());
    const int to = qBound(from, endOffset, plain.size());
    return plain.mid(from, to - from);
}

int AccessiblePageTextView::characterCount() const
{
    return m_view->writeDocument()->textDocument()->characterCount() - 1; // trailing \n
}

int AccessiblePageTextView::cursorPosition() const
{
    return m_view->control()->textCursor().position();
}

void AccessiblePageTextView::setCursorPosition(int position)
{
    m_view->control()->setTextCursor(cursorForPosition(position));
}

void AccessiblePageTextView::scrollToSubstring(int startIndex, int endIndex)
{
    Q_UNUSED(endIndex);
    // Center the viewport on the requested offset.
    QTextCursor cursor = cursorForPosition(startIndex);
    const QRectF rect = m_view->control()->cursorRect(cursor);
    m_view->verticalScrollBar()->setValue(int(m_view->verticalScrollBar()->value()
                                            + rect.top() - m_view->viewport()->height() / 3.0));
}

namespace {

QAccessibleInterface *pageTextFactory(const QString &classname, QObject *object)
{
    if (classname == QLatin1String("QindaOffice::Apps::Write::PageTextView")
        || classname == QLatin1String("PageTextView")) {
        if (auto *view = qobject_cast<PageTextView *>(object)) {
            return new AccessiblePageTextView(view);
        }
    }
    return nullptr;
}

} // namespace

void installPageTextAccessibility()
{
    static bool installed = false;
    if (!installed) {
        QAccessible::installFactory(pageTextFactory);
        installed = true;
    }
}

} // namespace QindaOffice::Apps::Write
