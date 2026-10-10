// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A hand-built formatted text block for the M84 gates (D-093): every run
// format, the Title, Heading 2, Quote and Code styles, a bulleted and a
// numbered list, a table with a header row, and three tags (two To Do —
// one done — and a Question). No user notebook is read.

#include "qindanote/src/note_rich_style.h"
#include "qindanote/src/note_rich_text.h"
#include "qindanote/src/note_tags.h"

#include <qindatk/rich_text_document.h>

#include <QJsonArray>
#include <QJsonObject>

namespace NoteFixture {

inline const QColor kRed(0xc0, 0x1c, 0x28);
inline const QColor kMark(0xff, 0xe1, 0x4d);

inline QindaTK::RichText::Position at(const QindaTK::RichTextDocument &doc, int paragraph, int offset)
{
    return {doc.paragraphsInOrder().at(paragraph)->id, offset};
}

inline QJsonObject formattedTextBlock()
{
    using namespace QindaOffice::Apps::Notes;
    using namespace QindaTK::RichText;
    auto doc = RichBlock::fromPlainText(QStringLiteral(
        "Bold Italic Under Strike Super Sub Red Mark Link\nA title\nHeading two\nA quote\nsome_code()\n"
        "Bullet\nNumber\nCall the bank\nPay the rent"));
    const QString first = doc->paragraphsInOrder().first()->text;
    const auto format = [&](const char *word, const auto &set) {
        CharFormatPatch patch;
        set(patch);
        const int from = int(first.indexOf(QLatin1String(word)));
        doc->applyCharFormat(at(*doc, 0, from), at(*doc, 0, from + int(qstrlen(word))), patch);
    };
    format("Bold", [](CharFormatPatch &p) { p.weight = 700; });
    format("Italic", [](CharFormatPatch &p) { p.italic = 1; });
    format("Under", [](CharFormatPatch &p) { p.underline = 1; });
    format("Strike", [](CharFormatPatch &p) { p.strike = 1; });
    format("Super", [](CharFormatPatch &p) { p.verticalAlign = VerticalAlign::Superscript; });
    format("Sub", [](CharFormatPatch &p) { p.verticalAlign = VerticalAlign::Subscript; });
    format("Red", [](CharFormatPatch &p) { p.color = kRed; });
    format("Mark", [](CharFormatPatch &p) { p.background = kMark; });
    format("Link", [](CharFormatPatch &p) { p.href = QStringLiteral("https://example.org/"); });
    RichStyle::apply(*doc, {at(*doc, 1, 0), at(*doc, 1, 0)}, RichStyle::Style::Title);
    RichStyle::apply(*doc, {at(*doc, 2, 0), at(*doc, 2, 0)}, RichStyle::Style::Heading2);
    RichStyle::apply(*doc, {at(*doc, 3, 0), at(*doc, 3, 0)}, RichStyle::Style::Quote);
    RichStyle::apply(*doc, {at(*doc, 4, 0), at(*doc, 4, 0)}, RichStyle::Style::Code);
    doc->setList(at(*doc, 5, 0), at(*doc, 5, 0), ListKind::Disc);
    doc->setList(at(*doc, 6, 0), at(*doc, 6, 0), ListKind::Decimal);
    doc->insertTable(at(*doc, 8, 12), 2, 2, 1);
    for (const Block &block : doc->blocks()) {
        if (block.table) {
            const QString table = block.table->id.toString(QUuid::WithoutBraces);
            doc->setTableCellText(table, block.table->rows.at(0).cells.at(0).id.toString(QUuid::WithoutBraces),
                                  QStringLiteral("Head"));
            doc->setTableCellText(table, block.table->rows.at(1).cells.at(1).id.toString(QUuid::WithoutBraces),
                                  QStringLiteral("Cell"));
        }
    }
    const QStringList ids = RichBlock::paragraphIds(*doc);
    QJsonObject text{{QStringLiteral("type"), QStringLiteral("text")}};
    QJsonObject done = Tags::entryFor(Tags::byId(QStringLiteral("todo")), ids.at(8));
    done.insert(QStringLiteral("done"), true);
    text.insert(QStringLiteral("tags"), QJsonArray{Tags::entryFor(Tags::byId(QStringLiteral("todo")), ids.at(7)), done,
                                                   Tags::entryFor(Tags::byId(QStringLiteral("question")), ids.at(2))});
    RichBlock::store(&text, *doc);
    return text;
}

// A checklist item tagged Important (a whole-block tag).
inline QJsonObject taggedCheckBlock()
{
    using namespace QindaOffice::Apps::Notes;
    return {{QStringLiteral("type"), QStringLiteral("check")},
            {QStringLiteral("text"), QStringLiteral("Send the report")},
            {QStringLiteral("tags"), QJsonArray{Tags::entryFor(Tags::byId(QStringLiteral("important")), QString())}}};
}

} // namespace NoteFixture
