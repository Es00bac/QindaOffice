// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_rich_style.h"

#include <qindatk/rich_text_document.h>

#include <QCoreApplication>

namespace QindaOffice::Apps::Notes::RichStyle {

using namespace QindaTK::RichText;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Apps::Notes::RichStyle", text);
}

// The named styles' looks, in the persistence keys (QindaTK publishing.md).
QVariantMap lookOf(Style style)
{
    switch (style) {
    case Style::Title:
        return {{QStringLiteral("character"), QVariantMap{{QStringLiteral("sizePt"), 22.0},
                                                          {QStringLiteral("weight"), 300}}},
                {QStringLiteral("paragraph"), QVariantMap{{QStringLiteral("heading"), 0},
                                                          {QStringLiteral("spaceAfterPt"), 4.0}}}};
    case Style::Quote:
        return {{QStringLiteral("character"), QVariantMap{{QStringLiteral("italic"), true},
                                                          {QStringLiteral("color"), QStringLiteral("#595959")}}},
                {QStringLiteral("paragraph"), QVariantMap{{QStringLiteral("heading"), 0},
                                                          {QStringLiteral("indentStartPt"), 18.0}}}};
    case Style::Code:
        // AGENT-NOTE: the generic "monospace" family resolves on every
        // fontconfig system; the Word export maps it to Consolas.
        return {{QStringLiteral("character"), QVariantMap{{QStringLiteral("family"), QStringLiteral("monospace")},
                                                          {QStringLiteral("sizePt"), 10.0}}},
                {QStringLiteral("paragraph"), QVariantMap{{QStringLiteral("heading"), 0}}}};
    default:
        return {};
    }
}

QString styleIdNamed(const QindaTK::RichTextDocument &doc, const QString &name)
{
    for (const NamedStyle &named : doc.styleSheet()) {
        if (named.kind == StyleKind::Paragraph && named.name.compare(name, Qt::CaseInsensitive) == 0) {
            return named.id;
        }
    }
    return {};
}

// The named style for `style`, created inside the caller's open transaction.
QString ensureStyle(QindaTK::RichTextDocument &doc, Style style)
{
    const QString name = styleName(style);
    QString id = styleIdNamed(doc, name);
    if (!id.isEmpty()) {
        return id;
    }
    id = doc.createStyle(name, QStringLiteral("paragraph"));
    if (id.isEmpty() || !doc.updateStyle(id, lookOf(style))) {
        return {};
    }
    return id;
}

Table *tableOf(const QindaTK::RichTextDocument &doc, const Position &caret, int *row, int *column)
{
    doc.paragraphsInOrder(); // refreshes the derived table, row and cell
    const Paragraph *p = doc.paragraph(caret.paragraph);
    if (p == nullptr || p->table == nullptr) {
        return nullptr;
    }
    *row = p->row;
    *column = p->cell;
    return p->table;
}

} // namespace

QString styleName(Style style)
{
    switch (style) {
    case Style::Normal:
        return QStringLiteral("Normal");
    case Style::Title:
        return QStringLiteral("Title");
    case Style::Quote:
        return QStringLiteral("Quote");
    case Style::Code:
        return QStringLiteral("Code");
    default:
        return QStringLiteral("Heading %1").arg(int(style) - int(Style::Heading1) + 1);
    }
}

Style at(const QindaTK::RichTextDocument &doc, const Position &position)
{
    const Paragraph *p = doc.paragraph(position.paragraph);
    if (p == nullptr) {
        return Style::Normal;
    }
    if (const NamedStyle *named = doc.style(p->format.styleId)) {
        for (const Style candidate : {Style::Title, Style::Quote, Style::Code}) {
            if (named->name.compare(styleName(candidate), Qt::CaseInsensitive) == 0) {
                return candidate;
            }
        }
    }
    const int level = p->format.headingLevel;
    return level > 0 ? Style(int(Style::Heading1) + qMin(level, 6) - 1) : Style::Normal;
}

bool apply(QindaTK::RichTextDocument &doc, const Selection &selection, Style style)
{
    if (!doc.beginTransaction(tr("Style %1").arg(styleName(style)), {}, selection)) {
        return false;
    }
    bool ok = true;
    if (style == Style::Title || style == Style::Quote || style == Style::Code) {
        const QString id = ensureStyle(doc, style);
        ok = !id.isEmpty() && doc.applyParagraphStyle(selection.anchor, selection.focus, id);
    } else {
        // Normal and the headings: no named style, OneNote's spacing.
        ParagraphFormatPatch patch;
        patch.headingLevel = style == Style::Normal ? 0 : int(style) - int(Style::Heading1) + 1;
        patch.indentStartPt = 0.0;
        patch.spaceBeforePt = 0.0;
        patch.spaceAfterPt = 0.0;
        ok = doc.applyParagraphStyle(selection.anchor, selection.focus, QString())
            && doc.setParagraphFormat(selection.anchor, selection.focus, patch);
    }
    if (!ok) {
        doc.cancelTransaction();
        return false;
    }
    return doc.commitTransaction(selection);
}

std::optional<Position> insertLink(QindaTK::RichTextDocument &doc, const Selection &selection,
                                   const QString &href, const QString &label)
{
    if (!doc.beginTransaction(tr("Link"), {}, selection)) {
        return std::nullopt;
    }
    std::optional<Position> caret;
    if (selection.isCollapsed()) {
        CharFormat format = doc.charFormat(doc.formatIndexAt(selection.focus)).withoutMarks();
        format.objectId.clear();
        format.href = href;
        caret = doc.insertText(selection.focus, label.isEmpty() ? href : label, doc.internFormat(format));
    } else {
        CharFormatPatch patch;
        patch.href = href;
        if (doc.applyCharFormat(selection.anchor, selection.focus, patch)) {
            caret = doc.compare(selection.anchor, selection.focus) <= 0 ? selection.focus : selection.anchor;
        }
    }
    if (!caret) {
        doc.cancelTransaction();
        return std::nullopt;
    }
    if (!doc.commitTransaction({*caret, *caret})) {
        return std::nullopt;
    }
    return caret;
}

bool inTable(const QindaTK::RichTextDocument &doc, const Position &caret)
{
    int row = 0;
    int column = 0;
    return tableOf(doc, caret, &row, &column) != nullptr;
}

bool editTable(QindaTK::RichTextDocument &doc, const Position &caret, TableEdit edit)
{
    int row = 0;
    int column = 0;
    const Table *table = tableOf(doc, caret, &row, &column);
    if (table == nullptr) {
        return false;
    }
    const QString tableId = table->id.toString(QUuid::WithoutBraces);
    switch (edit) {
    case TableEdit::RowAbove:
        return doc.insertTableRow(tableId, row);
    case TableEdit::RowBelow:
        return doc.insertTableRow(tableId, row + 1);
    case TableEdit::ColumnLeft:
        return doc.insertTableColumn(tableId, column);
    case TableEdit::ColumnRight:
        return doc.insertTableColumn(tableId, column + 1);
    case TableEdit::DeleteRow:
        return doc.removeTableRow(tableId, table->rows.at(row).id.toString(QUuid::WithoutBraces));
    case TableEdit::DeleteColumn: {
        const QUuid columnId = column < table->columnIds.size() ? table->columnIds.at(column)
                                                                 : derivedColumnId(table->id, column);
        return doc.removeTableColumn(tableId, columnId.toString(QUuid::WithoutBraces));
    }
    case TableEdit::DeleteTable: {
        // The whole table goes as one body step; the body still ends with
        // the paragraph that follows it.
        const int index = table->blockIndex;
        if (index < 0 || index >= doc.blocks().size()) {
            return false;
        }
        const QVector<Block> whole{doc.blocks().at(index)};
        if (!doc.beginTransaction(tr("Delete Table"))) {
            return false;
        }
        if (doc.apply(StepBlocks{ContainerSpec{}, index, whole, doc.objectsIn(whole), false})) {
            doc.cancelTransaction();
            return false;
        }
        return doc.commitTransaction();
    }
    }
    return false;
}

} // namespace QindaOffice::Apps::Notes::RichStyle
