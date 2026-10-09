// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QTextFormat properties that carry WordprocessingML semantics on a
// QTextDocument (D-068). Offsets are never reused: append new ones at the
// end of the M73 block (85 is the last taken) or of the M77 block, which
// starts at 120 (123 is its last); 86–119 are left to other milestones.
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
// Word content controls (w:sdt, docx_controls.h): the keys of the controls
// (WordParts::contentControls) a run is inside (char formats), or a
// paragraph or table is inside (block and table formats), outermost first.
inline constexpr int kContentControls = kDocxBase + 44; // QStringList
inline constexpr int kBlockControls = kDocxBase + 45;   // QStringList

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

// ---- M70 (D-079): Word's table, paragraph and font formatting ----------------------
// Word values Qt's formats cannot hold, read from the effective markup
// (style chain included, unless "own" says otherwise), changed by
// QindaWrite's formatting commands and dialogs, and part of the import
// snapshots (blockSnapshot / charSnapshot / the table, row and cell
// snapshots), so an untouched value is written from its source markup and
// a changed one re-derived (D-068 rule 3). Offsets 44/45 belong to content
// controls; M70 uses 60–81 (71 and 78 unused).
// Block formats.
// The paragraph's OWN spacing (w:spacing before/after in twips, and the
// auto-spacing flags). Qt's top/bottom margins hold the gaps the reader
// folded (Word adds after + before; Qt keeps the larger), so the writer
// and the Paragraph dialog read this, never the margins.
inline constexpr int kParagraphSpacing = kDocxBase + 60;   // QVariantList{int before, int after, bool autoBefore, bool autoAfter}
inline constexpr int kContextualSpacing = kDocxBase + 61;  // bool: w:contextualSpacing (set only when true)
inline constexpr int kOutlineLevel = kDocxBase + 62;       // int 0–8, 9 = body text: w:outlineLvl
// Tab stops as Word positions them (from the text column's edge, twips)
// with alignment and leader (tabStopsSpec, docx_paragraph_format.h); Qt's
// QTextOption::Tab has no leader and measures from the left indent.
inline constexpr int kTabStops = kDocxBase + 63;           // QString
inline constexpr int kParagraphBorderSpec = kDocxBase + 64; // QString bordersSpec() of w:pBdr (docx_borders.h)
// Char formats.
inline constexpr int kUnderlineWord = kDocxBase + 66; // QString w:u/@w:val Qt's underline styles cannot say
inline constexpr int kDoubleStrike = kDocxBase + 67;  // bool w:dstrike (drawn as one line)
inline constexpr int kHiddenText = kDocxBase + 68;    // bool w:vanish (shown, kept)
inline constexpr int kKerning = kDocxBase + 69;       // int w:kern, half-points (0 = none)
// Table formats.
// The table-level borders and default cell margins in effect (its style's
// with its own w:tblPr over them); written as the table's own when changed.
inline constexpr int kTableBorders = kDocxBase + 70;      // QString bordersSpec(): w:tblBorders
inline constexpr int kTableWidth = kDocxBase + 72;        // QString widthSpec() of w:tblW
inline constexpr int kTableIndent = kDocxBase + 73;       // int twips: w:tblInd
inline constexpr int kTableLayout = kDocxBase + 74;       // QString "autofit" | "fixed": w:tblLayout
inline constexpr int kTableCellMargins = kDocxBase + 75;  // QString marginsSpec(): w:tblCellMar
// Cell formats; the row's own properties sit on its first cell, beside kRowXml.
inline constexpr int kCellBorders = kDocxBase + 76;   // QString bordersSpec() of the cell's own w:tcBorders
inline constexpr int kCellMargins = kDocxBase + 77;   // QString marginsSpec() of the cell's own w:tcMar
inline constexpr int kRowHeightRule = kDocxBase + 79; // QString "atLeast" | "exact" (with kRowHeight)
inline constexpr int kRowCantSplit = kDocxBase + 80;  // bool w:cantSplit
// Display only: the padding applyRowHeights() added to a cell, so a new
// row height replaces it instead of adding to it.
inline constexpr int kRowPad = kDocxBase + 81; // qreal px

// ---- M73 (D-082): Word's lists and styles --------------------------------------------
// List formats: Word's own description of the list a paragraph is an item
// of (docx_lists.h). With these, docx_list_labels.h numbers and draws the
// list as Word does and the writer saves it as real w:abstractNum / w:num.
inline constexpr int kListDefinition = kDocxBase + 82;  // QString: the w:abstractNum (nine levels) as XML
inline constexpr int kListStarts = kDocxBase + 83;      // QVariantList of 9 ints: w:startOverride per level, −1 none
inline constexpr int kListAbstractKey = kDocxBase + 84; // QString: "src:<abstractNumId>" read, "new:<n>" made in Write
// Block formats: where a list paragraph's number stands — Word's hanging
// indent (left − hanging), in px; the text starts at the left indent.
inline constexpr int kListHanging = kDocxBase + 85; // qreal px

// ---- M77 (D-086): endnotes and floating pictures -------------------------------------
// An endnote's reference mark (char formats): the note id, as
// kFootnoteIdProperty is a footnote's. The note text lives in
// WordParts::endnotes; Write numbers the marks in document order in the
// endnote number format (i, ii, iii by default, docx_notes.h).
// AGENT-CONTRACT: apps/qindawrite/src/write_footnotes.h kEndnoteIdProperty
// static_asserts equality with this one.
inline constexpr int kEndnoteIdProperty = kDocxBase + 120; // QString note id
// A picture's layout as Word's wp:anchor describes it (docx_float.h,
// PictureFloat::toMap): wrap, horizontal and vertical position with what
// they are relative to, wrap distances. Absent on an inline picture. The
// snapshot is the map as read: while both match (and the size is as read),
// the writer keeps the picture's own drawing byte for byte.
inline constexpr int kPictureFloat = kDocxBase + 121;         // QVariantMap
inline constexpr int kPictureFloatSnapshot = kDocxBase + 122; // QVariantMap
// Display only, beside kRowPad: the part of a row height's padding put on
// top of a cell, so a vertically centred (or bottom) cell stays centred in
// a row Word gives a height (docx_table_format.h refreshRowHeights).
inline constexpr int kRowPadTop = kDocxBase + 123; // qreal px

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
