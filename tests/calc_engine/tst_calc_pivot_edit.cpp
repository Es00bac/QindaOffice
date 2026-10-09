// SPDX-License-Identifier: GPL-3.0-or-later
// M74 (D-083) PivotTables in a workbook (pivot_fixture.h): refresh after
// the source changes (new items join at the end, a removed column leaves
// the pivot), sources over ranges and tables, GETPIVOTDATA (and what it
// refuses), structural edits and renames, Show Details, moving, clearing,
// deleting and re-sourcing a pivot, output that would cover data, the
// areas of the field list, and the style the grid paints.

#include "pivot_fixture.h"

#include "pivot_cache.h"
#include "pivot_style.h"
#include "structure.h"
#include "table_edit.h"

using namespace PivotTest;

class CalcPivotEditTest : public QObject {
    Q_OBJECT

    static Workbook *book()
    {
        static Workbook *instance = nullptr;
        delete instance;
        instance = new Workbook;
        fillSales(*instance);
        return instance;
    }

    static PivotRef regionsByProduct(Workbook &wb)
    {
        const PivotRef ref = makePivot(wb);
        change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Columns);
            pivotPlaceField(p, c, 3, PivotArea::Values);
        });
        return ref;
    }

private slots:
    void refreshReadsTheSourceAgain()
    {
        Workbook &wb = *book();
        const PivotRef ref = regionsByProduct(wb);
        const qint32 cache = pivot(wb, ref).cacheId;
        QVERIFY(pivotCacheCurrent(wb, cacheOf(wb, ref)));
        wb.setCellValue(0, {1, 3}, 100.0); // East Apple 10 → 100
        QCOMPARE(body(wb, ref).at(2), QStringLiteral("East|18|30|48")); // Excel's model: not until refreshed
        QVERIFY(!pivotCacheCurrent(wb, cacheOf(wb, ref)));
        QString error;
        QVERIFY2(refreshPivotCache(wb, cache, &error), qPrintable(error));
        QCOMPARE(body(wb, ref).at(2), QStringLiteral("East|108|30|138"));
        // A row inserted inside the source grows it; its new item joins at the end.
        applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 3, 1});
        QCOMPARE(cacheOf(wb, ref).source.range, (CellRange{{0, 0}, {7, 4}}));
        wb.setCellValue(0, {3, 0}, QStringLiteral("South"));
        wb.setCellValue(0, {3, 1}, QStringLiteral("Pear"));
        wb.setCellValue(0, {3, 3}, 9.0);
        QVERIFY(refreshPivotCache(wb, cache, &error));
        QCOMPARE(body(wb, ref).mid(2, 4), (QStringList{"East|108|30|138", "North|5||5", "West|7|20|27", "South||9|9"}));
        // A removed source column leaves the pivot (Product was on Columns).
        applyStructureEdit(wb, StructureEdit{StructureOp::RemoveColumns, 0, 1, 1});
        QVERIFY(refreshPivotCache(wb, cache, &error));
        QCOMPARE(cacheOf(wb, ref).fields.size(), 4);
        QVERIFY(pivot(wb, ref).colFields.isEmpty());
        QCOMPARE(body(wb, ref).last(), QStringLiteral("Grand Total|179"));
        // A renamed data sheet: the source follows.
        QVERIFY(renameSheet(wb, 0, QStringLiteral("Sales Data")));
        QCOMPARE(cacheOf(wb, ref).source.sheet, QStringLiteral("Sales Data"));
        QVERIFY(refreshPivotCache(wb, cache, &error));
        QVERIFY(refreshAllPivots(wb));
    }

    void tableSources()
    {
        Workbook &wb = *book();
        QString error;
        QVERIFY(createTable(wb, 0, {{0, 0}, {6, 4}}, &error));
        const QString table = wb.sheetAt(0).tables().first().name;
        QVERIFY(setTableTotalsRow(wb, 0, 0, true, &error));
        const auto range = pivotSourceRange(wb, PivotSource{QString(), {}, table});
        QVERIFY(range.has_value());
        QCOMPARE(range->second, (CellRange{{0, 0}, {6, 4}})); // the totals row is not data
        const PivotRef ref = createPivotTable(wb, PivotSource{QString(), {}, table}, 1, {2, 0}, &error);
        QVERIFY2(ref.isValid(), qPrintable(error));
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            pivotPlaceField(p, c, 4, PivotArea::Values);
        }));
        QCOMPARE(body(wb, ref), (QStringList{"Row Labels|Sum of Qty", "Apple|16", "Pear|5", "Grand Total|21"}));
        // A source that cannot feed a pivot: Excel's message.
        QVERIFY(!createPivotTable(wb, PivotSource{QStringLiteral("Data"), {{0, 5}, {6, 6}}, QString()}, 1, {30, 0}, &error)
                     .isValid());
        QVERIFY(error.contains(QStringLiteral("field name is not valid")));
    }

    void getPivotData()
    {
        Workbook &wb = *book();
        const PivotRef ref = regionsByProduct(wb);
        const auto value = [&wb](const QString &formula) {
            wb.setCellInput(1, {0, 8}, formula);
            return text(wb.cellValue(1, 0, 8));
        };
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sum of Sales\",$A$3,\"Region\",\"East\",\"Product\",\"Pear\")")),
                 QStringLiteral("30"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3)")), QStringLiteral("80"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$B$5,\"Region\",\"west\")")), QStringLiteral("27"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Product\",\"Apple\")")), QStringLiteral("30"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"North\",\"Product\",\"Pear\")")),
                 QStringLiteral("#REF!"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"Nowhere\")")), QStringLiteral("#REF!"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Qty\",$A$3)")), QStringLiteral("#REF!"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$Z$99)")), QStringLiteral("#REF!"));
        // The formula follows a refresh.
        wb.setCellInput(1, {0, 8}, QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"East\",\"Product\",\"Pear\")"));
        wb.setCellValue(0, {3, 3}, 35.0); // East Pear 30 → 35
        QCOMPARE(text(wb.cellValue(1, 0, 8)), QStringLiteral("30"));
        QVERIFY(refreshPivotCache(wb, pivot(wb, ref).cacheId));
        QCOMPARE(text(wb.cellValue(1, 0, 8)), QStringLiteral("35"));
        // Only what is shown: Product alone under Region names nothing; a
        // subtotal turned off is not there.
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            p.fields[0].subtotals = false;
        }));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Product\",\"Apple\")")), QStringLiteral("#REF!"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"East\")")), QStringLiteral("#REF!"));
        QCOMPARE(value(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"East\",\"Product\",\"Apple\")")),
                 QStringLiteral("18"));
    }

    void structuralEditsMovePivots()
    {
        Workbook &wb = *book();
        const PivotRef ref = regionsByProduct(wb);
        wb.setCellInput(1, {0, 8}, QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"West\")"));
        applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 1, 0, 2});
        QCOMPARE(pivot(wb, ref).location, (CellAddr{4, 0}));
        QCOMPARE(pivot(wb, ref).extent, (CellRange{{4, 0}, {9, 3}}));
        QCOMPARE(pivot(wb, ref).render->body, (CellRange{{4, 0}, {9, 3}}));
        QCOMPARE(text(wb.cellValue(1, 2, 8)), QStringLiteral("27"));
        QCOMPARE(wb.cellFormula(1, 2, 8), QStringLiteral("GETPIVOTDATA(\"Sales\",$A$5,\"Region\",\"West\")"));
        QVERIFY(pivotSplitBy(wb, 1, true, 6, 1, true));
        QVERIFY(pivotSplitBy(wb, 1, false, 2, 1, false));
        QVERIFY(!pivotSplitBy(wb, 1, true, 3, 1, true));
        QVERIFY(!pivotSplitBy(wb, 1, false, 6, 2, false));
        // Deleting every row it stands on takes the pivot with them.
        applyStructureEdit(wb, StructureEdit{StructureOp::RemoveRows, 1, 0, 20});
        QVERIFY(wb.sheetAt(1).pivots().isEmpty());
    }

    void showDetails()
    {
        Workbook &wb = *book();
        const PivotRef ref = regionsByProduct(wb);
        const auto details = pivotDetails(wb, ref, {4, 2}); // East / Pear
        QVERIFY(details.has_value());
        QCOMPARE(details->headers, (QStringList{"Region", "Product", "Date", "Sales", "Qty"}));
        QCOMPARE(details->rows.size(), 1);
        QCOMPARE(details->rows.first().at(3), Value(30.0));
        QCOMPARE(details->formats.at(2), QStringLiteral("m/d/yyyy"));
        QCOMPARE(pivotDetails(wb, ref, {4, 0})->rows.size(), 3); // the East label
        QCOMPARE(pivotDetails(wb, ref, {7, 3})->rows.size(), 6); // the grand total
        const PivotCellInfo info = pivotCellInfo(wb, ref, {4, 2});
        QVERIFY(info.value);
        QCOMPARE(info.dataField, 0);
        const PivotCellInfo label = pivotCellInfo(wb, ref, {5, 0});
        QVERIFY(label.label && label.rows);
        QCOMPARE(label.field, 0);
        QCOMPARE(label.item, 2); // North's shared item
        QCOMPARE(pivotCellInfo(wb, ref, {3, 2}).field, 1); // a column label: Product
    }

    void moveClearDeleteAndResource()
    {
        Workbook &wb = *book();
        const PivotRef ref = regionsByProduct(wb);
        QString error;
        QVERIFY2(movePivotTable(wb, ref, 1, {10, 5}, &error), qPrintable(error));
        QVERIFY(isBlank(wb.cellValue(1, 2, 0)));
        QCOMPARE(text(wb.cellValue(1, 10, 5)), QStringLiteral("Sum of Sales"));
        QCOMPARE(pivot(wb, ref).extent, (CellRange{{10, 5}, {15, 8}}));
        QVERIFY(changePivotSource(wb, ref, PivotSource{QStringLiteral("Data"), {{0, 0}, {3, 4}}, QString()}, &error));
        QCOMPARE(grid(wb, 1, pivot(wb, ref).render->body).last(), QStringLiteral("Grand Total|10|50|60"));
        // Output that would cover data is refused, and nothing changes;
        // Excel's "replace it?" answered Yes writes over it.
        wb.setCellValue(1, {16, 5}, QStringLiteral("in the way"));
        const CellRange before = pivot(wb, ref).extent;
        PivotTable wider = pivot(wb, ref);
        QVERIFY(pivotPlaceField(wider, cacheOf(wb, ref), 2, PivotArea::Rows));
        wb.sheetAt(1).mutablePivots()[ref.index] = wider;
        QVERIFY(!renderPivotTable(wb, ref, &error));
        QVERIFY2(error.contains(QStringLiteral("already data")), qPrintable(error));
        QCOMPARE(pivot(wb, ref).extent, before);
        QCOMPARE(text(wb.cellValue(1, 16, 5)), QStringLiteral("in the way"));
        QVERIFY(renderPivotTable(wb, ref, &error, true));
        QVERIFY(pivot(wb, ref).extent.contains({16, 5}));
        QVERIFY(text(wb.cellValue(1, 16, 5)) != QStringLiteral("in the way"));
        QVERIFY(clearPivotTable(wb, ref, &error));
        QVERIFY(pivot(wb, ref).render->empty);
        QVERIFY(isBlank(wb.cellValue(1, 11, 6)) && isBlank(wb.cellValue(1, 16, 5)));
        QVERIFY(removePivotTable(wb, ref));
        QVERIFY(wb.sheetAt(1).pivots().isEmpty());
    }

    void areasOfTheFieldList()
    {
        Workbook &wb = *book();
        const PivotRef ref = regionsByProduct(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            QVERIFY(pivotMoveField(p, c, PivotArea::Rows, 0, PivotArea::Columns, 0));
            QCOMPARE(p.colFields, (QList<qint32>{0, 1}));
            QVERIFY(p.rowFields.isEmpty());
            QVERIFY(pivotPlaceField(p, c, 4, PivotArea::Values, 0)); // Sum of Qty first
            p.fields[0].sortData = 1;                               // sorted by Sum of Sales
            QVERIFY(pivotMoveField(p, c, PivotArea::Values, 0, PivotArea::Values, 1));
            QCOMPARE(p.dataFields.at(0).name, QStringLiteral("Sum of Sales"));
            QCOMPARE(p.fields.at(0).sortData, 0);
            QVERIFY(pivotMoveField(p, c, PivotArea::Values, 1, PivotArea::Rows, 0)); // Qty leaves Values
            QCOMPARE(p.rowFields, (QList<qint32>{4}));
            QVERIFY(!pivotRemoveField(p, PivotArea::Columns, 5));
            QVERIFY(pivotRemoveField(p, PivotArea::Columns, 1));
        }));
        QCOMPARE(pivotAreaFields(pivot(wb, ref), PivotArea::Values), (QList<qint32>{3}));
        QVERIFY(pivot(wb, ref).colFields == (QList<qint32>{0}));
    }

    void stylesAreDrawnFromTheName()
    {
        Workbook &wb = *book();
        const PivotRef ref = makePivot(wb);
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 3, PivotArea::Values);
        }));
        const QVector<Rgb> theme = pivotDefaultTheme();
        const auto header = pivotCellLook(pivot(wb, ref), {2, 1}, theme);
        QVERIFY(header.has_value());
        QCOMPARE(header->fill, std::optional<Rgb>(Rgb{0xDA, 0xE3, 0xF3})); // accent 1, tint 80%
        QVERIFY(header->bold);
        QCOMPARE(header->bottom, std::optional<Rgb>(Rgb{143, 170, 220}));
        const auto data = pivotCellLook(pivot(wb, ref), {3, 1}, theme);
        QVERIFY(data.has_value() && !data->fill.has_value() && !data->bold);
        const auto grand = pivotCellLook(pivot(wb, ref), {6, 0}, theme);
        QVERIFY(grand->bold && grand->top.has_value());
        QVERIFY(!pivotCellLook(pivot(wb, ref), {7, 0}, theme).has_value());
        QVERIFY(change(wb, ref, [](PivotTable &p, const PivotCache &) {
            p.options.style.name = QStringLiteral("PivotStyleMedium9");
        }));
        QCOMPARE(pivotCellLook(pivot(wb, ref), {2, 0}, theme)->text, std::optional<Rgb>(Rgb{255, 255, 255}));
        QVERIFY(pivotCellLook(pivot(wb, ref), {3, 1}, theme)->fill.has_value()); // Medium9 tints the body
        QCOMPARE(pivotStyleNames().size(), 9);
    }
};

QTEST_GUILESS_MAIN(CalcPivotEditTest)

#include "tst_calc_pivot_edit.moc"
