// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QTextFormat properties that carry WordprocessingML semantics on a
// QTextDocument (D-068).
//
// Two groups:
//
// 1. Properties QindaWrite already defines for its own features. A docx
//    footnote IS a Write footnote marker, a w:ins run IS a Write insertion
//    mark — so the reader produces exactly Write's representation and the
//    writer consumes it.
//    AGENT-CONTRACT: the values below must equal QindaWrite's constants
//    (apps/qindawrite/src/style_catalog.h kStyleIdProperty, write_toc.h
//    kTocBlockProperty, write_footnotes.h kFootnoteIdProperty,
//    write_revisions.h kRevisionInsert/DeleteProperty). Write's sources
//    static_assert the equality; change both sides together or neither.
//
// 2. office_docx's own, in a reserved block well above every app-defined
//    property. They hold PRESERVED markup (self-contained XML fragments,
//    see ooxml_xml.h) and IMPORT SNAPSHOTS (the Qt values the reader
//    mapped). The writer compares a format's current values with its
//    snapshot: unchanged properties are written from the preserved markup
//    verbatim, changed ones are re-derived (D-068 rule 3).
//
// AGENT-GUARD: Qt copies a block's format into the block created by Enter
// and a run's format into newly typed text. That is intended — the new
// paragraph/run has the same Word formatting — and is why snapshots live on
// the format rather than in a side table keyed by position.

#include <QColor>
#include <QTextFormat>

namespace QindaOffice::Docx {

// ---- shared with QindaWrite --------------------------------------------------------
inline constexpr int kWriteStyleIdProperty = QTextFormat::UserProperty + 1;   // int StyleCatalog id
inline constexpr int kWriteTocBlockProperty = QTextFormat::UserProperty + 7;  // bool
inline constexpr int kFootnoteIdProperty = QTextFormat::UserProperty + 8;     // QString note id
inline constexpr int kRevisionInsertProperty = QTextFormat::UserProperty + 9; // QString author
inline constexpr int kRevisionDeleteProperty = QTextFormat::UserProperty + 10; // QString author

// ---- office_docx --------------------------------------------------------------------
inline constexpr int kDocxBase = QTextFormat::UserProperty + 0x400;

// Block formats.
inline constexpr int kParagraphStyleId = kDocxBase + 1;  // QString w:pStyle value
inline constexpr int kParagraphXml = kDocxBase + 2;      // QString preserved w:pPr
inline constexpr int kParagraphSnapshot = kDocxBase + 3; // QVariantMap mapped values at import
inline constexpr int kSectionXml = kDocxBase + 4;        // QString w:sectPr closing a section here
inline constexpr int kPageBreakRun = kDocxBase + 5;      // bool: break came from <w:br w:type="page"/>
// An empty block Qt requires (before a leading table, between adjacent
// tables) that has no w:p behind it; the writer skips it while empty.
inline constexpr int kParagraphSynthetic = kDocxBase + 6; // bool
// Word paragraph borders (w:pBdr) for display: "side:widthPx:style:#rrggbb:spacePx;…"
// with side top/bottom/left/right/between. Qt draws no paragraph borders;
// drawParagraphBorders() paints these (docx_text.h).
inline constexpr int kParagraphBorders = kDocxBase + 7; // QString
// Word's pagination rules, read from the effective w:pPr (style chain
// included) for display; the preserved markup carries them on save.
inline constexpr int kKeepWithNext = kDocxBase + 8; // bool: w:keepNext
inline constexpr int kWidowControl = kDocxBase + 9; // bool, set only when false: w:widowControl="0"
// A display-only page break QindaWrite added for those rules (Qt's layout
// has neither): the value is the block's OWN QTextFormat::PageBreakFlags.
// AGENT-CONTRACT: ownPageBreaks() (docx_format.h) is what the writer and
// the snapshot read; apps/qindawrite/src/write_pagination.cpp sets and
// clears it.
inline constexpr int kSyntheticBreak = kDocxBase + 24; // int
// A display-only line height (Word's font metrics, docx_metrics.h): the
// layout's is LineDistanceHeight; this keeps the paragraph's OWN line
// height type and value. AGENT-CONTRACT: ownLineSpacing() (docx_format.h)
// is what the snapshot and the writer read.
inline constexpr int kLineSpacingOwn = kDocxBase + 25; // QVariantList{int type, qreal value}
// A picture's alternative text (wp:docPr/@descr), shown to screen readers
// and kept with the picture; edited in QindaWrite's Picture dialog.
inline constexpr int kPictureAltText = kDocxBase + 26; // QString
// Word comments (docx_comments.h): the ids of the comments whose range
// covers a run, and the comment a reference mark (U+200B) stands for.
inline constexpr int kCommentIds = kDocxBase + 27;       // QStringList
inline constexpr int kCommentReference = kDocxBase + 28; // QString

// Char formats (QTextImageFormat included).
inline constexpr int kRunStyleId = kDocxBase + 10;      // QString w:rStyle value
inline constexpr int kRunXml = kDocxBase + 11;          // QString preserved w:rPr
inline constexpr int kRunSnapshot = kDocxBase + 12;     // QVariantMap mapped values at import
inline constexpr int kFieldInstruction = kDocxBase + 13; // QString: this run shows a field result
inline constexpr int kInlineXml = kDocxBase + 14;       // QString preserved w:r on a U+FFFC
inline constexpr int kInlineSourcePart = kDocxBase + 15; // QString part its r:ids resolve against
inline constexpr int kRevisionDate = kDocxBase + 16;    // QDateTime of an ins/del mark
inline constexpr int kDrawingXml = kDocxBase + 17;      // QString preserved w:drawing of a picture
inline constexpr int kDrawingSize = kDocxBase + 18;     // QSizeF picture size at import (px)
inline constexpr int kInlineLabel = kDocxBase + 19;     // QString what a preserved object is
// Distinguishes two adjacent preserved objects with identical markup: the
// writer emits one object per contiguous run of the same serial.
inline constexpr int kInlineSerial = kDocxBase + 23;    // int

// List formats.
inline constexpr int kListNumId = kDocxBase + 20;    // int w:numId
inline constexpr int kListLevel = kDocxBase + 21;    // int w:ilvl
inline constexpr int kListSnapshot = kDocxBase + 22; // QVariantMap

// Table formats (QTextTableFormat) and cell formats.
inline constexpr int kTableXml = kDocxBase + 30;      // QString preserved w:tblPr
inline constexpr int kTableGridXml = kDocxBase + 31;  // QString preserved w:tblGrid
inline constexpr int kTableSnapshot = kDocxBase + 32; // QVariantMap
inline constexpr int kCellXml = kDocxBase + 35;       // QString preserved w:tcPr
inline constexpr int kRowXml = kDocxBase + 36;        // QString preserved w:trPr (row's first cell)
inline constexpr int kCellSnapshot = kDocxBase + 37;  // QVariantMap
// A row's minimum height (w:trHeight, px) on the row's first cell; Qt has
// no row heights, so applyRowHeights() pads the cells (docx_text.h).
inline constexpr int kRowHeight = kDocxBase + 38;     // qreal

// How QindaWrite shows a tracked change: blue underlined insertions, red
// struck deletions — content colours, the same on screen, paper and PDF.
// AGENT-CONTRACT: write_revisions.cpp marks with exactly these; the .docx
// reader shows imported revisions the same way and the writer never turns
// them into real formatting.
inline constexpr QRgb kRevisionInsertColor = 0x1a5fb4;
inline constexpr QRgb kRevisionDeleteColor = 0xc0392b;

// The scheme of resource names under which a document's pictures are
// registered with its QTextDocument ("qwmedia:image3.png"); the bytes live in
// WordParts::media under the same name.
inline constexpr char16_t kMediaScheme[] = u"qwmedia";

} // namespace QindaOffice::Docx
