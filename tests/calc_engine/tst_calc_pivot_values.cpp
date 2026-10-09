// SPDX-License-Identifier: GPL-3.0-or-later
// M74 (D-083) PivotTable values against hand-worked grids
// (pivot_fixture.h): every Show Values As calculation, grouping by date
// parts, number ranges and chosen items, sorting by label, by value and by
// hand, and label, value, Top 10 and manual filters.

#include "pivot_fixture.h"

#include <limits>

using namespace PivotTest;

class CalcPivotValuesTest : public QObject {
    Q_OBJECT

    static Workbook *book()
    {
        static Workbook *instance = nullptr;
        delete instance;
        instance = new Workbook;
        fillSales(*instance);
        return instance;
    }

    // Rows Region (and Product), Sum of Sales shown as `as`.
    static PivotRef shownAs(Workbook &wb, PivotShowAs as, bool products, bool columns, qint32 baseItem = -1)
    {
        const PivotRef ref = makePivot(wb);
        change(wb, ref, [&](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            if (products || columns) {
                pivotPlaceField(p, c, 1, products ? PivotArea::Rows : PivotArea::Columns);
            }
            pivotPlaceField(p, c, 3, PivotArea::Values);
            p.dataFields[0].showAs = as;
            p.dataFields[0].baseField = 0;
            p.dataFields[0].baseItem = baseItem;
        });
        return ref;
    }

    static QStringList valuesColumn(const Workbook &wb, const PivotRef &ref)
    {
        QStringList out;
        for (const QString &row : body(wb, ref).mid(1)) {
            out.append(row.section(QLatin1Char('|'), 1));
        }
        return out;
    }

private slots:
    void showValuesAsOverRegions_data()
    {
        QTest::addColumn<int>("as");
        QTest::addColumn<int>("baseItem");
        QTest::addColumn<QStringList>("values"); // East, North, West, Grand Total
        QTest::newRow("% of grand total") << int(PivotShowAs::PercentOfTotal) << -1
                                          << QStringList{"0.6", "0.0625", "0.3375", "1"};
        QTest::newRow("running total") << int(PivotShowAs::RunningTotal) << -1 << QStringList{"48", "53", "80", ""};
        QTest::newRow("% running total") << int(PivotShowAs::PercentRunningTotal) << -1
                                         << QStringList{"0.6", "0.6625", "1", ""};
        QTest::newRow("rank largest") << int(PivotShowAs::RankDescending) << -1 << QStringList{"1", "3", "2", ""};
        QTest::newRow("rank smallest") << int(PivotShowAs::RankAscending) << -1 << QStringList{"3", "1", "2", ""};
        QTest::newRow("difference from previous") << int(PivotShowAs::Difference) << kPivotPreviousItem
                                                  << QStringList{"", "-43", "22", ""};
        QTest::newRow("difference from next") << int(PivotShowAs::Difference) << kPivotNextItem
                                              << QStringList{"43", "-22", "", ""};
        QTest::newRow("% difference from East") << int(PivotShowAs::PercentDifference) << 0
                                                << QStringList{"", "-0.8958", "-0.4375", ""};
        QTest::newRow("% of East") << int(PivotShowAs::PercentOf) << 0 << QStringList{"1", "0.1042", "0.5625", ""};
    }

    void showValuesAsOverRegions()
    {
        QFETCH(int, as);
        QFETCH(int, baseItem);
        QFETCH(QStringList, values);
        Workbook &wb = *book();
        const PivotRef ref = shownAs(wb, PivotShowAs(as), false, false, baseItem);
        QCOMPARE(valuesColumn(wb, ref), values);
    }

    void showValuesAsOverACrossTable()
    {
        Workbook &wb = *book();
        PivotRef ref = shownAs(wb, PivotShowAs::PercentOfColumn, false, true);
        QCOMPARE(body(wb, ref).mid(2), (QStringList{"East|0.6|0.6|0.6", "North|0.1667||0.0625", "West|0.2333|0.4|0.3375",
                                                    "Grand Total|1|1|1"}));
        Workbook &other = *book();
        ref = shownAs(other, PivotShowAs::PercentOfRow, false, true);
        QCOMPARE(body(other, ref).mid(2), (QStringList{"East|0.375|0.625|1", "North|1||1", "West|0.2593|0.7407|1",
                                                         "Grand Total|0.375|0.625|1"}));
    }

    void indexAndPercentOf()
    {
        Workbook &wb = *book();
        PivotRef ref = shownAs(wb, PivotShowAs::Index, false, true);
        QCOMPARE(body(wb, ref).mid(2), (QStringList{"East|1|1|1", "North|2.6667||1", "West|0.6914|1.1852|1",
                                                    "Grand Total|1|1|1"}));
        Workbook &other = *book();
        ref = shownAs(other, PivotShowAs::PercentOf, false, true, 0);
        QCOMPARE(body(other, ref).mid(2), (QStringList{"East|1|1|1", "North|0.2778||0.1042", "West|0.3889|0.6667|0.5625",
                                                       "Grand Total|||"}));
    }

    void percentOfParents()
    {
        Workbook &wb = *book();
        PivotRef ref = shownAs(wb, PivotShowAs::PercentOfParentRow, true, false);
        QCOMPARE(valuesColumn(wb, ref), (QStringList{"0.6", "0.375", "0.625", "0.0625", "1", "0.3375", "0.2593", "0.7407",
                                                     "1"}));
        Workbook &other = *book();
        ref = shownAs(other, PivotShowAs::PercentOfParent, true, false);
        QCOMPARE(valuesColumn(other, ref), (QStringList{"1", "0.375", "0.625", "1", "1", "1", "0.2593", "0.7407", ""}));
    }

    void groupDatesByYearsQuartersMonths()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 2, PivotArea::Rows);
            pivotPlaceField(p, c, 3, PivotArea::Values);
        }));
        const double automatic = std::numeric_limits<double>::quiet_NaN();
        QString error;
        QVERIFY2(pivotGroup(wb, ref, 2, {PivotGroupBy::Months, PivotGroupBy::Quarters, PivotGroupBy::Years}, automatic,
                            automatic, 1, &error),
                 qPrintable(error));
        const PivotCache &cache = cacheOf(wb, ref);
        QCOMPARE(cache.fields.size(), 7);
        QCOMPARE(cache.fields.at(5).name, QStringLiteral("Quarters"));
        QCOMPARE(cache.fields.at(6).name, QStringLiteral("Years"));
        QCOMPARE(cache.fields.at(6).group.names, (QStringList{"<1/5/2024", "2024", "2025", ">3/4/2025"}));
        QCOMPARE(cache.fields.at(2).group.names.size(), 14); // <start, Jan … Dec, >end
        QCOMPARE(pivot(wb, ref).rowFields, (QList<qint32>{6, 5, 2}));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "2024|72", "Qtr1|30", "Jan|10", "Feb|20", "Qtr2|30",
                                             "Apr|30", "Qtr3|5", "Jul|5", "Qtr4|7", "Nov|7", "2025|8", "Qtr1|8", "Mar|8",
                                             "Grand Total|80"}));
        // Months alone fold the years together.
        QVERIFY(pivotUngroup(wb, ref, 2, &error));
        QCOMPARE(cacheOf(wb, ref).fields.size(), 5);
        QVERIFY(pivotGroup(wb, ref, 2, {PivotGroupBy::Months}, automatic, automatic, 1, &error));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "Jan|10", "Feb|20", "Mar|8", "Apr|30", "Jul|5",
                                             "Nov|7", "Grand Total|80"}));
    }

    void groupNumbersAndItems()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 3, PivotArea::Rows);
            pivotPlaceField(p, c, 0, PivotArea::Values);
        }));
        QString error;
        QVERIFY2(pivotGroup(wb, ref, 3, {PivotGroupBy::Range}, 0, 30, 10, &error), qPrintable(error));
        QCOMPARE(cacheOf(wb, ref).fields.at(3).group.names, (QStringList{"<0", "0-9", "10-19", "20-29", "30-39", ">40"}));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Count of Region", "0-9|3", "10-19|1", "20-29|1", "30-39|1",
                                             "Grand Total|6"}));
        // Text cannot group by ranges.
        QVERIFY(!pivotGroup(wb, ref, 0, {PivotGroupBy::Range}, 0, 10, 1, &error));
        // Group Selection: East and West become Group1 over Region.
        Workbook &other = *book();
        const PivotRef regions = makePivot(other);
        QVERIFY(change(other, regions, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 3, PivotArea::Values);
        }));
        QVERIFY2(pivotGroupSelection(other, regions, 0, {0, 1}, &error), qPrintable(error)); // East(0), West(1)
        QCOMPARE(cacheOf(other, regions).fields.at(5).name, QStringLiteral("Region2"));
        QCOMPARE(pivot(other, regions).rowFields, (QList<qint32>{5, 0}));
        QCOMPARE(body(other, regions), (QStringList{"Row Labels|Sum of Sales", "Group1|75", "East|48", "West|27", "North|5",
                                                    "North|5", "Grand Total|80"}));
        QVERIFY(pivotUngroup(other, regions, 5, &error));
        QCOMPARE(pivot(other, regions).rowFields, (QList<qint32>{0}));
        QCOMPARE(body(other, regions).size(), 5);
    }

    void sortByLabelValueAndHand()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 3, PivotArea::Values);
            p.fields[0].sort = PivotSort::Descending;
        }));
        QCOMPARE(body(wb, ref).mid(1, 3), (QStringList{"West|27", "North|5", "East|48"}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) { p.fields[0].sortData = 0; }));
        QCOMPARE(body(wb, ref).mid(1, 3), (QStringList{"East|48", "West|27", "North|5"}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) { p.fields[0].sort = PivotSort::Ascending; }));
        QCOMPARE(body(wb, ref).mid(1, 3), (QStringList{"North|5", "West|27", "East|48"}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) {
            p.fields[0].sort = PivotSort::Manual;
            p.fields[0].sortData = -1;
            p.fields[0].order = {1, 0, 2}; // West, East, North
        }));
        QCOMPARE(body(wb, ref).mid(1, 3), (QStringList{"West|27", "East|48", "North|5"}));
        QCOMPARE(lines(pivot(wb, ref).render->rowLines).mid(0, 3), (QStringList{"i x=0", "i x=1", "i x=2"}));
    }

    void labelValueTopAndManualFilters()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        const auto filtered = [&](const PivotFilter &filter) {
            change(wb, ref, [&filter](PivotTable &p, const PivotCache &) { p.fields[0].filters = {filter}; });
            return body(wb, ref).mid(1);
        };
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 3, PivotArea::Values);
        }));
        PivotFilter label;
        label.op = PivotFilter::Op::BeginsWith;
        label.text1 = QStringLiteral("w");
        QCOMPARE(filtered(label), (QStringList{"West|27", "Grand Total|27"}));
        label.op = PivotFilter::Op::NotContains;
        label.text1 = QStringLiteral("s");
        QCOMPARE(filtered(label), (QStringList{"North|5", "Grand Total|5"}));
        label.op = PivotFilter::Op::Equal;
        label.text1 = QStringLiteral("?orth");
        QCOMPARE(filtered(label), (QStringList{"North|5", "Grand Total|5"}));
        PivotFilter value;
        value.kind = PivotFilter::Kind::Value;
        value.op = PivotFilter::Op::Greater;
        value.value1 = 10;
        QCOMPARE(filtered(value), (QStringList{"East|48", "West|27", "Grand Total|75"}));
        PivotFilter top;
        top.kind = PivotFilter::Kind::Top;
        top.value1 = 2;
        QCOMPARE(filtered(top), (QStringList{"East|48", "West|27", "Grand Total|75"}));
        top.top = false;
        top.value1 = 1;
        QCOMPARE(filtered(top), (QStringList{"North|5", "Grand Total|5"}));
        top.top = true;
        top.topBy = PivotFilter::TopBy::Percent;
        top.value1 = 60;
        QCOMPARE(filtered(top), (QStringList{"East|48", "Grand Total|48"}));
        top.topBy = PivotFilter::TopBy::Sum;
        top.value1 = 50;
        QCOMPARE(filtered(top), (QStringList{"East|48", "West|27", "Grand Total|75"}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) {
            p.fields[0].filters.clear();
            p.fields[0].hidden = {2}; // North unchecked
        }));
        QCOMPARE(body(wb, ref).mid(1), (QStringList{"East|48", "West|27", "Grand Total|75"}));
        // Top 1 product inside each region: the totals follow what is shown.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            p.fields[0].hidden.clear();
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            PivotFilter best;
            best.kind = PivotFilter::Kind::Top;
            best.value1 = 1;
            p.fields[1].filters = {best};
        }));
        QCOMPARE(body(wb, ref).mid(1), (QStringList{"East|30", "Pear|30", "North|5", "Apple|5", "West|20", "Pear|20",
                                                    "Grand Total|55"}));
    }
};

QTEST_GUILESS_MAIN(CalcPivotValuesTest)

#include "tst_calc_pivot_values.moc"
