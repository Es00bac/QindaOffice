// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet.h"
#include "workbook.h"
#include <QtTest>
using namespace QindaOffice::Calc;

class AggregateVisibilityTest : public QObject {
    Q_OBJECT
    static Value result(Workbook &book, const QString &formula)
    {
        if (!book.setCellInput(0, {0, 4}, QLatin1Char('=') + formula)) return Err::Name;
        return book.cellValue(0, 0, 4);
    }
private slots:
    void subtotalVisibilityAndNested()
    {
        Workbook book;
        book.addSheet(QStringLiteral("Sheet1"));
        for (qint32 r = 0; r < 4; ++r) book.setCellValue(0, {r, 0}, double(r + 1));
        QVERIFY(book.setCellInput(0, {4, 0}, QStringLiteral("=SUBTOTAL(9,A1:A4)")));
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(9,A1:A5)")), Value(10.0));
        SheetOutline outline;
        setLinesHidden(outline, OutlineAxis::Rows, 1, 1, true);
        book.sheetAt(0).setOutline(outline);
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(109,A1:A5)")), Value(8.0));
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(9,A1:A5)")), Value(10.0));
        AutoFilter filter;
        filter.active = true;
        filter.range = {{0, 0}, {3, 0}};
        filter.allowed.insert(0, {QStringLiteral("2"), QStringLiteral("4")});
        book.sheetAt(0).setAutoFilter(filter);
        book.recalcAll();
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(9,A1:A5)")), Value(7.0));
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(109,A1:A5)")), Value(5.0));
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(109,A2)")), Value(0.0));
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(209,A1:A5)")), Value(Err::Value));
        // Literal text is not a nested call, and sparse full columns stay cheap.
        QVERIFY(book.setCellInput(0, {5, 0}, QStringLiteral("=\"SUBTOTAL(9)\"")));
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(103,A:A)")), Value(3.0));
    }
    void filterInputsRecalculateTotals()
    {
        Workbook book;
        book.addSheet(QStringLiteral("Sheet1"));
        book.setCellValue(0, {1, 0}, 10.0);
        book.setCellValue(0, {1, 1}, QStringLiteral("yes"));
        AutoFilter filter;
        filter.active = true;
        filter.range = {{0, 0}, {1, 1}};
        filter.allowed.insert(1, {QStringLiteral("yes")});
        book.sheetAt(0).setAutoFilter(filter);
        QCOMPARE(result(book, QStringLiteral("SUBTOTAL(109,A2)")), Value(10.0));
        book.setCellValue(0, {1, 1}, QStringLiteral("no"));
        QCOMPARE(book.cellValue(0, 0, 4), Value(0.0));
        book.setCellValue(0, {1, 1}, QStringLiteral("yes"));
        QCOMPARE(book.cellValue(0, 0, 4), Value(10.0));
    }
    void aggregateOptions()
    {
        Workbook book;
        book.addSheet(QStringLiteral("Sheet1"));
        book.setCellValue(0, {0, 0}, 1.0);
        book.setCellValue(0, {1, 0}, 2.0);
        book.setCellValue(0, {2, 0}, Value(Err::Div0));
        QVERIFY(book.setCellInput(0, {3, 0}, QStringLiteral("=SUBTOTAL(9,A1:A2)")));
        SheetOutline outline;
        setLinesHidden(outline, OutlineAxis::Rows, 1, 1, true);
        book.sheetAt(0).setOutline(outline);
        const QList<Value> expected{Err::Div0, Err::Div0, 3.0, 1.0, Err::Div0, Err::Div0, 6.0, 4.0};
        for (int option = 0; option <= 7; ++option) {
            QCOMPARE(result(book, QStringLiteral("AGGREGATE(9,%1,A1:A4)").arg(option)), expected.at(option));
        }
        QCOMPARE(result(book, QStringLiteral("AGGREGATE(9,1,A2)")), Value(0.0));
        QCOMPARE(result(book, QStringLiteral("AGGREGATE(14,7,A1:A4,1)")), Value(3.0));
        // Materialized arrays have no visibility metadata, as in Excel.
        QCOMPARE(result(book, QStringLiteral("AGGREGATE(9,3,{1,2,3})")), Value(6.0));
    }
};
QTEST_GUILESS_MAIN(AggregateVisibilityTest)
#include "tst_calc_aggregate_visibility.moc"
