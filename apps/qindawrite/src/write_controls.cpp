// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_controls.h"

#include "docx_controls.h"
#include "docx_properties.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QAbstractTextDocumentLayout>
#include <QDate>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

namespace QindaOffice::Apps::Write {

using Kind = Docx::ContentControl::Kind;

WriteControls::WriteControls(WriteDocument *document, PageTextControl *control, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_control(control)
{
}

QString WriteControls::activate(int position, const QPointF &docPoint, const QPointF &viewPoint)
{
    dismiss();
    const QTextDocument *text = m_document->textDocument();
    const Docx::ControlSpan span = Docx::controlAt(*text, position);
    const auto found = m_document->parts().contentControls.constFind(span.key);
    if (span.key.isEmpty() || found == m_document->parts().contentControls.cend()
        || text->findBlock(span.start) != text->findBlock(span.end)) {
        return {}; // not a field, or one spanning paragraphs (a block-level wrapper)
    }
    const Docx::ContentControl control = *found;
    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(span.start);
    cursor.setPosition(span.end, QTextCursor::KeepAnchor);
    const QString now = cursor.selectedText();
    m_key = span.key;
    m_start = span.start;
    m_end = span.end;
    m_title = control.title;
    switch (control.kind) {
    case Kind::CheckBox: {
        if (!onSymbol(span.start, span.end, docPoint)) {
            dismiss();
            return {};
        }
        const bool checked = now.contains(control.checkedSymbol);
        QString next = now;
        next.replace(checked ? control.checkedSymbol : control.uncheckedSymbol,
                     checked ? control.uncheckedSymbol : control.checkedSymbol);
        if (next == now) {
            next = QString(checked ? control.uncheckedSymbol : control.checkedSymbol);
        }
        replaceField(next);
        dismiss();
        return QStringLiteral("checkbox");
    }
    case Kind::DropDown:
    case Kind::ComboBox:
        m_kind = QStringLiteral("list");
        m_items = control.itemTexts;
        emit activeChanged();
        emit popupRequested(viewPoint.x(), viewPoint.y());
        return m_kind;
    case Kind::Date: {
        const QDate shown = QDate::fromString(now.trimmed(), Docx::qtDateFormat(control.dateFormat));
        m_kind = QStringLiteral("date");
        m_date = (shown.isValid() ? shown : QDate::currentDate()).toString(Qt::ISODate);
        emit activeChanged();
        emit popupRequested(viewPoint.x(), viewPoint.y());
        return m_kind;
    }
    default:
        break;
    }
    if (control.showingPlaceholder && now == control.sourceText && span.end > span.start) {
        // Word selects a placeholder on a click, so typing replaces it. The
        // caret ends inside the field: what is typed keeps its key.
        QTextCursor select(m_document->textDocument());
        select.setPosition(span.start);
        select.setPosition(span.end, QTextCursor::KeepAnchor);
        m_control->setTextCursor(select);
        dismiss();
        return QStringLiteral("placeholder");
    }
    dismiss();
    return {};
}

bool WriteControls::choose(int index)
{
    if (m_kind != u"list" || index < 0 || index >= m_items.size()) {
        return false;
    }
    const bool done = replaceField(m_items.at(index));
    dismiss();
    return done;
}

bool WriteControls::chooseDate(const QString &isoDate)
{
    const QDate day = QDate::fromString(isoDate, Qt::ISODate);
    const auto found = m_document->parts().contentControls.constFind(m_key);
    if (m_kind != u"date" || !day.isValid() || found == m_document->parts().contentControls.cend()) {
        return false;
    }
    const bool done = replaceField(day.toString(Docx::qtDateFormat(found->dateFormat)));
    dismiss();
    return done;
}

void WriteControls::dismiss()
{
    const bool had = !m_kind.isEmpty();
    m_key.clear();
    m_start = m_end = -1;
    m_kind.clear();
    m_items.clear();
    m_date.clear();
    m_title.clear();
    if (had) {
        emit activeChanged();
    }
}

bool WriteControls::replaceField(const QString &text)
{
    QTextDocument *document = m_document->textDocument();
    if (m_key.isEmpty() || m_start < 0 || m_end < m_start || m_end > document->characterCount()) {
        return false;
    }
    QTextCursor probe(document);
    probe.setPosition(m_end > m_start ? m_start + 1 : m_start);
    QTextCharFormat format = probe.charFormat();
    if (format.stringProperty(Docx::kRunStyleId) == u"PlaceholderText") {
        // The new text is the field's value, not its grey placeholder.
        format.clearProperty(Docx::kRunStyleId);
        format.clearProperty(Docx::kRunXml);
        format.clearProperty(Docx::kRunSnapshot);
        format.clearForeground();
    }
    QStringList keys = format.property(Docx::kContentControls).toStringList();
    if (!keys.contains(m_key)) {
        keys.append(m_key);
        format.setProperty(Docx::kContentControls, keys);
    }
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setPosition(m_start);
    cursor.setPosition(m_end, QTextCursor::KeepAnchor);
    cursor.insertText(text, format);
    cursor.endEditBlock();
    m_end = m_start + int(text.size());
    return true;
}

bool WriteControls::onSymbol(int start, int end, const QPointF &docPoint) const
{
    if (docPoint.x() < 0 && docPoint.y() < 0) {
        return true; // no pointer (the keyboard, a test)
    }
    const QTextDocument *document = m_document->textDocument();
    const QTextBlock block = document->findBlock(start);
    const QTextLayout *layout = block.layout();
    if (layout == nullptr) {
        return false;
    }
    const QTextLine line = layout->lineForTextPosition(start - block.position());
    if (!line.isValid()) {
        return false;
    }
    const QPointF origin = document->documentLayout()->blockBoundingRect(block).topLeft();
    const qreal x0 = origin.x() + line.cursorToX(start - block.position());
    const qreal x1 = origin.x() + line.cursorToX(end - block.position());
    const qreal y0 = origin.y() + line.y();
    constexpr qreal kSlack = 1.0;
    return docPoint.x() >= qMin(x0, x1) - kSlack && docPoint.x() <= qMax(x0, x1) + kSlack && docPoint.y() >= y0
           && docPoint.y() <= y0 + line.height();
}

} // namespace QindaOffice::Apps::Write
