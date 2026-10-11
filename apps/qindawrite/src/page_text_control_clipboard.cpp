// SPDX-License-Identifier: GPL-3.0-or-later
// PageTextControl: the clipboard (HTML + text), undo/redo and select-all,
// over whichever story the control edits (split from page_text_control.cpp).
#include "page_text_control.h"
#include "docx_math.h"
#include "docx_properties.h"
#include "math_omml.h"
#include "math_linear.h"
#include <QJsonDocument>
#include <QJsonObject>

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
    if (m_cursor.selectionEnd() - m_cursor.selectionStart() == 1 && Docx::isEquation(m_cursor.charFormat())) {
        const auto format = m_cursor.charFormat();
        const QString xml = format.stringProperty(Docx::kInlineXml);
        const QJsonObject object{{QStringLiteral("xml"), xml}, {QStringLiteral("size"), format.fontPointSize()}};
        mime->setData(QStringLiteral("application/x-qindaoffice-omml"), xml.toUtf8());
        mime->setData(QStringLiteral("application/x-qindaoffice-equation"), QJsonDocument(object).toJson(QJsonDocument::Compact));
        if (const auto equation = Math::readOmmlFragment(xml)) mime->setText(Math::toLinear(*equation));
    }
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
    const auto equation = QJsonDocument::fromJson(mime->data(QStringLiteral("application/x-qindaoffice-equation"))).object();
    QString xml = equation.value(QStringLiteral("xml")).toString();
    if (xml.isEmpty()) xml = QString::fromUtf8(mime->data(QStringLiteral("application/x-qindaoffice-omml")));
    if (!xml.isEmpty() && Math::readOmmlFragment(xml)) {
        QTextCharFormat format = m_charFormat;
        const qreal size = equation.value(QStringLiteral("size")).toDouble();
        if (size > 0 && size <= 144) format.setFontPointSize(size);
        m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), Docx::equationFormat(xml, format));
    } else if (mime->hasHtml()) {
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
