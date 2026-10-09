// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a conditional format draws in a cell beyond colours (M78, D-087):
// Excel's data bars (2007 gradients, 2010 solid bars, negative values left
// of an axis, borders, right-to-left) and its icon sets, drawn as vector
// shapes in Excel's colours. The grid and the printer both paint through
// here, so paper and screen show the same bar and the same icon. Document
// content, not chrome: fixed colours like the paper itself (D-004).

#include "sheet_rules.h"

#include <QRectF>

class QPainter;

namespace QindaOffice::Apps::QindaCalc {

namespace CalcEng = QindaOffice::Calc;

// Paints the bar inside `cell` (a small inset, as Excel leaves).
void paintDataBar(QPainter &p, const QRectF &cell, const CalcEng::DataBarFill &bar);

// The square an icon takes at the cell's left edge (Excel's place), and
// the icon itself.
[[nodiscard]] QRectF iconBox(const QRectF &cell);
void paintCfIcon(QPainter &p, const QRectF &box, const CalcEng::IconPaint &icon);

// How many icons a set has (3, 4 or 5; 0 for a name Excel does not have).
[[nodiscard]] int iconSetSize(const QString &set);

} // namespace QindaOffice::Apps::QindaCalc
