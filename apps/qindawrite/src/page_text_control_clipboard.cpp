// SPDX-License-Identifier: GPL-3.0-or-later
// PageTextControl: the clipboard (HTML + text), undo/redo and select-all,
// over whichever story the control edits (split from page_text_control.cpp).
#include "page_text_control.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTextDocumentFragment>

namespace QindaOffice::Apps::Write {


bool PageTextControl::canPaste() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime != nullptr && (mime->hasHtml() || mime->hasText());
}

void PageTextControl::copy()
{
    if (!m_cursor.hasSelection()) {
        return;
    }
    const QTextDocumentFragment fragment(m_cursor);
    auto *mime = new QMimeData;
    mime->setHtml(fragment.toHtml());
    mime->setText(fragment.toPlainText());
    QGuiApplication::clipboard()->setMimeData(mime);
}

void PageTextControl::cut()
{
    if (!m_cursor.hasSelection()) {
        return;
    }
    copy();
    m_cursor.removeSelectedText();
    cursorSettled(true);
}

void PageTextControl::paste()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr) {
        return;
    }
    if (mime->hasHtml()) {
        m_cursor.insertFragment(QTextDocumentFragment::fromHtml(mime->html()));
    } else if (mime->hasText()) {
        m_cursor.insertText(mime->text(), m_charFormat);
    } else {
        return;
    }
    m_formatPinned = false;
    cursorSettled(true);
}

void PageTextControl::undo()
{
    m_document->undo(&m_cursor);
    m_formatPinned = false;
    cursorSettled(true);
}

void PageTextControl::redo()
{
    m_document->redo(&m_cursor);
    m_formatPinned = false;
    cursorSettled(true);
}

void PageTextControl::selectAll()
{
    m_cursor.select(QTextCursor::Document);
    cursorSettled(false);
}

void PageTextControl::insertText(const QString &text)
{
    m_cursor.insertText(text, m_charFormat);
    cursorSettled(true);
}

} // namespace QindaOffice::Apps::Write
