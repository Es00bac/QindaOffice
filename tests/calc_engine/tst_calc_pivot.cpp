// SPDX-License-Identifier: GPL-3.0-or-later
// M74 (D-083) PivotTable layouts and summaries against hand-worked grids
// (pivot_fixture.h): compact, outline and tabular forms; subtotals at the
// top, at the bottom and off; grand totals; repeated labels and blank
// rows; column fields; two data fields (Σ Values); filters (page fields);
// the empty-cell caption; Excel's empty placeholder; every Summarize Values
// By function; and the rowItems / colItems lines with the location
// metrics a save writes.

#include "pivot_fixture.h"

using namespace PivotTest;

class CalcPivotTest : public QObject {
    Q_OBJECT

    static Workbook *book()
    {
        static Workbook *instance = nullptr;
        delete instance;
        instance = new Workbook;
        fillSales(*instance);
        return instance;
    }

    static void sales(PivotTable &p, const PivotCache &c)
    {
        QVERIFY(pivotPlaceField(p, c, 3, PivotArea::Values));
    }

private slots:
    void emptyPivotIsExcelsPlaceholder()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(ref.isValid());
        const PivotTable &p = pivot(wb, ref);
        QCOMPARE(p.name, QStringLiteral("PivotTable1"));
        QVERIFY(p.render && p.render->empty);
        QCOMPARE(p.render->body, (CellRange{{2, 0}, {19, 2}})); // A3:C20, as Excel writes it
        QVERIFY(p.render->rowLines.isEmpty() && p.render->cells.isEmpty());
        QCOMPARE(cacheOf(wb, ref).fields.size(), 5);
        QCOMPARE(cacheOf(wb, ref).recordCount, 6);
        QCOMPARE(cacheOf(wb, ref).fields.at(2).numberFormat, QStringLiteral("m/d/yyyy"));
    }

    void compactOneRowField()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            QVERIFY(pivotPlaceField(p, c, 0, PivotArea::Rows));
            sales(p, c);
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "East|48", "North|5", "West|27", "Grand Total|80"}));
        const PivotRender &r = *pivot(wb, ref).render;
        QCOMPARE(r.body, (CellRange{{2, 0}, {6, 1}}));
        QCOMPARE(r.firstHeaderRow, 1);
        QCOMPARE(r.firstDataRow, 1);
        QCOMPARE(r.firstDataCol, 1);
        QCOMPARE(lines(r.rowLines), (QStringList{"i x=0", "i x=1", "i x=2", "grand x=0"}));
        QCOMPARE(lines(r.colLines), (QStringList{"i"}));
        // The order Excel writes as <items>: East, North, West by name,
        // pointing at shared items East(0), West(1), North(2).
        QCOMPARE(pivot(wb, ref).fields.at(0).order, (QList<qint32>{0, 2, 1}));
    }

    void compactTwoRowFieldsIndents()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            sales(p, c);
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "East|48", "Apple|18", "Pear|30", "North|5",
                                             "Apple|5", "West|27", "Apple|7", "Pear|20", "Grand Total|80"}));
        const PivotRender &r = *pivot(wb, ref).render;
        QCOMPARE(lines(r.rowLines), (QStringList{"i x=0", "i r=1 x=0", "i r=1 x=1", "i x=1", "i r=1 x=0", "i x=2",
                                                  "i r=1 x=0", "i r=1 x=1", "grand x=0"}));
        QCOMPARE(r.cellAt({3, 0})->indent, quint8(0));
        QCOMPARE(r.cellAt({4, 0})->indent, quint8(1));
        QVERIFY(r.rowLines.at(0).group && r.rowLines.at(0).hasValues);
    }

    void outlineForm()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            sales(p, c);
            p.options.form = PivotForm::Outline;
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Region|Product|Sum of Sales", "East||48", "|Apple|18", "|Pear|30",
                                             "North||5", "|Apple|5", "West||27", "|Apple|7", "|Pear|20",
                                             "Grand Total||80"}));
        QCOMPARE(pivot(wb, ref).render->firstDataCol, 2);
    }

    void tabularFormSubtotalsBelow()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            sales(p, c);
            p.options.form = PivotForm::Tabular;
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Region|Product|Sum of Sales", "East|Apple|18", "|Pear|30", "East Total||48",
                                             "North|Apple|5", "North Total||5", "West|Apple|7", "|Pear|20",
                                             "West Total||27", "Grand Total||80"}));
        QCOMPARE(lines(pivot(wb, ref).render->rowLines),
                 (QStringList{"i x=0 x=0", "i r=1 x=1", "default x=0", "i x=1 x=0", "default x=1", "i x=2 x=0",
                              "i r=1 x=1", "default x=2", "grand x=0"}));
        // Repeat All Item Labels fills the Region column down.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) { p.fields[0].repeatLabels = true; }));
        QCOMPARE(body(wb, ref).mid(1, 3), (QStringList{"East|Apple|18", "East|Pear|30", "East Total||48"}));
    }

    void subtotalsAtBottomAndOff()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            sales(p, c);
            p.fields[0].subtotalTop = false;
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "East|", "Apple|18", "Pear|30", "East Total|48",
                                             "North|", "Apple|5", "North Total|5", "West|", "Apple|7", "Pear|20",
                                             "West Total|27", "Grand Total|80"}));
        QCOMPARE(lines(pivot(wb, ref).render->rowLines),
                 (QStringList{"i x=0", "i r=1 x=0", "i r=1 x=1", "default x=0", "i x=1", "i r=1 x=0", "default x=1",
                              "i x=2", "i r=1 x=0", "i r=1 x=1", "default x=2", "grand x=0"}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) { p.fields[0].subtotals = false; }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "East|", "Apple|18", "Pear|30", "North|", "Apple|5",
                                             "West|", "Apple|7", "Pear|20", "Grand Total|80"}));
        // Custom subtotals: Sum and Count under each region.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) {
            p.fields[0].customSubtotals = {PivotFunction::Sum, PivotFunction::Count};
        }));
        QCOMPARE(body(wb, ref).mid(1, 5), (QStringList{"East|", "Apple|18", "Pear|30", "East Sum|48", "East Count|3"}));
    }

    void blankRowsAfterItems()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            sales(p, c);
            p.fields[0].insertBlankRow = true;
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales", "East|48", "Apple|18", "Pear|30", "|", "North|5",
                                             "Apple|5", "|", "West|27", "Apple|7", "Pear|20", "|", "Grand Total|80"}));
        QCOMPARE(lines(pivot(wb, ref).render->rowLines).at(3), QStringLiteral("blank x=0"));
    }

    void columnFieldsAndGrandTotals()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Columns);
            sales(p, c);
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Sum of Sales|Column Labels||", "Row Labels|Apple|Pear|Grand Total",
                                             "East|18|30|48", "North|5||5", "West|7|20|27", "Grand Total|30|50|80"}));
        const PivotRender &r = *pivot(wb, ref).render;
        QCOMPARE(r.firstHeaderRow, 1);
        QCOMPARE(r.firstDataRow, 2);
        QCOMPARE(r.firstDataCol, 1);
        QCOMPARE(r.body, (CellRange{{2, 0}, {7, 3}}));
        QCOMPARE(lines(r.colLines), (QStringList{"i x=0", "i x=1", "grand x=0"}));
        // rowGrandTotals is the Grand Total ROW, colGrandTotals the column.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) {
            p.options.rowGrandTotals = false;
            p.options.colGrandTotals = false;
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Sum of Sales|Column Labels|", "Row Labels|Apple|Pear", "East|18|30",
                                             "North|5|", "West|7|20"}));
        // The empty-cell caption.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) { p.options.missingCaption = QStringLiteral("-"); }));
        QCOMPARE(body(wb, ref).at(3), QStringLiteral("North|5|-"));
    }

    void twoDataFieldsBringSigmaValues()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            sales(p, c);
            pivotPlaceField(p, c, 4, PivotArea::Values);
        }));
        QCOMPARE(pivot(wb, ref).colFields, (QList<qint32>{kPivotValuesField}));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Sales|Sum of Qty", "East|48|10", "North|5|4", "West|27|7",
                                             "Grand Total|80|21"}));
        const PivotRender &r = *pivot(wb, ref).render;
        QCOMPARE(r.firstHeaderRow, 0);
        QCOMPARE(r.firstDataRow, 1);
        QCOMPARE(lines(r.colLines), (QStringList{"i x=0", "i i=1 x=1"}));
        // With Product on Columns before Σ Values: a column per product and
        // data field, totals per data field at the end.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 1, PivotArea::Columns, 0);
        }));
        QCOMPARE(body(wb, ref).mid(0, 4),
                 (QStringList{"|Column Labels|||||", "|Apple||Pear||Total Sum of Sales|Total Sum of Qty",
                              "Row Labels|Sum of Sales|Sum of Qty|Sum of Sales|Sum of Qty||",
                              "East|18|7|30|3|48|10"}));
        QCOMPARE(lines(pivot(wb, ref).render->colLines),
                 (QStringList{"i x=0 x=0", "i r=1 i=1 x=1", "i x=1 x=0", "i r=1 i=1 x=1", "grand x=0", "grand i=1 x=1"}));
    }

    void pageFieldsSitAboveTheBody()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 1, PivotArea::Filters);
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            sales(p, c);
        }));
        QCOMPARE(grid(wb, 1, {{0, 0}, {6, 1}}), (QStringList{"Product|(All)", "|", "Row Labels|Sum of Sales", "East|48",
                                                             "North|5", "West|27", "Grand Total|80"}));
        QCOMPARE(pivot(wb, ref).render->pageRows, 1);
        QCOMPARE(pivot(wb, ref).extent, (CellRange{{0, 0}, {6, 1}}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) { p.fields[1].pageItem = 0; })); // Apple
        QCOMPARE(grid(wb, 1, {{0, 0}, {6, 1}}), (QStringList{"Product|Apple", "|", "Row Labels|Sum of Sales", "East|18",
                                                             "North|5", "West|7", "Grand Total|30"}));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) {
            p.fields[1].pageItem = -1;
            p.fields[1].multipleSelection = true;
            p.fields[1].hidden = {1}; // Pear unchecked
        }));
        QCOMPARE(grid(wb, 1, {{0, 0}, {0, 1}}), (QStringList{"Product|(Multiple Items)"}));
        QCOMPARE(body(wb, ref).last(), QStringLiteral("Grand Total|30"));
        // A second filter pushes the body down: there is no room above A3.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) { pivotPlaceField(p, c, 2, PivotArea::Filters); }));
        QCOMPARE(pivot(wb, ref).location, (CellAddr{3, 0}));
        QCOMPARE(grid(wb, 1, {{0, 0}, {3, 0}}), (QStringList{"Product", "Date", "", "Row Labels"}));
    }

    void onlyValues()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) { sales(p, c); }));
        QCOMPARE(body(wb, ref), (QStringList{"Sum of Sales", "80"}));
        QCOMPARE(pivot(wb, ref).render->firstDataCol, 0);
        QCOMPARE(lines(pivot(wb, ref).render->rowLines), (QStringList{"i"}));
    }

    void everySummaryFunction_data()
    {
        QTest::addColumn<int>("function");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QStringList>("values"); // East, North, West, Grand Total
        QTest::newRow("sum") << int(PivotFunction::Sum) << "Sum of Sales" << QStringList{"48", "5", "27", "80"};
        QTest::newRow("count") << int(PivotFunction::Count) << "Count of Sales" << QStringList{"3", "1", "2", "6"};
        QTest::newRow("average") << int(PivotFunction::Average) << "Average of Sales"
                                 << QStringList{"16", "5", "13.5", "13.3333"};
        QTest::newRow("max") << int(PivotFunction::Max) << "Max of Sales" << QStringList{"30", "5", "20", "30"};
        QTest::newRow("min") << int(PivotFunction::Min) << "Min of Sales" << QStringList{"8", "5", "7", "5"};
        QTest::newRow("product") << int(PivotFunction::Product) << "Product of Sales"
                                 << QStringList{"2400", "5", "140", "1680000"};
        QTest::newRow("countNums") << int(PivotFunction::CountNums) << "Count of Sales" << QStringList{"3", "1", "2", "6"};
        QTest::newRow("stdDev") << int(PivotFunction::StdDev) << "StdDev of Sales"
                                << QStringList{"12.1655", "#DIV/0!", "9.1924", "9.7091"};
        QTest::newRow("stdDevp") << int(PivotFunction::StdDevP) << "StdDevp of Sales"
                                 << QStringList{"9.9331", "0", "6.5", "8.8632"};
        QTest::newRow("var") << int(PivotFunction::Var) << "Var of Sales" << QStringList{"148", "#DIV/0!", "84.5", "94.2667"};
        QTest::newRow("varp") << int(PivotFunction::VarP) << "Varp of Sales"
                              << QStringList{"98.6667", "0", "42.25", "78.5556"};
    }

    void everySummaryFunction()
    {
        QFETCH(int, function);
        QFETCH(QString, name);
        QFETCH(QStringList, values);
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [&](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            sales(p, c);
            p.dataFields[0].function = PivotFunction(function);
            p.dataFields[0].name = pivotDefaultDataName(c, PivotTable{}, 3, PivotFunction(function));
        }));
        const QStringList rows = body(wb, ref);
        QCOMPARE(rows.first(), QStringLiteral("Row Labels|") + name);
        QStringList shown;
        for (const QString &row : rows.mid(1)) {
            shown.append(row.section(QLatin1Char('|'), 1));
        }
        QCOMPARE(shown, values);
    }

    void textFieldsAreCounted()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            QVERIFY(pivotPlaceField(p, c, 0, PivotArea::Values));
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Count of Region", "Apple|4", "Pear|2", "Grand Total|6"}));
        // The field list's checkbox: numbers to Values, the rest to Rows.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotToggleField(p, c, 4, true);
            pivotToggleField(p, c, 0, false);
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Qty", "Apple|16", "Pear|5", "Grand Total|21"}));
    }
};

QTEST_GUILESS_MAIN(CalcPivotTest)

#include "tst_calc_pivot.moc"
