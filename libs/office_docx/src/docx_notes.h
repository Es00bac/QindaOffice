// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Footnote and endnote options and numbers as Word keeps them (M77, D-086).
//
// Word's Footnote and Endnote dialog sets, per kind, the number format
// (1 2 3 for footnotes, i ii iii for endnotes by default), the first
// number, whether numbering restarts (each section, each page for
// footnotes) and where the notes go (bottom of the page; end of the
// document or of the section). Word stores them in settings.xml
// (w:footnotePr / w:endnotePr, document-wide) and, when a section has its
// own, in that section's w:sectPr; the section's win. NoteOptions is what
// QindaWrite models: one set per kind for the whole document.

#include "ooxml_xml.h"

#include <QString>

namespace QindaOffice::Docx {

struct NoteOptions {
    // w:numFmt: decimal, lowerRoman, upperRoman, lowerLetter, upperLetter,
    // chicago (* † ‡ §). Empty: the kind's default (noteFormat()).
    QString numberFormat;
    int start = 1;   // w:numStart
    // w:numRestart: continuous (empty), eachSect, eachPage (footnotes only).
    QString restart;
    // w:pos: footnotes pageBottom (empty) or beneathText; endnotes docEnd
    // (empty) or sectEnd.
    QString position;

    bool operator==(const NoteOptions &) const = default;
};

// The format in effect: `options`' own, or Word's default for the kind.
[[nodiscard]] QString noteFormat(const NoteOptions &options, bool endnote);

// `value` (1-based) in a Word note number format: 1, i, I, a, A, or the
// Chicago symbols * † ‡ § repeated (**, ††, … past the fourth).
[[nodiscard]] QString noteNumber(int value, const QString &format);

// Reads w:footnotePr / w:endnotePr (settings or a section) over `base`:
// what the element says replaces the base's value.
[[nodiscard]] NoteOptions readNoteOptions(const Ooxml::XmlElement *notePr, NoteOptions base = {});

// Writes `options` into a w:footnotePr / w:endnotePr: its w:pos, w:numFmt,
// w:numStart and w:numRestart are replaced (defaults leave them out) and
// put first, in schema order; anything else it holds (the separator
// w:footnote / w:endnote ids of settings.xml) stays after them.
void writeNoteOptions(Ooxml::XmlElement &notePr, const NoteOptions &options);

// A note's text as QindaWrite stores it: its paragraphs, plain, joined by
// a space (w:tab as a space; field codes left out).
[[nodiscard]] QString noteText(const Ooxml::XmlElement &note);

} // namespace QindaOffice::Docx
