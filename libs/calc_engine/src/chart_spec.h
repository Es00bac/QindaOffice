// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ChartSpec (plan section 7.2): a chart anchored on a sheet as a floating
// object. The spec is plain data in the workbook so xlsx export flows
// through the model; the app renders live previews with Qt Charts when
// QQO_WITH_CHARTS is enabled. QXlsx has no public chart-read API, so
// loading a file with charts drops them with a warning (documented
// limitation, plan section 7.2).

#include "value.h"

#include <QString>

namespace QindaOffice::Calc {

struct ChartSpec {
    enum class Kind { Bar, StackedBar, Line, Area, Pie, Scatter };
    Kind kind = Kind::Bar;
    QString title;
    QString rangeText; // "A1:B5" form, on `sheet`
    bool headerH = false; // first row carries series/categories names
    bool headerV = false;
    qint32 sheet = 0;
    int anchorRow = 0; // floating-object anchor cell
    int anchorCol = 0;
    int widthPx = 480;
    int heightPx = 320;

    bool operator==(const ChartSpec &) const = default;
};

// Parses "A1:B5" (case-insensitive, optional $ anchors) into a CellRange;
// returns an invalid range on malformed input. Whole rows/columns are not
// supported for charts (v1).
[[nodiscard]] CellRange parseChartRange(const QString &text);

} // namespace QindaOffice::Calc
