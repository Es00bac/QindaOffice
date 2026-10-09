// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's lists on a QTextDocument (M73, D-082): the model QindaWrite
// edits, draws and saves.
//
// A Word list paragraph is a QTextList member whose QTextListFormat
// carries Word's own description of the list (docx_properties.h):
//   kListDefinition   the list's w:abstractNum — all nine levels — as XML,
//   kListAbstractKey  that definition's identity: "src:<abstractNumId>"
//                     for one read from the package, "new:<n>" for one
//                     QindaWrite made,
//   kListNumId        the instance (w:num) the paragraph points at,
//   kListLevel        its level (w:ilvl),
//   kListStarts       the instance's w:startOverride per level (−1 none).
// Lists are numbered from those alone (docx_list_labels.h): one counter
// set per definition, or per instance when it overrides a start — Word's
// rule — so how the items are split into QTextLists does not matter, and
// every story (the body, a header, a clone for printing) carries all it
// needs. A changed list is a new list format, so Undo is the document's
// own. The QTextListFormat's Qt style, prefix and suffix are kept for HTML
// and Markdown; Qt's own marker is switched off per paragraph
// (QTextFormat::ListStyle on the block) and Word's is drawn instead.
//
// AGENT-CONTRACT: the reader (docx_reader_body.cpp, joinList) writes these
// properties and an import snapshot {num, def, starts}; the writer
// (docx_writer_numbering.cpp) keeps a list's w:num and w:abstractNum byte
// for byte while they equal it, and otherwise writes a new w:num — over
// the same abstract with w:lvlOverride/w:startOverride when only the starts
// changed, over a new w:abstractNum when the definition did.

#include "docx_numbering.h"

#include <QFont>
#include <QList>
#include <QString>
#include <QTextListFormat>
#include <QVariantMap>

#include <array>
#include <memory>

class QTextBlock;

namespace QindaOffice::Docx {

// ---- definitions --------------------------------------------------------------------

struct ListDefinition {
    std::array<NumberingLevel, 9> levels;
    std::array<bool, 9> defined{}; // the definition has a w:lvl for this level
};

// The definition `abstractXml` describes, parsed once per distinct XML
// (a bounded cache); never null.
[[nodiscard]] std::shared_ptr<const ListDefinition> listDefinition(const QString &abstractXml);

// A level's indents from its w:pPr/w:ind, in twips: the text's left
// indent (w:left or w:start) and the hanging indent (w:hanging, or minus
// w:firstLine). Word puts the number at left − hanging.
[[nodiscard]] int levelLeft(const NumberingLevel &level);
[[nodiscard]] int levelHanging(const NumberingLevel &level);
// A custom tab stop the level sets for the text after its number, or −1.
[[nodiscard]] int levelTab(const NumberingLevel &level);

// `value` in Word's number format: decimal, decimalZero, upperRoman,
// lowerRoman, upperLetter, lowerLetter, ordinal, cardinalText,
// ordinalText; anything else as decimal. Bullets and "none" are empty.
[[nodiscard]] QString wordNumberText(int value, const QString &numFmt);

// The Qt list style that stands for a level in HTML and Markdown.
[[nodiscard]] QTextListFormat::Style qtStyleFor(const NumberingLevel &level);

// ---- the library (Home ▸ Bullets, Numbering, Multilevel List) ---------------------

// Word's bullet definition: `glyph` (as Word stores it: a symbol font's
// code point in U+F0xx, or a plain character) in `font` (empty: the
// paragraph's) and `color` ("RRGGBB" or empty) at level 1, Word's default
// bullets below it; number at 0.25", text at 0.5" per level.
[[nodiscard]] QString bulletDefinition(const QString &glyph, const QString &font, const QString &color = {});
// Word's numbering definition: level 1 in `numFmt` with `text` ("%1.",
// "%1)", "Step %1:"), `start` and alignment `jc`; Word's default levels
// below it (a. i. 1. …).
[[nodiscard]] QString numberingDefinition(const QString &numFmt, const QString &text, const QString &jc = QStringLiteral("left"),
                                          int start = 1);
// Word's multilevel list library: 0 "1. 1.1. 1.1.1.", 1 "1) a) i)",
// 2 bullets "➢ ▪ •", 3 "Article I. / Section 1.01 / (a)".
inline constexpr int kMultilevelPresets = 4;
[[nodiscard]] QString multilevelDefinition(int preset);

// ---- one level, as QindaWrite's dialogs see it ---------------------------------------

// {format, text, start, justification, suffix ("tab", "space", "nothing"),
//  restartAfter (−2 after any higher level, −1 never, 0–7 after that
//  level), legal, numberPosition (twips: left − hanging), textIndent
//  (twips: left), tab (twips or −1), font, color ("RRGGBB" or "")}
[[nodiscard]] QVariantMap levelSpec(const QString &abstractXml, int ilvl);
// The definition with level `ilvl` changed to `spec` (keys it lacks are
// kept; the level's other markup and the other levels stay verbatim).
// The result is a new definition: no nsid, template or style link.
[[nodiscard]] QString withLevel(const QString &abstractXml, int ilvl, const QVariantMap &spec);

// The marker a level shows for the numbers `counters` (values of levels
// 0–8), Word's way: %1–%9 replaced by each level's number in its format
// (legal numbering: ancestors in arabic numerals).
[[nodiscard]] QString levelLabel(const ListDefinition &definition, int ilvl, const std::array<int, 9> &counters);

// ---- list formats --------------------------------------------------------------------

[[nodiscard]] bool isWordList(const QTextListFormat &format);
// A list format for level `level` of an instance: Word's properties plus
// the Qt style, prefix and suffix that stand for it in HTML.
[[nodiscard]] QTextListFormat wordListFormat(const QString &definition, const QString &abstractKey, int numId, int level,
                                             const QList<int> &starts);
// kListStarts as nine ints (−1 none).
[[nodiscard]] QList<int> listStarts(const QTextListFormat &format);

} // namespace QindaOffice::Docx
