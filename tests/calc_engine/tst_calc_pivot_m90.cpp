// SPDX-License-Identifier: GPL-3.0-or-later
#include "pivot_fixture.h"
#include "pivot_calculated.h"
#include "pivot_dates.h"
using namespace PivotTest;
class CalcPivotM90Test : public QObject {
    Q_OBJECT
private slots:
    void calculatedTotalsAndRefresh()
    {
        Workbook book;
        fillSales(book);
        const auto ref = makePivot(book);
        auto &cache = book.pivotCaches[0];
        PivotCacheField f;
        f.name = QStringLiteral("Unit Price");
        f.database = false;
        f.formula = QStringLiteral("=Sales/Qty");
        cache.fields.append(f);
        QVERIFY(pivotCalculatedProblem(cache, 5).isEmpty());
        PivotTable probe = pivot(book, ref);
        QVERIFY(!pivotPlaceField(probe, cache, 5, PivotArea::Rows));
        pivotToggleField(probe, cache, 5, true);
        QCOMPARE(probe.dataFields.first().function, PivotFunction::Sum);
        QVERIFY(change(book, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 5, PivotArea::Values);
        }));
        const auto computed = computePivot(cache, pivot(book, ref));
        const auto *grand = computed.valuesAt(0, 0);
        QVERIFY(grand != nullptr);
        QCOMPARE(grand->first(), Value(80.0 / 21.0));
        QVERIFY(refreshPivotCache(book, cache.id));
        QCOMPARE(book.pivotCaches.first().fields.last().formula, QStringLiteral("=Sales/Qty"));
        auto edited = book.pivotCaches.first();
        edited.fields[5].formula = QStringLiteral("='Unit Price'+1");
        QVERIFY(!pivotCalculatedProblem(edited, 5).isEmpty());
        edited.fields[5].formula = QStringLiteral("=A1+1");
        QVERIFY(!pivotCalculatedProblem(edited, 5).isEmpty());
        edited.fields[5].formula = QStringLiteral("=SUM(Sales)*2");
        QVERIFY(pivotCalculatedProblem(edited, 5).isEmpty());
    }
    void dateBoundaries()
    {
        const QDate today(2024, 2, 29);
        PivotFilter f;
        f.kind = PivotFilter::Kind::Date;
        for (const auto &type : pivotDateTypes()) {
            f.dateType = type;
            QVERIFY(!pivotDateAccepts(f, QStringLiteral("not a date"), today));
        }
        const auto accepts = [&](const QString &type, const QDate &date) {
            f.dateType = type;
            return pivotDateAccepts(f, serialFromDate(date) + 0.75, today);
        };
        QVERIFY(accepts(QStringLiteral("thisMonth"), today));
        QVERIFY(!accepts(QStringLiteral("thisMonth"), QDate(2024, 3, 1)));
        QVERIFY(accepts(QStringLiteral("nextQuarter"), QDate(2024, 4, 1)));
        QVERIFY(accepts(QStringLiteral("lastQuarter"), QDate(2023, 12, 31)));
        QVERIFY(accepts(QStringLiteral("yearToDate"), today));
        QVERIFY(!accepts(QStringLiteral("yearToDate"), QDate(2024, 3, 1)));
        f.text1 = QStringLiteral("2024-02-01");
        f.text2 = QStringLiteral("2024-02-29");
        QVERIFY(accepts(QStringLiteral("dateBetween"), today));
        QVERIFY(accepts(QStringLiteral("dateOlderThan"), QDate(2024, 1, 31)));
        QVERIFY(!accepts(QStringLiteral("dateNewerThan"), QDate(2024, 2, 1)));
    }
};
QTEST_GUILESS_MAIN(CalcPivotM90Test)
#include "tst_calc_pivot_m90.moc"
