// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sparklines drawn in their cells (M90, D-098): line, column and win/loss
// as Excel draws them — the points shown (high, low, first, last,
// negative, markers) in their colours, the horizontal axis, a date axis,
// gaps, right to left, the vertical axis per sparkline, shared by the group
// or custom (calc_engine's sparkline.h works the values out). The grid
// (spreadsheet_item_cells.cpp) and the printer (calc_print_cells.cpp) both
// paint through one SparklinePainter, so paper and screen agree. Document
// content, not chrome: Excel's colours from the workbook's theme (D-004).

#include "sparkline.h"

#include <QHash>
#include <QRectF>
#include <QVector>

class QPainter;

namespace QindaOffice::Apps::QindaCalc {

namespace CalcEng = QindaOffice::Calc;

class SparklinePainter {
public:
    // Indexes the sheet's sparklines by cell; the theme's palette from the
    // workbook's theme part.
    SparklinePainter(const CalcEng::Workbook &book, qint32 sheet);
    [[nodiscard]] bool isEmpty() const { return m_index.isEmpty(); }
    // Paints the sparkline drawn in `addr`, if any, into `cell`;
    // `unitsPerPoint` scales its line weight (screen px at the zoom, or
    // printer units).
    void paint(QPainter &p, const CalcEng::CellAddr &addr, const QRectF &cell, qreal unitsPerPoint) const;

private:
    const CalcEng::Workbook &m_book;
    qint32 m_sheet;
    QHash<quint64, CalcEng::SparklineRef> m_index;
    QVector<CalcEng::Rgb> m_palette;
};

} // namespace QindaOffice::Apps::QindaCalc
