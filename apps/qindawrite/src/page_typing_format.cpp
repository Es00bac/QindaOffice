// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_typing_format.h"
#include "docx_math.h"
#include "docx_graphics.h"
#include "docx_properties.h"
#include "docx_text.h"
#include <QTextTable>
#include <QTextBlock>
namespace QindaOffice::Apps::Write {
// The format typing continues in: the cursor's, without a comment, footnote
// or endnote mark's identity — a mark is one character (or its number), and
// text typed after it is text (docx_comments.h, write_footnotes.h).
QTextCharFormat typingFormat(const QTextCursor &cursor)
{
    return typingFormat(cursor, cursor.charFormat());
}
QTextCharFormat typingFormat(const QTextCursor &cursor, QTextCharFormat format)
{
    if (format.isImageFormat() || format.hasProperty(Docx::kGraphicKind)) format = cursor.block().charFormat();
    // AGENT-GUARD: a reference command can be invoked directly beside a
    // graphic, bypassing the control's pinned typing format. New text must
    // never carry drawing identity into the native island writer.
    for (int key : {Docx::kGraphicKind, Docx::kGraphicData, Docx::kGraphicSnapshot, Docx::kGraphicSource})
        format.clearProperty(key);
    format.clearProperty(Docx::kCommentReference);
    if (format.hasProperty(Docx::kInlineXml)) {
        format.setObjectType(QTextFormat::NoObject);
        for (int key : {Docx::kInlineXml, Docx::kInlineSourcePart, Docx::kInlineSerial, Docx::kInlineLabel})
            format.clearProperty(key);
    }
    // AGENT-NOTE (M70, D-079): at the start of a table cell Qt hands out
    // the CELL's format (the character before the caret is the cell's
    // separator). Its shading, borders and Word cell markup are not the
    // typed text's: typing into a shaded cell wrote run shading into the
    // .docx and the HTML (docx_text.h withoutCellProperties).
    if (const QTextTable *table = cursor.currentTable()) {
        const QTextTableCell cell = table->cellAt(cursor);
        format = Docx::withoutCellProperties(format, cell.isValid() ? cell.format() : QTextCharFormat());
    }
    // Text typed after a footnote or endnote mark is text, not more of the
    // mark (the next renumbering replaced it with the number; M77, D-086).
    if (format.hasProperty(Docx::kFootnoteIdProperty) || format.hasProperty(Docx::kEndnoteIdProperty)) {
        const QString style = format.stringProperty(Docx::kRunStyleId);
        for (const int p : {Docx::kFootnoteIdProperty, Docx::kEndnoteIdProperty, int(QTextFormat::TextVerticalAlignment),
                            Docx::kRunXml, Docx::kRunSnapshot}) {
            format.clearProperty(p); // its identity, superscript and the reference run's markup
        }
        if (style == u"FootnoteReference" || style == u"EndnoteReference") {
            format.clearProperty(Docx::kRunStyleId);
        }
    }
    if (format.stringProperty(Docx::kRunStyleId) == u"PlaceholderText") {
        // Text typed into a form field's placeholder is its value (D-080).
        format.clearProperty(Docx::kRunStyleId);
        format.clearProperty(Docx::kRunXml);
        format.clearProperty(Docx::kRunSnapshot);
        format.clearForeground();
    }
    return format;
}

}
