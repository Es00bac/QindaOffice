// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word borders in Word's own terms (M70, D-079): the sides of a w:pBdr,
// w:tcBorders or w:tblBorders as the values Word wrote — line style
// (w:val), width in eighths of a point (w:sz), colour (RRGGBB or "auto",
// theme colours resolved), spacing in points (w:space) — so a border the
// user did not touch is written back from its source markup and a changed
// side is written exactly as Word writes one.
//
// The canonical text form (bordersSpec) is what the import snapshots
// compare: "bottom=single/4/auto/1;top=double/12/1F4E79/0". Sides are
// Word's names with start/end folded into left/right; an absent side means
// "not specified here" (inherited), "nil" means "explicitly none".
//
// AGENT-CONTRACT: the reader stores bordersSpec(readBorders(…)) on the
// paragraph (kParagraphBorderSpec), table (kTableBorders) and cell
// (kCellBorders) formats and in their snapshots; the writer compares the
// two and calls writeBorders(); QindaWrite's border commands set the same
// properties through docx_table_format.h / docx_paragraph_format.h. All of
// them go through this file, so they agree on what "unchanged" means.

#include "ooxml_theme.h"
#include "ooxml_xml.h"

#include <QColor>
#include <QMap>
#include <QString>
#include <QTextFrameFormat>

namespace QindaOffice::Docx {

struct WordBorder {
    QString style = QStringLiteral("single"); // w:val: single, double, dotted, dashed, thick, nil, …
    int size = 4;                              // w:sz, eighths of a point (2–96)
    QString color = QStringLiteral("auto");   // w:color: RRGGBB or auto
    int space = 0;                             // w:space, points

    [[nodiscard]] bool isNone() const { return style == u"nil" || style == u"none"; }
    bool operator==(const WordBorder &) const = default;

    [[nodiscard]] static WordBorder none() { return {QStringLiteral("nil"), 0, QStringLiteral("auto"), 0}; }
};

// side ("top", "left", "bottom", "right", "insideH", "insideV", "between",
// "bar", "tl2br", "tr2bl") → border.
using WordBorders = QMap<QString, WordBorder>;

[[nodiscard]] QString bordersSpec(const WordBorders &borders);
[[nodiscard]] WordBorders parseBordersSpec(QStringView spec);

// The sides `element` (a w:pBdr / w:tcBorders / w:tblBorders, or null)
// specifies, colours resolved through `theme`.
[[nodiscard]] WordBorders readBorders(const Ooxml::XmlElement *element, const Ooxml::Theme &theme);

// Rewrites `container`'s children to `now`: a side whose value equals its
// value in `then` keeps `source`'s own element for it (theme colour
// attributes, w:shadow, w:frame and all), every other side is written the
// way Word writes one (w:val, w:sz, w:space, w:color; "nil" alone). Sides
// absent from `now` are dropped.
void writeBorders(Ooxml::XmlElement &container, const WordBorders &now, const WordBorders &then,
                  const Ooxml::XmlElement *source);

// How Qt shows one side: width in document px (0 = none), Qt's nearest
// line style, colour (black for auto). The table reader and the border
// commands both use it, so a border looks the same read or applied.
struct DisplayBorder {
    qreal width = 0;
    QTextFrameFormat::BorderStyle style = QTextFrameFormat::BorderStyle_Solid;
    QColor color = Qt::black;
};
[[nodiscard]] DisplayBorder displayBorder(const WordBorder &border);

// The kParagraphBorders display spec (docx_properties.h) drawParagraphBorders
// paints, for a paragraph's Word borders.
[[nodiscard]] QString paragraphDisplaySpec(const WordBorders &borders);

// Cell margins (w:tblCellMar, w:tcMar) in twips, -1 where a side is not
// specified: "top,left,bottom,right".
struct CellMargins {
    int top = -1;
    int left = -1;
    int bottom = -1;
    int right = -1;
    [[nodiscard]] bool isEmpty() const { return top < 0 && left < 0 && bottom < 0 && right < 0; }
    bool operator==(const CellMargins &) const = default;
};
[[nodiscard]] QString marginsSpec(const CellMargins &margins);
[[nodiscard]] CellMargins parseMarginsSpec(QStringView spec);
[[nodiscard]] CellMargins readMargins(const Ooxml::XmlElement *element);
// Rewrites `container` (w:tblCellMar / w:tcMar) to `margins` as w:top,
// w:left, w:bottom, w:right with w:type="dxa".
void writeMargins(Ooxml::XmlElement &container, const CellMargins &margins);

// A Word width (w:tblW, w:tcW): "auto:0", "dxa:N" (twips), "pct:N"
// (fiftieths of a percent), "nil:0".
struct WordWidth {
    QString type = QStringLiteral("auto");
    int value = 0;
    bool operator==(const WordWidth &) const = default;
};
[[nodiscard]] QString widthSpec(const WordWidth &width);
[[nodiscard]] WordWidth parseWidthSpec(QStringView spec);
[[nodiscard]] WordWidth readWidth(const Ooxml::XmlElement *element);
void writeWidth(Ooxml::XmlElement &element, const WordWidth &width);

} // namespace QindaOffice::Docx
