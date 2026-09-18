// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_series.h"

#include "value.h"

#include <QVariantList>

namespace QindaOffice::Apps::QindaCalc {

using namespace QindaOffice::Calc;

QVariantMap chartSeriesData(const ChartSpec &spec, const Workbook &wb)
{
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
