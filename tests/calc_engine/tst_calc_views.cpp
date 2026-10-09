// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091): Excel's Custom Views in the model (custom_view.h). Add takes
// the workbook as it is — frozen panes, sheet visibility, and as asked the
// hidden rows and columns with the filter and the print settings — with
// each sheet's window part as given; Show puts the document part back and
// says which sheet the view shows; Add replaces a view of the same name;
// Delete removes every sheet's part; a workbook with a table refuses, as
// Excel does; structural edits move a view's hidden lines, print area,
// titles, breaks and cells, and renaming a sheet keeps the view on it.

#include "custom_view.h"
#include "structure.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

CellRange range(qint32 r0, qint32 c0, qint32 r1, qint32 c1)
{
    return CellRange{{r0, c0}, {r1, c1}};
}

void hide(Sheet &sheet, OutlineAxis axis, qint32 line, bool hidden = true)
{
    SheetOutline outline = sheet.outline();
    LineOutline state = outline.line(axis, line);
    state.hidden = hidden;
    outline.setLine(axis, line, state);
    sheet.setOutline(outline);
}

QList<qint32> hiddenRows(const Sheet &sheet)
{
    QList<qint32> out;
    for (auto it = sheet.outline().rows.cbegin(); it != sheet.outline().rows.cend(); ++it) {
        if (it.value().hidden) {
            out.append(it.key());
        }
    }
    return out;
}

Workbook twoSheets()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    for (qint32 r = 0; r < 30; ++r) {
        for (qint32 c = 0; c < 6; ++c) {
            wb.setCellValue(0, {r, c}, Value(double(r * 10 + c)));
        }
    }
    wb.addSheet(QStringLiteral("Notes"));
    return wb;
}

} // namespace

class CalcViewsTest : public QObject {
    Q_OBJECT
private slots:
    void addAndShowPutTheWorkbookBack();
    void optionsLeaveHiddenLinesAndPrintAlone();
    void addReplacesTheSameNameAndDeleteRemovesEveryPart();
    void tablesRefuseCustomViews();
    void structuralEditsMoveTheViews();
};

void CalcViewsTest::addAndShowPutTheWorkbookBack()
{
    Workbook wb = twoSheets();
    Sheet &data = wb.sheetAt(0);
    data.setFreeze(1, 1);
    hide(data, OutlineAxis::Rows, 4);
    hide(data, OutlineAxis::Rows, 5);
    hide(data, OutlineAxis::Columns, 2);
    data.setAutoFilter({range(0, 0, 29, 5), {{1, {QStringLiteral("11")}}}, true});
    data.setPrintArea(range(0, 0, 19, 3));
    Sheet::PageSetup setup;
    setup.orientation = 1;
    setup.rowBreaks = {10};
    data.setPageSetup(setup);
    data.setPrintTitles(range(0, 0, 0, Sheet::kMaxCols - 1), std::nullopt);
    wb.sheetAt(1).setHidden(true);

    QHash<qint32, Sheet::CustomView> window;
    Sheet::CustomView part;
    part.scale = 75;
    part.topLeft = {3, 1};
    part.activeCell = {6, 2};
    window.insert(0, part);
    const QString guid = addCustomView(wb, QStringLiteral("Filtered"), true, true, 0, window);
    QVERIFY(guid.startsWith(QLatin1Char('{')) && guid.endsWith(QLatin1Char('}')));
    QCOMPARE(guid, guid.toUpper());
    QCOMPARE(wb.customViews.size(), 1);
    QCOMPARE(wb.customViews.first().activeSheet, QStringLiteral("Data"));
    const Sheet::CustomView *saved = customViewOf(wb.sheetAt(0), guid);
    QVERIFY(saved != nullptr);
    QCOMPARE(saved->scale, 75);
    QCOMPARE(saved->topLeft, (CellAddr{3, 1}));
    QCOMPARE(saved->activeCell, (CellAddr{6, 2}));
    QCOMPARE(saved->freezeRows, 1);
    QCOMPARE(saved->hiddenRows, (QList<qint32>{4, 5}));
    QCOMPARE(saved->hiddenColumns, (QList<qint32>{2}));
    QVERIFY(saved->filter);
    QVERIFY(saved->printSettings);
    QCOMPARE(saved->pageSetup.orientation, 1);
    QCOMPARE(*saved->printArea, range(0, 0, 19, 3));
    QVERIFY(customViewOf(wb.sheetAt(1), guid)->sheetHidden);

    // Change everything, then Show puts it back.
    data.setFreeze(0, 0);
    hide(data, OutlineAxis::Rows, 4, false);
    hide(data, OutlineAxis::Rows, 5, false);
    hide(data, OutlineAxis::Rows, 20);
    hide(data, OutlineAxis::Columns, 2, false);
    data.setAutoFilter({});
    data.setPrintArea(std::nullopt);
    data.setPageSetup({});
    data.setPrintTitles(std::nullopt, std::nullopt);
    wb.sheetAt(1).setHidden(false);
    qint32 active = -1;
    QVERIFY(showCustomView(wb, guid, &active));
    QCOMPARE(active, 0);
    QCOMPARE(data.freezeRows(), 1);
    QCOMPARE(data.freezeCols(), 1);
    QCOMPARE(hiddenRows(data), (QList<qint32>{4, 5})); // row 20 shown again
    QVERIFY(data.outline().isHidden(OutlineAxis::Columns, 2));
    QVERIFY(data.autoFilter().active);
    QCOMPARE(data.autoFilter().range, range(0, 0, 29, 5));
    QCOMPARE(*data.printArea(), range(0, 0, 19, 3));
    QCOMPARE(data.pageSetup().orientation, 1);
    QCOMPARE(data.pageSetup().rowBreaks, (QList<qint32>{10}));
    QVERIFY(data.printTitleRows().has_value());
    QVERIFY(wb.sheetAt(1).isHidden());
    QVERIFY(!showCustomView(wb, QStringLiteral("{00000000-0000-0000-0000-000000000000}"), &active));
}

void CalcViewsTest::optionsLeaveHiddenLinesAndPrintAlone()
{
    Workbook wb = twoSheets();
    Sheet &data = wb.sheetAt(0);
    hide(data, OutlineAxis::Rows, 7);
    data.setPrintArea(range(0, 0, 9, 1));
    const QString guid = addCustomView(wb, QStringLiteral("Plain"), false, false, 1, {});
    const Sheet::CustomView *saved = customViewOf(data, guid);
    QVERIFY(saved != nullptr);
    QVERIFY(saved->hiddenRows.isEmpty());
    QVERIFY(!saved->printSettings);
    QVERIFY(!wb.customViews.first().includePrintSettings);
    QVERIFY(!wb.customViews.first().includeHiddenRowCol);
    // Show leaves what the view does not hold as it is now.
    hide(data, OutlineAxis::Rows, 7, false);
    hide(data, OutlineAxis::Rows, 8);
    data.setPrintArea(range(0, 0, 4, 4));
    qint32 active = -1;
    QVERIFY(showCustomView(wb, guid, &active));
    QCOMPARE(active, 1);
    QCOMPARE(hiddenRows(data), (QList<qint32>{8}));
    QCOMPARE(*data.printArea(), range(0, 0, 4, 4));
}

void CalcViewsTest::addReplacesTheSameNameAndDeleteRemovesEveryPart()
{
    Workbook wb = twoSheets();
    const QString first = addCustomView(wb, QStringLiteral("Review"), true, true, 0, {});
    const QString other = addCustomView(wb, QStringLiteral("Other"), true, true, 0, {});
    const QString second = addCustomView(wb, QStringLiteral("review"), true, true, 1, {});
    QVERIFY(first != second);
    QCOMPARE(wb.customViews.size(), 2);
    QCOMPARE(wb.customViews.at(0).guid, other);
    QCOMPARE(wb.customViews.at(1).guid, second);
    QVERIFY(customViewOf(wb.sheetAt(0), first) == nullptr);
    QCOMPARE(wb.sheetAt(0).customViews().size(), 2);
    QCOMPARE(wb.sheetAt(1).customViews().size(), 2);
    QVERIFY(deleteCustomView(wb, other));
    QCOMPARE(wb.customViews.size(), 1);
    QCOMPARE(wb.sheetAt(0).customViews().size(), 1);
    QCOMPARE(wb.sheetAt(1).customViews().size(), 1);
    QVERIFY(!deleteCustomView(wb, other));
}

void CalcViewsTest::tablesRefuseCustomViews()
{
    Workbook wb = twoSheets();
    QVERIFY(customViewsAllowed(wb));
    TableSpec table;
    table.name = QStringLiteral("Table1");
    table.range = range(0, 0, 5, 2);
    wb.sheetAt(1).setTables({table});
    QVERIFY(!customViewsAllowed(wb));
}

void CalcViewsTest::structuralEditsMoveTheViews()
{
    Workbook wb = twoSheets();
    Sheet &data = wb.sheetAt(0);
    hide(data, OutlineAxis::Rows, 10);
    hide(data, OutlineAxis::Columns, 3);
    data.setPrintArea(range(2, 1, 19, 4));
    data.setPrintTitles(range(0, 0, 1, Sheet::kMaxCols - 1), std::nullopt);
    Sheet::PageSetup setup;
    setup.rowBreaks = {12};
    data.setPageSetup(setup);
    data.setAutoFilter({range(2, 0, 29, 5), {}, true});
    QHash<qint32, Sheet::CustomView> window;
    Sheet::CustomView part;
    part.topLeft = {5, 2};
    part.activeCell = {8, 4};
    window.insert(0, part);
    const QString guid = addCustomView(wb, QStringLiteral("Moving"), true, true, 0, window);

    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 3});
    const Sheet::CustomView *moved = customViewOf(wb.sheetAt(0), guid);
    QCOMPARE(moved->hiddenRows, (QList<qint32>{13}));
    QCOMPARE(*moved->printArea, range(5, 1, 22, 4));
    QCOMPARE(*moved->printTitleRows, (CellRange{{3, 0}, {4, Sheet::kMaxCols - 1}}));
    QCOMPARE(moved->pageSetup.rowBreaks, (QList<qint32>{15}));
    QCOMPARE(moved->autoFilter.range, range(5, 0, 32, 5));
    QCOMPARE(moved->topLeft, (CellAddr{8, 2}));
    QCOMPARE(moved->activeCell, (CellAddr{11, 4}));

    // Column D (the hidden one) goes: so does its mark; the area closes up.
    applyStructureEdit(wb, {StructureOp::RemoveColumns, 0, 3, 1});
    moved = customViewOf(wb.sheetAt(0), guid);
    QVERIFY(moved->hiddenColumns.isEmpty());
    QCOMPARE(*moved->printArea, range(5, 1, 22, 3));
    QCOMPARE(moved->activeCell, (CellAddr{11, 3}));

    // The other sheet's part is not moved by an edit of this one.
    applyStructureEdit(wb, {StructureOp::InsertRows, 1, 0, 2});
    QCOMPARE(customViewOf(wb.sheetAt(0), guid)->hiddenRows, (QList<qint32>{13}));

    // Renaming the sheet keeps the view on it.
    QVERIFY(renameSheet(wb, 0, QStringLiteral("Figures")));
    QCOMPARE(wb.customViews.first().activeSheet, QStringLiteral("Figures"));
}

QTEST_GUILESS_MAIN(CalcViewsTest)
#include "tst_calc_views.moc"
