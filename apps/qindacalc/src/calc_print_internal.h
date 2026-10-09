// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the sheet printer's two files share (calc_print.h; M78, D-087;
// split by M82): the canvas that turns points into the printer's device
// pixels, the printed rows and columns of a page, and the page's cells
// (calc_print_cells.cpp — fills, grid lines, text with Excel's overflow,
// borders, pictures). Internal to QindaCalc's printer.

#include "page_layout.h"
#include "sheet.h"
#include "workbook.h"

#include <QFont>
#include <QPainter>
#include <QRectF>

namespace QindaOffice::Apps::QindaCalc::PrintDetail {

namespace CalcEng = QindaOffice::Calc;

inline constexpr qreal kPxToPt = 0.75; // the engine's sizes are pixels at 96 dpi
inline const QColor kInk(0x20, 0x20, 0x28);
inline const QColor kPrintGrid(0xb8, 0xb8, 0xbc);
inline const QColor kHeadingFill(0xf0, 0xf0, 0xf2);

// Points → the printer's device pixels, from the paper's corner. A world
// transform would scale the fonts a second time (QFont point sizes already
// follow the device's DPI).
struct Canvas {
    QPainter *p = nullptr;
    qreal dpp = 1;  // device pixels per point
    QPointF origin; // the paper's corner in device pixels
    [[nodiscard]] QRectF rect(const QRectF &pt) const
    {
        return {origin.x() + pt.x() * dpp, origin.y() + pt.y() * dpp, pt.width() * dpp, pt.height() * dpp};
    }
    [[nodiscard]] QPointF point(qreal x, qreal y) const { return {origin.x() + x * dpp, origin.y() + y * dpp}; }
};

struct Line {
    qint32 index;
    qreal sizePt;
};

struct PrintContext {
    const CalcEng::Workbook *book = nullptr;
    qint32 sheet = 0;
    const CalcEng::Sheet::PageSetup *setup = nullptr;
    QFont base;
    qreal scale = 1;
};

// A run of printed columns or rows on a page — the Print Titles, or the
// body — with where each starts (points from the paper's corner) and the
// columns its text may come from or run over.
struct Segment {
    QList<Line> lines;
    QList<qreal> starts;
    qint32 minLine = 0;
    qint32 maxLine = 0;
};

// The printed rows (or columns) of [first, last]: hidden ones take no space.
[[nodiscard]] QList<Line> printedLines(const CalcEng::Sheet &sheet, qint32 first, qint32 last, bool rows, qreal scale);

// The cells of one page: `rows` and `cols` are its segments (titles first),
// drawn in passes — fills, grid lines (none under overflowing text), icons
// and text by calc_text_overflow.h's rules, borders, pictures.
void paintPageCells(const Canvas &canvas, const PrintContext &ctx, const QList<Segment> &rows,
                    const QList<Segment> &cols);

} // namespace QindaOffice::Apps::QindaCalc::PrintDetail
