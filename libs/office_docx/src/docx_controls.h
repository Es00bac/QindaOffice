// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word content controls (w:sdt; D-068, D-080): read into
// WordParts::contentControls, found in the text, written back.
//
// In the text a control is its content: runs carry its key in
// kContentControls (an inline control, inside one paragraph), paragraphs
// and tables in kBlockControls (a block-level control around them),
// outermost first. The control's w:sdtPr is kept as read and patched on
// save for what changed: a check box's state follows the symbol in its
// text, a date control's w:fullDate its text, a list's w:lastValue the
// chosen item; edited content drops w:showingPlcHdr (it is no longer the
// placeholder) and w:dataBinding (Word would refill the control from its
// bound XML on open, undoing the edit).
//
// AGENT-CONTRACT: the reader (docx_reader_body.cpp, docx_reader_runs.cpp,
// docx_reader_table.cpp) registers each control with parseControl() and
// tags its content; the body writer (docx_writer_body.cpp,
// docx_writer_controls.cpp) groups tagged content back into w:sdt elements
// with controlProperties(); QindaWrite (write_controls.h) fills check
// boxes, lists and dates through controlAt(). Row- and cell-level controls
// (repeating sections) are read as their content only.

#include "docx_model.h"
#include "ooxml_xml.h"

#include <QHash>

namespace QindaOffice::Docx {

// One w:sdt's properties as a control: kind, check box symbols, list
// items, date format, placeholder flag, alias and tag; the markup kept.
[[nodiscard]] ContentControl parseControl(const Ooxml::XmlElement &sdt, bool block);

// Each control's text in `document`: its runs' text (inline) or its
// paragraphs' and tables' text, paragraphs joined with '\n' (block-level).
// Comment marks (U+200B) and objects (U+FFFC) are left out.
[[nodiscard]] QHash<QString, QString> controlTexts(const QTextDocument &document);

// The w:sdtPr to write for `control` whose content now reads `text`;
// `repeat` drops w:id (a copy of a control already written once).
[[nodiscard]] Ooxml::XmlElementPtr controlProperties(const ContentControl &control, const QString &text,
                                                     bool repeat);

// The innermost control at `position` (an inline one before a block-level
// one) and its content's extent [start, end); an empty key when none.
struct ControlSpan {
    QString key;
    int start = -1;
    int end = -1;
};
[[nodiscard]] ControlSpan controlAt(const QTextDocument &document, int position);

// Qt's date format for Word's w:dateFormat: the letters are the same
// (d … dddd, M … MMMM, yy, yyyy, h, H, m, s) except Word's AM/PM forms.
[[nodiscard]] QString qtDateFormat(const QString &wordFormat);

} // namespace QindaOffice::Docx
