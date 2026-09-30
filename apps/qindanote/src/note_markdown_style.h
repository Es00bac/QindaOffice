// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The typesetting vocabulary of markdown blocks beyond GitHub Markdown
// (M57, D-063), shared by the screen and the printer through
// note_markdown.cpp:
//
//   inline   ==key==  =={term}…==  =={example}…==  =={warn}…==
//            =={question}…==  =={date}…==   ++underline++   !!stand-out!!
//            x^2^ (superscript)   H~2~O (subscript)   [#tag] badges
//            [mm:ss] timestamps (small, grey)   [[ai: …]] AI notes (M56)
//   blocks   > [!kind] Title   callout boxes: def, key, example, warn, tip,
//            question, mnemonic, summary, quote, big, note (any other kind)
//   tables   ruled, padded, a tinted header row
//
// The markup is Obsidian-compatible where Obsidian has an equivalent
// (==highlight==, > [!callout]), so exported Markdown still reads well.
//
// AGENT-CONTRACT: office_ai's LiveNotesLayouts::styleGuide() teaches the
// model exactly this vocabulary; change both together. Colours are PAPER
// colours (D-004), identical on every theme.

#include <QString>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Apps::Notes::MarkdownStyle {

// Replaces the inline markup in `markdown` with private-use sentinels that
// survive the Markdown parser (applyInline() turns them into formats).
[[nodiscard]] QString markInline(const QString &markdown);
// Formats and removes the sentinels markInline() left in `document`.
void applyInline(QTextDocument *document);

// Inserts `markdown` at `cursor`: ordinary Markdown as it is, each
// "> [!kind]" callout as a tinted, ruled frame with its label.
void insertWithCallouts(QTextCursor &cursor, const QString &markdown);

// Rules and pads every table; tints and bolds its first row.
void styleTables(QTextDocument *document);
// Collapses the empty paragraph Qt keeps next to every frame (a callout or
// a table), which would otherwise show as a blank line around each box.
void tightenFrames(QTextDocument *document);

} // namespace QindaOffice::Apps::Notes::MarkdownStyle
