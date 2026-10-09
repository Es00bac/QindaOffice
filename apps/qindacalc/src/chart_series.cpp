// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_series.h"

#include "parser.h"
#include "value.h"

#include <QVariantList>

namespace QindaOffice::Apps::QindaCalc {

using namespace QindaOffice::Calc;

namespace {

// "'Dashboard'!$Q$41:$Q$52" → (sheet index, range) through the formula
// parser, which already speaks the xlsx reference spelling.
std::optional<QPair<qint32, CellRange>> resolveRef(const Workbook &wb, const QString &text)
{
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const ParseResult parsed = parseFormula(text);
    if (!parsed.root || parsed.root->kind != Node::Kind::Ref) {
        return std::nullopt;
    }
    const RangeRef &ref = parsed.root->ref;
    const qint32 sheet = ref.sheet.isEmpty() ? 0 : wb.sheetIndex(unquotedSheetName(ref.sheet));
    if (sheet < 0 || !ref.range.isValid()) {
        return std::nullopt;
    }
    return QPair<qint32, CellRange>{sheet, ref.range};
}

QVariantList valuesOf(const Workbook &wb, const QString &refText)
{
    QVariantList out;
    const std::optional<QPair<qint32, CellRange>> resolved = resolveRef(wb, refText);
    if (!resolved.has_value()) {
        return out;
    }
    const auto [sheet, range] = *resolved;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            out.append(toNumber(wb.cellValue(sheet, r, c)).value_or(0.0));
        }
    }
    return out;
}

QVariantList categoriesOf(const Workbook &wb, const QString &refText)
{
    QVariantList out;
    const std::optional<QPair<qint32, CellRange>> resolved = resolveRef(wb, refText);
    if (!resolved.has_value()) {
        return out;
    }
    const auto [sheet, range] = *resolved;
    int index = 0;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            QString category = toText(wb.cellValue(sheet, r, c));
            if (category.isEmpty()) {
                category = QString::number(index + 1);
            }
            out.append(category);
            ++index;
        }
    }
    return out;
}

// The series-authored path (imported charts): explicit per-series cat/val
// ranges resolved against live cell values.
QVariantMap seriesDataFromSeries(const ChartSpec &spec, const Workbook &wb)
{
    QVariantList series;
    for (const ChartSeriesSpec &ser : spec.series) {
        QString name = ser.name;
        // A name read from a cell ('Q1 Data'!$B$1, the file had no cached
        // text) shows that cell's text, live (M69).
        if (name.contains(QLatin1Char('!'))) {
            if (const auto cell = resolveRef(wb, name); cell && cell->second.cellCount() == 1) {
                name = toText(wb.cellValue(cell->first, cell->second.topLeft.row, cell->second.topLeft.col));
            }
        }
        if (name.isEmpty()) {
            name = QStringLiteral("Series %1").arg(series.size() + 1);
        }
        series.append(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("values"), valuesOf(wb, ser.valRange)}});
    }
    // ChartView renders one category axis: the first series' categories.
    QVariantList categories = spec.series.isEmpty()
        ? QVariantList()
        : categoriesOf(wb, spec.series.first().catRange);
    return {{QStringLiteral("title"), spec.title},
            {QStringLiteral("kind"), int(spec.kind)},
            {QStringLiteral("categories"), categories},
            {QStringLiteral("series"), series}};
}

} // namespace

QVariantMap chartSeriesData(const ChartSpec &spec, const Workbook &wb)
{
    // A non-empty series list wins (chart_spec.h); rangeText stays the UI
    // authoring path.
    if (!spec.series.isEmpty()) {
        return seriesDataFromSeries(spec, wb);
    }
    QVariantMap out{{QStringLiteral("title"), spec.title},
                    {QStringLiteral("kind"), int(spec.kind)},
                    {QStringLiteral("categories"), QVariantList()},
                    {QStringLiteral("series"), QVariantList()}};
    const CellRange range = parseChartRange(spec.rangeText);
    if (!range.isValid() || spec.sheet < 0 || spec.sheet >= wb.sheetCount()) {
        return out;
    }
    const int bodyTop = range.topLeft.row + (spec.headerH ? 1 : 0);
    const int bodyLeft = range.topLeft.col + (spec.headerV ? 1 : 0);
    const int cols = range.bottomRight.col - bodyLeft + 1;
    const int rows = range.bottomRight.row - bodyTop + 1;
    QVariantList series;
    for (int c = 0; c < cols; ++c) {
        QString name = spec.headerH ? toText(wb.cellValue(spec.sheet, range.topLeft.row, bodyLeft + c))
                                    : QString();
        if (name.isEmpty()) {
            name = QStringLiteral("Series %1").arg(c + 1);
        }
        QVariantList values;
        for (int r = 0; r < rows; ++r) {
            const Value v = wb.cellValue(spec.sheet, bodyTop + r, bodyLeft + c);
            values.append(toNumber(v).value_or(0.0));
        }
        series.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("values"), values}});
    }
    QVariantList categories;
    for (int r = 0; r < rows; ++r) {
        QString category = spec.headerV ? toText(wb.cellValue(spec.sheet, bodyTop + r, range.topLeft.col))
                                        : QString();
        if (category.isEmpty()) {
            category = QString::number(r + 1);
        }
        categories.append(category);
    }
    out.insert(QStringLiteral("series"), series);
    out.insert(QStringLiteral("categories"), categories);
    return out;
}

} // namespace QindaOffice::Apps::QindaCalc
