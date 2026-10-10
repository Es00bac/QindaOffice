// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Printing a page across sheets (M87, D-095; Visio's Print Setup): where
// each sheet of a page's printout cuts the drawing, for the paged painter
// (export.cpp) and for the page breaks the canvas shows.
//
// AGENT-CONTRACT (with DiagramHost::printPageLayout and the page view): a
// sheet's printable area is the paper less kPrintMargin on every side —
// the host asks the printer for exactly that margin, the screen's page
// breaks assume it. Sheets are taken left to right, then top to bottom.

#include "drawing.h"

#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace QindaOffice::Diagram {

// A quarter inch, in page units.
inline constexpr double kPrintMargin = 24;

struct PrintTile {
    QRectF region;      // the part of the page on this sheet (page units)
    double scale = 1;   // device units per page unit
    QPointF offset;     // where `region`'s top-left lands on the sheet (device units)
    bool turned = false; // the page is turned a quarter on the sheet
};

// The sheets page `page` prints on, for a printable area of `sheet` device
// units at `dpi` device units per inch. The default setup keeps M50's rule:
// one sheet, the page fitted on it, turned a quarter when the page and the
// sheet differ in orientation.
[[nodiscard]] QVector<PrintTile> printTiles(const Page &page, QSizeF sheet, double dpi);

// Where the sheets meet, in page units, for the screen (empty when the page
// prints on one sheet). The sheet is the setup's paper less the margins.
[[nodiscard]] QVector<QRectF> pageBreakTiles(const Page &page);

} // namespace QindaOffice::Diagram
