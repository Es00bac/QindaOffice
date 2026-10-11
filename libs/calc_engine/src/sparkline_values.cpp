// SPDX-License-Identifier: GPL-3.0-or-later
// What a sparkline draws (sparkline.h, M90, D-098): its values in order —
// hidden rows and columns left out unless the group shows them (Excel's
// "Show data in hidden rows and columns"), empty cells as gaps, zeros or
// left out to connect the line ("Show empty cells as") — and the vertical
// axis: per sparkline, the group's extremes, or the custom values.

#include "sparkline.h"

#include "parser.h"
#include "sheet.h"
#include "workbook.h"

#include <QSet>

#include <cmath>

namespace QindaOffice::Calc {

namespace {

struct Located {
    qint32 sheet = -1;
    CellRange range;
};

std::optional<Located> locate(const Workbook &book, const QString &text)
{
    const ParseResult parsed = parseFormula(text.trimmed());
    if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref || !parsed.root->ref.range.isValid()) {
        return std::nullopt;
    }
    const RangeRef &ref = parsed.root->ref;
    const qint32 sheet = book.sheetIndex(unquotedSheetName(ref.sheet));
    if (sheet < 0) {
        return std::nullopt;
    }
    return Located{sheet, ref.range};
}

// The cells of a range in a sparkline's order (along the row, or down the
// column; a block row by row).
QList<CellAddr> cellsOf(const CellRange &range)
{
    QList<CellAddr> cells;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            cells.append({r, c});
        }
    }
    return cells;
}

} // namespace

SparklineSeries sparklineSeries(const Workbook &book, const SparklineGroup &group, const Sparkline &line)
{
    SparklineSeries out;
    const std::optional<Located> data = locate(book, line.data);
    if (!data.has_value()) {
        return out;
    }
    const Sheet &sheet = book.sheetAt(data->sheet);
    const QSet<qint32> filtered = group.displayHidden ? QSet<qint32>() : sheet.filteredOutRows();
    const QList<CellAddr> cells = cellsOf(data->range);
    QList<CellAddr> dateCells;
    qint32 dateSheet = -1;
    if (group.dateAxis) {
        if (const std::optional<Located> dates = locate(book, group.dateRange)) {
            dateCells = cellsOf(dates->range);
            dateSheet = dates->sheet;
        }
    }
    const bool withDates = dateSheet >= 0 && dateCells.size() == cells.size();
    for (qsizetype i = 0; i < cells.size(); ++i) {
        const CellAddr &cell = cells.at(i);
        if (!group.displayHidden
            && (sheet.isRowHidden(cell.row) || sheet.isColumnHidden(cell.col) || filtered.contains(cell.row))) {
            continue;
        }
        const Value value = book.cellValue(data->sheet, cell.row, cell.col);
        std::optional<double> number;
        if (const auto *n = std::get_if<double>(&value); n != nullptr && std::isfinite(*n)) {
            number = *n;
        } else if (group.empty == SparklineGroup::Empty::Span) {
            continue; // left out: the line joins its neighbours
        } else if (group.empty == SparklineGroup::Empty::Zero) {
            number = 0.0;
        }
        out.values.append(number);
        if (withDates) {
            const Value date = book.cellValue(dateSheet, dateCells.at(i).row, dateCells.at(i).col);
            out.dates.append(toNumber(date).value_or(double(i)));
        }
    }
    return out;
}

SparklineScale sparklineScale(const Workbook &book, const SparklineGroup &group, const SparklineSeries &series)
{
    const auto extremes = [](const QVector<std::optional<double>> &values, SparklineScale *scale, bool *any) {
        for (const std::optional<double> &v : values) {
            if (!v.has_value()) {
                continue;
            }
            scale->min = *any ? qMin(scale->min, *v) : *v;
            scale->max = *any ? qMax(scale->max, *v) : *v;
            *any = true;
        }
    };
    SparklineScale own;
    bool ownAny = false;
    extremes(series.values, &own, &ownAny);
    SparklineScale shared = own;
    if (group.minAxis == SparklineGroup::AxisScale::Group || group.maxAxis == SparklineGroup::AxisScale::Group) {
        bool any = false;
        for (const Sparkline &line : group.sparklines) {
            extremes(sparklineSeries(book, group, line).values, &shared, &any);
        }
    }
    SparklineScale out = own;
    out.min = group.minAxis == SparklineGroup::AxisScale::Custom ? group.manualMin
        : group.minAxis == SparklineGroup::AxisScale::Group      ? shared.min
                                                                  : own.min;
    out.max = group.maxAxis == SparklineGroup::AxisScale::Custom ? group.manualMax
        : group.maxAxis == SparklineGroup::AxisScale::Group      ? shared.max
                                                                  : own.max;
    return out;
}

} // namespace QindaOffice::Calc
