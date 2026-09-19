// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ChartSpec (plan section 7.2): a chart anchored on a sheet as a floating
// object. The spec is plain data in the workbook so xlsx export flows
// through the model; the app renders live previews with Qt Charts when
// QQO_WITH_CHARTS is enabled. QXlsx has no public chart-read API, so
// loading a file with charts drops them with a warning (documented
// limitation, plan section 7.2).

#include "value.h"

#include <QList>
#include <QString>

namespace QindaOffice::Calc {

// One chart series as an imported file spells it: explicit category and
// value ranges (sheet-qualified, xlsx reference syntax) plus the series
// title. Imported charts are usually NOT one contiguous block (a shared
// category column with scattered value columns), which the single-range
// form cannot say.
struct ChartSeriesSpec {
    QString name;
    QString catRange;
    QString valRange;
    bool operator==(const ChartSeriesSpec &) const = default;
};

struct ChartSpec {
    enum class Kind { Bar, StackedBar, Line, Area, Pie, Scatter };
    Kind kind = Kind::Bar;
    QString title;
    QString rangeText; // "A1:B5" form, on `sheet`
    bool headerH = false; // first row carries series/categories names
    bool headerV = false;
    // A non-empty series list wins for rendering and export; rangeText +
    // headerH/V remains the UI authoring path (ChartDialog).
    QList<ChartSeriesSpec> series;
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
