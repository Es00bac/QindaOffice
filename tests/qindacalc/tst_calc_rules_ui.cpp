// SPDX-License-Identifier: GPL-3.0-or-later
// QindaCalc's rule commands driven through the real window: data
// validation refusing input out loud, conditional formatting as one undo
// step (through the dialog's map seam), a filter actually removing rows
// from the grid, and comments surviving both undo and the recovery payload.

#include "calc_test_support.h"

#include "sheet.h"
#include "sheet_rules.h"

#include <QSignalSpy>
#include <QUndoStack>

using namespace CalcTest;
using namespace QindaOffice::Apps::QindaCalc;
namespace Calc = QindaOffice::Calc;

class CalcRulesUiTest final : public QObject {
    Q_OBJECT
private slots:
    void validationRefusesInputAndSaysSo()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        // Through the dialog's seam: whole numbers 1..10 on A1.
        scene.grid->setCursorCell(0, 0);
        scene.host.setValidationRule({{QStringLiteral("kind"), 0}, {QStringLiteral("min"), 1.0},
                                      {QStringLiteral("max"), 10.0}});
        QCOMPARE(scene.host.validationAtCursor().value(QStringLiteral("kind")).toInt(), 0);
        QSignalSpy rejected(doc, &SpreadsheetDocument::inputRejected);
        QSignalSpy status(&scene.host, &CalcHost::statusMessageChanged);
        doc->applyInput({0, 0}, QStringLiteral("42"));
        // The keystroke must not be swallowed in silence: the value is
        // refused AND the window is told why.
        QVERIFY(doc->inputTextAt({0, 0}).isEmpty());
        QCOMPARE(rejected.count(), 1);
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("A1")));
        doc->applyInput({0, 0}, QStringLiteral("7"));
        QCOMPARE(doc->displayTextAt({0, 0}), QStringLiteral("7"));
        // Clearing is never refused.
        doc->applyInput({0, 0}, QString());
        QVERIFY(doc->inputTextAt({0, 0}).isEmpty());
        // "Any value" removes the rule.
        scene.host.setValidationRule({{QStringLiteral("kind"), -1}});
        QCOMPARE(scene.host.validationAtCursor().value(QStringLiteral("kind")).toInt(), -1);
    }

    void conditionalFormattingIsOneUndoStep()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("50"));
        const int before = doc->undoStack()->count();
        // The dialog hands the whole list back: two rules added in one visit
        // are ONE undo step — which is what the user did.
        scene.host.setCondFormatRules(
            {QVariantMap{{QStringLiteral("range"), QStringLiteral("A1:A10")}, {QStringLiteral("op"), 0},
                         {QStringLiteral("first"), 10.0}, {QStringLiteral("background"), QStringLiteral("#00ff00")}},
             QVariantMap{{QStringLiteral("range"), QStringLiteral("A1:A10")}, {QStringLiteral("op"), 0},
                         {QStringLiteral("first"), 100.0}, {QStringLiteral("bold"), true}}});
        QCOMPARE(doc->undoStack()->count(), before + 1);
        QCOMPARE(doc->workbook()->sheetAt(0).condFormats().size(), 2);
        QVERIFY(doc->workbook()->sheetAt(0).condFormatFor({0, 0}, Calc::Value(50.0)).background.has_value());
        const QVariantList back = scene.host.condFormatRules();
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.first().toMap().value(QStringLiteral("range")).toString(), QStringLiteral("A1:A10"));
        QCOMPARE(back.first().toMap().value(QStringLiteral("background")).toString(), QStringLiteral("#00ff00"));
        doc->undoStack()->undo();
        QVERIFY(doc->workbook()->sheetAt(0).condFormats().isEmpty());
    }

    void filteredRowsDisappearFromTheGrid()
    {
        Scene scene(QSize(700, 480));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("Status"));
        doc->applyInput({1, 0}, QStringLiteral("Open"));
        doc->applyInput({2, 0}, QStringLiteral("Done"));
        doc->applyInput({3, 0}, QStringLiteral("Open"));
        // Data ▸ AutoFilter over the block, then the dropdown keeps "Open".
        scene.grid->setCursorCell(0, 0);
        scene.host.actions()->trigger(QStringLiteral("calcAutoFilterAction"));
        QVERIFY(doc->workbook()->sheetAt(0).autoFilter().active);
        QSignalSpy menu(&scene.host, &CalcHost::filterMenuRequested);
        scene.host.applyFilterValues(0, {QStringLiteral("Open")});
        // A hidden row has no band at all, so the row BELOW it moves up into
        // its place — which is also what keeps clicks landing on the row
        // the user can see.
        QCOMPARE(scene.grid->rowTopForTest(2), scene.grid->rowTopForTest(3));
        QVERIFY(scene.grid->rowTopForTest(1) < scene.grid->rowTopForTest(3));
        // Everything allowed again clears the column's filter.
        scene.host.applyFilterValues(0, {QStringLiteral("Open"), QStringLiteral("Done")});
        QVERIFY(!doc->workbook()->sheetAt(0).autoFilter().allowed.contains(0));
    }

    void commentsSurviveUndoAndRecovery()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        scene.grid->setCursorCell(2, 1);
        scene.host.setCommentText(QStringLiteral("verify this"));
        QVERIFY(doc->workbook()->sheetAt(0).commentAt({2, 1}) != nullptr);
        QCOMPARE(scene.host.commentAtCursor(), QStringLiteral("verify this"));
        QVERIFY(scene.host.actions()->entry(QStringLiteral("calcDeleteCommentAction"))->isEnabled());
        const QByteArray payload = doc->recoverySnapshot();
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        const Calc::CellComment *back = restored.workbook()->sheetAt(0).commentAt({2, 1});
        QVERIFY(back != nullptr);
        QCOMPARE(back->text, QStringLiteral("verify this"));
        doc->undoStack()->undo();
        QVERIFY(doc->workbook()->sheetAt(0).commentAt({2, 1}) == nullptr);
    }
};

CALC_TEST_MAIN(CalcRulesUiTest)

#include "tst_calc_rules_ui.moc"
