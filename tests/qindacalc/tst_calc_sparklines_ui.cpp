// SPDX-License-Identifier: GPL-3.0-or-later
// M90: real dialogs, contextual commands, visibility-sensitive totals and undo.
#include "calc_m78_support.h"
#include "sheet.h"
#include <QUndoStack>
using namespace CalcTest;
using namespace CalcTest::M78;
using namespace QindaOffice::Calc;

class CalcSparklinesUiTest : public QObject {
    Q_OBJECT
private slots:
    void sparklines_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("wide") << 1280;
        QTest::newRow("narrow") << 520;
    }
    void sparklines()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
            type(scene, r + 1, c, QString::number((r + 1) * (c % 2 == 0 ? c + 1 : -c)));
        scene.grid->setCursorCell(1, 0);
        scene.grid->setCursorCell(3, 3, true);
        const int steps = scene.doc->undoStack()->count();
        trigger(scene, ActionId::kCalcInsertSparklineLine);
        QVERIFY(dialogOpened(scene, "sparklineCreateDialog"));
        QVERIFY(setText(scene, "sparklineDataRange", QStringLiteral("A2:D4")));
        QVERIFY(setText(scene, "sparklineLocationRange", QStringLiteral("E2:E4")));
        grab(scene, QStringLiteral("qindacalc_m90_sparklines_dialog_%1").arg(width));
        QVERIFY(answer(scene, "sparklineCreateDialog", true));
        const auto &sheet = scene.doc->workbook()->sheetAt(0);
        QCOMPARE(sheet.sparklineGroups().size(), 1);
        QCOMPARE(sheet.sparklineGroups().first().sparklines.size(), 3);
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        scene.grid->setCursorCell(1, 4);
        QVERIFY(scene.host.ribbonContexts().contains(QStringLiteral("sparkline")));
        trigger(scene, ActionId::kCalcSparklineTypeColumn);
        QCOMPARE(sheet.sparklineGroups().first().type, SparklineGroup::Type::Column);
        trigger(scene, ActionId::kCalcSparklineHighPoint);
        QVERIFY(sheet.sparklineGroups().first().high);
        scene.item(QStringLiteral("officeRibbon"))->setProperty("currentTabId", QStringLiteral("sparkline"));
        grab(scene, QStringLiteral("qindacalc_m90_sparklines_cells_%1").arg(width));
        SpreadsheetDocument recovered;
        QString error;
        QVERIFY2(recovered.loadRecovery(scene.doc->recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(recovered.workbook()->sheetAt(0).sparklineGroups(), sheet.sparklineGroups());
        trigger(scene, ActionId::kCalcSparklineClearGroup);
        QVERIFY(sheet.sparklineGroups().isEmpty());
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->sheetAt(0).sparklineGroups().size(), 1);
    }
    void tableDesignAndVisibleTotals()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, 0, 0, QStringLiteral("Sales"));
        type(scene, 1, 0, QStringLiteral("2"));
        type(scene, 2, 0, QStringLiteral("3"));
        scene.grid->setCursorCell(1, 0);
        trigger(scene, ActionId::kCalcInsertTable);
        QVERIFY(scene.host.ribbonContexts().contains(QStringLiteral("table")));
        trigger(scene, ActionId::kCalcTableRename);
        QVERIFY(dialogOpened(scene, "tableRenameDialog"));
        QVERIFY(popup(scene, "tableRenameDialog")->setProperty("text", QStringLiteral("SalesTable")));
        QVERIFY(answer(scene, "tableRenameDialog", true));
        QCOMPARE(scene.host.tableName(), QStringLiteral("SalesTable"));
        trigger(scene, ActionId::kCalcTableResize);
        QVERIFY(dialogOpened(scene, "tableResizeDialog"));
        QVERIFY(popup(scene, "tableResizeDialog")->setProperty("text", QStringLiteral("A1:B4")));
        QVERIFY(answer(scene, "tableResizeDialog", true));
        QCOMPARE(scene.doc->workbook()->sheetAt(0).tables().first().range, (CellRange{{0, 0}, {3, 1}}));
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->sheetAt(0).tables().first().range, (CellRange{{0, 0}, {2, 0}}));
        type(scene, 5, 0, QStringLiteral("=SUBTOTAL(109,A2:A3)"));
        QCOMPARE(cell(scene, 0, 5, 0), Value(5.0));
        scene.doc->setLinesHidden(OutlineAxis::Rows, 1, 1, true);
        QCOMPARE(cell(scene, 0, 5, 0), Value(3.0));
        scene.doc->undoStack()->undo();
        QCOMPARE(cell(scene, 0, 5, 0), Value(5.0));
        AutoFilter filter;
        filter.active = true;
        filter.range = {{0, 0}, {2, 0}};
        filter.allowed.insert(0, {QStringLiteral("2")});
        scene.doc->setAutoFilter(filter);
        QCOMPARE(cell(scene, 0, 5, 0), Value(2.0));
    }
};
CALC_TEST_MAIN(CalcSparklinesUiTest)
#include "tst_calc_sparklines_ui.moc"
