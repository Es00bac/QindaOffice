// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_accessible.h"

#include "page_text_control.h"
#include "page_text_item.h"
#include "write_document.h"

#include <QQuickWindow>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

AccessiblePageTextItem::AccessiblePageTextItem(PageTextItem *item)
    : QAccessibleQuickItem(item)
    , m_item(item)
{
}

void *AccessiblePageTextItem::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::TextInterface) {
        return static_cast<QAccessibleTextInterface *>(this);
    }
    if (type == QAccessible::EditableTextInterface) {
        return static_cast<QAccessibleEditableTextInterface *>(this);
    }
    return QAccessibleQuickItem::interface_cast(type);
}

QAccessible::Role AccessiblePageTextItem::role() const
{
    // The paged editor is a document, not a static label.
    return QAccessible::Document;
}

QAccessible::State AccessiblePageTextItem::state() const
{
    QAccessible::State s = QAccessibleQuickItem::state();
    s.readOnly = false;
    s.multiLine = true;
    s.focusable = true;
    s.editable = true;
    return s;
}

QString AccessiblePageTextItem::text(QAccessible::Text t) const
{
    if (t == QAccessible::Value && m_item->writeDocument() != nullptr) {
        return m_item->writeDocument()->textDocument()->toPlainText();
    }
    return QAccessibleQuickItem::text(t);
}

void AccessiblePageTextItem::selection(int selectionIndex, int *startOffset, int *endOffset) const
{
    const QTextCursor cursor = m_item->control()->textCursor();
    if (selectionIndex == 0 && cursor.hasSelection()) {
        *startOffset = cursor.selectionStart();
        *endOffset = cursor.selectionEnd();
    } else {
        *startOffset = 0;
        *endOffset = 0;
    }
}

int AccessiblePageTextItem::selectionCount() const
{
    return m_item->control()->textCursor().hasSelection() ? 1 : 0;
}

void AccessiblePageTextItem::addSelection(int startOffset, int endOffset)
{
    QTextCursor cursor = cursorForPosition(startOffset);
    cursor.setPosition(qBound(0, endOffset, characterCount()), QTextCursor::KeepAnchor);
    m_item->control()->setTextCursor(cursor);
}

void AccessiblePageTextItem::removeSelection(int selectionIndex)
{
    Q_UNUSED(selectionIndex);
    QTextCursor cursor = m_item->control()->textCursor();
    cursor.clearSelection();
    m_item->control()->setTextCursor(cursor);
}

void AccessiblePageTextItem::setSelection(int selectionIndex, int startOffset, int endOffset)
{
    Q_UNUSED(selectionIndex);
    addSelection(startOffset, endOffset);
}

int AccessiblePageTextItem::cursorPosition() const
{
    return m_item->control()->textCursor().position();
}

void AccessiblePageTextItem::setCursorPosition(int position)
{
    m_item->control()->setTextCursor(cursorForPosition(position));
}

QString AccessiblePageTextItem::text(int startOffset, int endOffset) const
{
    const QString plain = m_item->writeDocument()->textDocument()->toPlainText();
    const int from = qBound(0, startOffset, int(plain.size()));
    const int to = qBound(from, endOffset, int(plain.size()));
    return plain.mid(from, to - from);
}

int AccessiblePageTextItem::characterCount() const
{
    return m_item->writeDocument()->textDocument()->characterCount() - 1; // trailing \n
}

QRect AccessiblePageTextItem::characterRect(int offset) const
{
    const QRectF local = m_item->docToView(m_item->control()->cursorRect(cursorForPosition(offset)));
    const QPointF scene = m_item->mapToScene(local.topLeft());
    const QPoint global = m_item->window() != nullptr ? m_item->window()->mapToGlobal(scene.toPoint())
                                                      : scene.toPoint();
    return QRect(global, local.size().toSize());
}

int AccessiblePageTextItem::offsetAtPoint(const QPoint &point) const
{
    const QPointF scene = m_item->window() != nullptr ? QPointF(m_item->window()->mapFromGlobal(point))
                                                      : QPointF(point);
    return m_item->control()->cursorForPosition(m_item->viewToDoc(m_item->mapFromScene(scene))).position();
}

void AccessiblePageTextItem::scrollToSubstring(int startIndex, int endIndex)
{
    Q_UNUSED(endIndex);
    const QRectF rect = m_item->control()->cursorRect(cursorForPosition(startIndex));
    emit m_item->ensureVisibleRequested(m_item->docToView(rect).translated(0, m_item->y()));
}

QString AccessiblePageTextItem::attributes(int offset, int *startOffset, int *endOffset) const
{
    *startOffset = 0;
    *endOffset = characterCount();
    const QTextCharFormat format = cursorForPosition(offset).charFormat();
    QStringList attrs;
    if (format.hasProperty(QTextFormat::FontWeight) && format.fontWeight() >= QFont::Bold) {
        attrs.append(QStringLiteral("font-weight:bold"));
    }
    if (format.fontItalic()) {
        attrs.append(QStringLiteral("font-style:italic"));
    }
    return attrs.join(QLatin1Char(';'));
}

void AccessiblePageTextItem::deleteText(int startOffset, int endOffset)
{
    QTextCursor cursor = cursorForPosition(startOffset);
    cursor.setPosition(qBound(0, endOffset, characterCount()), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
}

void AccessiblePageTextItem::insertText(int offset, const QString &text)
{
    cursorForPosition(offset).insertText(text);
}

void AccessiblePageTextItem::replaceText(int startOffset, int endOffset, const QString &text)
{
    QTextCursor cursor = cursorForPosition(startOffset);
    cursor.setPosition(qBound(0, endOffset, characterCount()), QTextCursor::KeepAnchor);
    cursor.insertText(text);
}

QTextCursor AccessiblePageTextItem::cursorForPosition(int offset) const
{
    QTextCursor cursor(m_item->writeDocument()->textDocument());
    cursor.setPosition(qBound(0, offset, characterCount()));
    return cursor;
}

namespace {

QAccessibleInterface *pageTextFactory(const QString &classname, QObject *object)
{
    if (classname == QLatin1String("QindaOffice::Apps::Write::PageTextItem")
        || classname == QLatin1String("PageTextItem")) {
        if (auto *item = qobject_cast<PageTextItem *>(object); item != nullptr && item->isAttached()) {
            return new AccessiblePageTextItem(item);
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
