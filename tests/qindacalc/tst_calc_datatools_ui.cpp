// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) through the real QindaCalc window: Data ▸ Data Tools — Text
// to Columns through its wizard (Excel's overwrite question first), Remove
// Duplicates by chosen columns with Excel's report, Flash Fill (Ctrl+E)
// from two examples and its plain refusal, Consolidate across two sheets,
// and Circle Invalid Data / Clear Validation Circles. One undo step each.
// QQO_GRAB_DIR saves the dialogs at 1280 and 520.

#include "calc_m78_support.h"

#include "sheet.h"

#include <QUndoStack>

using namespace CalcTest;
using namespace CalcTest::M78;
using QindaOffice::Calc::Value;

class CalcDataToolsUiTest : public QObject {
    Q_OBJECT

private slots:
    void textToColumnsThroughTheWizard()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, 0, 0, QStringLiteral("Smith,John,42"));
        type(scene, 1, 0, QStringLiteral("Lee,Mary Ann,37"));
        type(scene, 2, 0, QStringLiteral("Ng,Bo,"));
        type(scene, 0, 2, QStringLiteral("in the way"));
        scene.grid->setCursorCell(0, 0);
        scene.grid->setCursorCell(2, 0, true);
        trigger(scene, ActionId::kCalcTextToColumns);
        QVERIFY(dialogOpened(scene, "textToColumnsDialog"));
        QObject *dialog = popup(scene, "textToColumnsDialog");
        scene.item(QStringLiteral("textToColumnsComma"))->setProperty("checked", true);
        scene.item(QStringLiteral("textToColumnsTab"))->setProperty("checked", false);
        dialog->setProperty("step", 1);
        grab(scene, QStringLiteral("qindacalc_m78_text_to_columns_1280"));
        const int steps = scene.doc->undoStack()->count();
        QMetaObject::invokeMethod(dialog, "accept");
        // C1 holds something: Excel asks first.
        QVERIFY(dialogOpened(scene, "textToColumnsOverwrite"));
        QVERIFY(answer(scene, "textToColumnsOverwrite", true));
        QCOMPARE(cell(scene, 0, 0, 0), Value(QStringLiteral("Smith")));
        QCOMPARE(cell(scene, 0, 0, 1), Value(QStringLiteral("John")));
        QCOMPARE(cell(scene, 0, 0, 2), Value(42.0));
        QCOMPARE(cell(scene, 0, 1, 1), Value(QStringLiteral("Mary Ann")));
        QCOMPARE(cell(scene, 0, 2, 0), Value(QStringLiteral("Ng")));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        scene.doc->undoStack()->undo();
        QCOMPARE(cell(scene, 0, 0, 0), Value(QStringLiteral("Smith,John,42")));
        QCOMPARE(cell(scene, 0, 0, 2), Value(QStringLiteral("in the way")));
    }

    void removeDuplicatesWithItsReport()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        const QStringList rows{QStringLiteral("Name,City,Amount"), QStringLiteral("Ann,Oslo,10"), QStringLiteral("Bo,Rome,20"),
                               QStringLiteral("ann,Oslo,30"), QStringLiteral("Bo,Rome,20"), QStringLiteral("Cy,Rome,5")};
        for (int r = 0; r < rows.size(); ++r) {
            const QStringList cells = rows.at(r).split(QLatin1Char(','));
            for (int c = 0; c < cells.size(); ++c) {
                type(scene, r, c, cells.at(c));
            }
        }
        type(scene, 8, 0, QStringLiteral("below"));
        scene.grid->setCursorCell(1, 1);
        trigger(scene, ActionId::kCalcRemoveDuplicates);
        QVERIFY(dialogOpened(scene, "removeDuplicatesDialog"));
        QObject *dialog = popup(scene, "removeDuplicatesDialog");
        QVERIFY(dialog->property("hasHeader").toBool()); // text over numbers: headers
        QTRY_VERIFY(scene.item(QStringLiteral("removeDuplicatesColumn_2")) != nullptr);
        QCOMPARE(scene.item(QStringLiteral("removeDuplicatesColumn_0"))->property("text").toString(), QStringLiteral("Name"));
        grab(scene, QStringLiteral("qindacalc_m78_remove_duplicates_1280"));
        dialog->setProperty("picked", QVariantList{0, 1}); // Name and City
        QVERIFY(answer(scene, "removeDuplicatesDialog", true));
        QVERIFY(dialogOpened(scene, "removeDuplicatesReport"));
        QVERIFY2(popup(scene, "removeDuplicatesReport")->property("text").toString().startsWith(QStringLiteral("2 duplicate")),
                 qPrintable(popup(scene, "removeDuplicatesReport")->property("text").toString()));
        QVERIFY(answer(scene, "removeDuplicatesReport", true));
        QCOMPARE(cell(scene, 0, 1, 0), Value(QStringLiteral("Ann")));
        QCOMPARE(cell(scene, 0, 2, 0), Value(QStringLiteral("Bo")));
        QCOMPARE(cell(scene, 0, 3, 0), Value(QStringLiteral("Cy")));
        QVERIFY(QindaOffice::Calc::isBlank(cell(scene, 0, 4, 0)));
        QCOMPARE(cell(scene, 0, 8, 0), Value(QStringLiteral("below"))); // outside the range: unmoved
    }

    void flashFillFromExamples()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        type(scene, 0, 0, QStringLiteral("Full name"));
        type(scene, 0, 1, QStringLiteral("Last"));
        const QStringList names{QStringLiteral("John Smith"), QStringLiteral("Mary Lee"), QStringLiteral("Bo Ng"),
                                QStringLiteral("Ann Petersen")};
        for (int r = 0; r < names.size(); ++r) {
            type(scene, r + 1, 0, names.at(r));
        }
        type(scene, 1, 1, QStringLiteral("Smith"));
        scene.grid->setCursorCell(2, 1);
        const int steps = scene.doc->undoStack()->count();
        trigger(scene, ActionId::kCalcFlashFill); // the title row is read as headers
        QVERIFY2(cell(scene, 0, 2, 1) == Value(QStringLiteral("Lee")), qPrintable(scene.host.statusMessage()));
        QCOMPARE(cell(scene, 0, 4, 1), Value(QStringLiteral("Petersen")));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        QVERIFY2(scene.host.statusMessage().contains(QStringLiteral("Flash Fill")), qPrintable(scene.host.statusMessage()));
        // Arithmetic is beyond it, and it says so.
        type(scene, 0, 3, QStringLiteral("2"));
        type(scene, 0, 4, QStringLiteral("4"));
        type(scene, 1, 3, QStringLiteral("3"));
        scene.grid->setCursorCell(1, 4);
        trigger(scene, ActionId::kCalcFlashFill);
        QVERIFY(QindaOffice::Calc::isBlank(cell(scene, 0, 1, 4)));
        QVERIFY2(scene.host.statusMessage().contains(QStringLiteral("pattern")), qPrintable(scene.host.statusMessage()));
    }

    void consolidateAcrossSheets()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.doc->insertSheet(QStringLiteral("East"));
        scene.doc->insertSheet(QStringLiteral("West"));
        for (const int sheet : {1, 2}) {
            scene.host.setActiveSheet(sheet);
            type(scene, 0, 0, QStringLiteral("Apples"));
            type(scene, 0, 1, QString::number(sheet * 10));
            type(scene, 1, 0, sheet == 1 ? QStringLiteral("Pears") : QStringLiteral("Plums"));
            type(scene, 1, 1, QStringLiteral("5"));
        }
        scene.host.setActiveSheet(0);
        scene.grid->setCursorCell(0, 0);
        trigger(scene, ActionId::kCalcConsolidate);
        QVERIFY(dialogOpened(scene, "consolidateDialog"));
        QObject *dialog = popup(scene, "consolidateDialog");
        dialog->setProperty("references", QVariantList{QStringLiteral("East!$A$1:$B$2"), QStringLiteral("West!$A$1:$B$2")});
        scene.item(QStringLiteral("consolidateLeftColumn"))->setProperty("checked", true);
        grab(scene, QStringLiteral("qindacalc_m78_consolidate_1280"));
        QVERIFY(answer(scene, "consolidateDialog", true));
        QCOMPARE(cell(scene, 0, 0, 0), Value(QStringLiteral("Apples")));
        QCOMPARE(cell(scene, 0, 0, 1), Value(30.0));
        QCOMPARE(cell(scene, 0, 1, 0), Value(QStringLiteral("Pears")));
        QCOMPARE(cell(scene, 0, 2, 0), Value(QStringLiteral("Plums")));
        QCOMPARE(cell(scene, 0, 2, 1), Value(5.0));
    }

    void circleInvalidData()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        for (int r = 0; r < 4; ++r) {
            type(scene, r, 0, QString::number((r + 1) * 4)); // 4, 8, 12, 16
        }
        scene.grid->setCursorCell(0, 0);
        scene.grid->setCursorCell(3, 0, true);
        scene.host.setValidationRule({{QStringLiteral("kind"), 0}, {QStringLiteral("operand1"), QStringLiteral("1")},
                                      {QStringLiteral("operand2"), QStringLiteral("10")}});
        trigger(scene, ActionId::kCalcCircleInvalid);
        QCOMPARE(scene.grid->invalidCells(), (QList<QindaOffice::Calc::CellAddr>{{2, 0}, {3, 0}}));
        grab(scene, QStringLiteral("qindacalc_m78_circle_invalid_1280"));
        // A corrected value loses its ring.
        scene.grid->setCursorCell(2, 0);
        scene.host.commitFormulaBar(QStringLiteral("9"));
        QCOMPARE(scene.grid->invalidCells(), (QList<QindaOffice::Calc::CellAddr>{{3, 0}}));
        trigger(scene, ActionId::kCalcClearCircles);
        QVERIFY(scene.grid->invalidCells().isEmpty());
    }

    void dialogsSqueezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, 0, 0, QStringLiteral("a,b"));
        scene.grid->setCursorCell(0, 0);
        for (const auto &[id, name] : {std::pair{ActionId::kCalcTextToColumns, "textToColumnsDialog"},
                                       std::pair{ActionId::kCalcRemoveDuplicates, "removeDuplicatesDialog"},
                                       std::pair{ActionId::kCalcConsolidate, "consolidateDialog"}}) {
            trigger(scene, id);
            QVERIFY(dialogOpened(scene, name));
            grab(scene, QStringLiteral("qindacalc_m78_%1_520").arg(QString::fromLatin1(name)));
            QVERIFY(answer(scene, name, false));
        }
    }
};

CALC_TEST_MAIN(CalcDataToolsUiTest)

#include "tst_calc_datatools_ui.moc"
