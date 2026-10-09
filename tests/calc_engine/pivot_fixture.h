// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sales list the M74 PivotTable tests summarize (D-083), and helpers to
// read a pivot's output back as text. Every expected grid in the tests is
// worked by hand from these six records:
//
//   Region  Product  Date        Sales  Qty
//   East    Apple    2024-01-05  10     1
//   West    Pear     2024-02-10  20     2
//   East    Pear     2024-04-20  30     3
//   North   Apple    2024-07-01  5      4
//   West    Apple    2024-11-15  7      5
//   East    Apple    2025-03-03  8      6
//
// Sum of Sales: East 48 (Apple 18, Pear 30), North 5 (Apple 5), West 27
// (Apple 7, Pear 20); Apple 30, Pear 50; 80 in all. Sum of Qty: East 10,
// North 4, West 7; 21.

#include "functions/functions.h"
#include "number_format.h"
#include "pivot_compute.h"
#include "pivot_edit.h"
#include "sheet.h"
#include "workbook.h"

#include <QtTest>

#include <cmath>

namespace PivotTest {

using namespace QindaOffice::Calc;

inline void fillSales(Workbook &book)
{
    book.addSheet(QStringLiteral("Data"));
    book.addSheet(QStringLiteral("Pivot"));
    CellStyle date;
    date.numberFormat = QStringLiteral("m/d/yyyy");
    const qint32 dateStyle = book.addStyle(date);
    const QStringList headers{QStringLiteral("Region"), QStringLiteral("Product"), QStringLiteral("Date"),
                              QStringLiteral("Sales"), QStringLiteral("Qty")};
    for (qint32 c = 0; c < headers.size(); ++c) {
        book.setCellValue(0, {0, c}, headers.at(c));
    }
    struct Row {
        const char *region;
        const char *product;
        QDate date;
        double sales;
        double qty;
    };
    const QList<Row> rows{{"East", "Apple", QDate(2024, 1, 5), 10, 1},   {"West", "Pear", QDate(2024, 2, 10), 20, 2},
                          {"East", "Pear", QDate(2024, 4, 20), 30, 3},   {"North", "Apple", QDate(2024, 7, 1), 5, 4},
                          {"West", "Apple", QDate(2024, 11, 15), 7, 5},  {"East", "Apple", QDate(2025, 3, 3), 8, 6}};
    for (qint32 r = 0; r < rows.size(); ++r) {
        const Row &row = rows.at(r);
        book.setCellValue(0, {r + 1, 0}, QString::fromLatin1(row.region));
        book.setCellValue(0, {r + 1, 1}, QString::fromLatin1(row.product));
        book.setCellValue(0, {r + 1, 2}, serialFromDate(row.date));
        book.sheetAt(0).setStyleIdAt({r + 1, 2}, dateStyle);
        book.setCellValue(0, {r + 1, 3}, row.sales);
        book.setCellValue(0, {r + 1, 4}, row.qty);
    }
}

inline PivotSource salesSource()
{
    return PivotSource{QStringLiteral("Data"), CellRange{{0, 0}, {6, 4}}, QString()};
}

// An empty pivot over the sales list at Pivot!A3 (fields: 0 Region,
// 1 Product, 2 Date, 3 Sales, 4 Qty).
inline PivotRef makePivot(Workbook &book)
{
    QString error;
    const PivotRef ref = createPivotTable(book, salesSource(), 1, {2, 0}, &error);
    if (!ref.isValid()) {
        qWarning("createPivotTable: %s", qPrintable(error));
    }
    return ref;
}

inline const PivotTable &pivot(const Workbook &book, const PivotRef &ref)
{
    return book.sheetAt(ref.sheet).pivots().at(ref.index);
}

inline const PivotCache &cacheOf(const Workbook &book, const PivotRef &ref)
{
    return *pivotCacheOf(book, pivot(book, ref).cacheId);
}

// Changes the pivot's definition and lays it out again.
template <typename Change>
inline bool change(Workbook &book, const PivotRef &ref, Change change)
{
    PivotTable p = pivot(book, ref);
    const PivotCache &cache = cacheOf(book, ref);
    change(p, cache);
    pivotNormalize(p, cache);
    book.sheetAt(ref.sheet).mutablePivots()[ref.index] = p;
    QString error;
    const bool ok = renderPivotTable(book, ref, &error);
    if (!ok) {
        qWarning("renderPivotTable: %s", qPrintable(error));
    }
    return ok;
}

inline QString text(const Value &value)
{
    if (const auto *n = std::get_if<double>(&value)) {
        if (std::floor(*n) == *n && qAbs(*n) < 1e15) {
            return QString::number(qint64(*n));
        }
        QString out = QString::number(*n, 'f', 4);
        while (out.endsWith(QLatin1Char('0'))) {
            out.chop(1);
        }
        return out;
    }
    return isBlank(value) ? QString() : toText(value);
}

// The pivot's body (or `range`) as rows of cell texts joined by '|'.
inline QStringList grid(const Workbook &book, qint32 sheet, const CellRange &range)
{
    QStringList rows;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        QStringList cells;
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            cells.append(text(book.cellValue(sheet, r, c)));
        }
        rows.append(cells.join(QLatin1Char('|')));
    }
    return rows;
}

inline QStringList body(const Workbook &book, const PivotRef &ref)
{
    return grid(book, ref.sheet, pivot(book, ref).render->body);
}

// rowItems / colItems the way the file spells them: "i", "i r=1 x=0",
// "default x=2", "grand i=1 x=1".
inline QStringList lines(const QList<PivotLine> &list)
{
    QStringList out;
    for (const PivotLine &line : list) {
        QString s;
        switch (line.type) {
        case PivotLine::Type::Data: s = QStringLiteral("i"); break;
        case PivotLine::Type::Subtotal: s = line.custom ? QStringLiteral("custom") : QStringLiteral("default"); break;
        case PivotLine::Type::Grand: s = QStringLiteral("grand"); break;
        case PivotLine::Type::Blank: s = QStringLiteral("blank"); break;
        }
        if (line.repeat != 0) {
            s += QStringLiteral(" r=%1").arg(line.repeat);
        }
        if (line.dataIndex != 0) {
            s += QStringLiteral(" i=%1").arg(line.dataIndex);
        }
        for (const qint32 x : line.items) {
            s += QStringLiteral(" x=%1").arg(x);
        }
        out.append(s);
    }
    return out;
}

} // namespace PivotTest
