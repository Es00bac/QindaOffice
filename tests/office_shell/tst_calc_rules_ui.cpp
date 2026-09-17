// SPDX-License-Identifier: GPL-3.0-or-later
// QindaCalc's rule commands driven through the real window: data
// validation refusing input out loud, conditional formatting as one undo
// step, a filter actually removing rows from the grid, and comments
// surviving both undo and the crash-recovery payload. Split from
// tst_calc_ui.cpp to keep both inside the line budget.

#include "qindacalc/src/calc_rules_actions.h"
#include "qindacalc/src/calc_window.h"
#include "qindacalc/src/spreadsheet_document.h"
#include "qindacalc/src/spreadsheet_view.h"

#include "sheet.h"
#include "sheet_rules.h"
#include "standard_actions.h"

#include <QApplication>
#include <QSignalSpy>
#include <QUndoStack>
#include <QtTest>

using namespace QindaOffice::Apps::QindaCalc;

class CalcRulesUiTest : public QObject {
    Q_OBJECT
private slots:
    void validationRefusesInputAndSaysSo();
    void conditionalFormattingIsOneUndoStep();
    void filteredRowsDisappearFromTheGrid();
    void commentsSurviveUndoAndRecovery();
};

void CalcRulesUiTest::validationRefusesInputAndSaysSo()
{
    CalcWindow window;
    window.show();
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    QindaOffice::Calc::ValidationRule rule;
    rule.kind = QindaOffice::Calc::ValidationRule::Kind::WholeNumber;
    rule.range = {{0, 0}, {0, 0}};
    rule.min = 1;
    rule.max = 10;
    doc->setValidations({rule});
    QSignalSpy rejected(doc, &SpreadsheetDocument::inputRejected);

    doc->applyInput({0, 0}, QStringLiteral("42"));

    // The keystroke must not be swallowed in silence: the value is refused
    // AND the window is told why.
    QVERIFY(doc->inputTextAt({0, 0}).isEmpty());
    QCOMPARE(rejected.count(), 1);
    doc->applyInput({0, 0}, QStringLiteral("7"));
    QCOMPARE(doc->displayTextAt({0, 0}), QStringLiteral("7"));
    // Clearing is never refused, or a mistyped value could not be taken back.
    doc->applyInput({0, 0}, QString());
    QVERIFY(doc->inputTextAt({0, 0}).isEmpty());
}

void CalcRulesUiTest::conditionalFormattingIsOneUndoStep()
{
    CalcWindow window;
    window.show();
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("50"));
    const int before = doc->undoStack()->count();

    QindaOffice::Calc::CondFormatRule first;
    first.range = {{0, 0}, {9, 0}};
    first.op = QindaOffice::Calc::CondFormatRule::Op::GreaterThan;
    first.first = 10;
    first.format.background = QindaOffice::Calc::Rgb{0, 255, 0};
    QindaOffice::Calc::CondFormatRule second = first;
    second.first = 100;
    // The dialog hands the whole list back, so two rules added in one visit
    // are ONE undo step — which is what the user did.
    doc->setCondFormats({first, second});

    QCOMPARE(doc->undoStack()->count(), before + 1);
    QCOMPARE(doc->workbook()->sheetAt(0).condFormats().size(), 2);
    QVERIFY(doc->workbook()
                ->sheetAt(0)
                .condFormatFor({0, 0}, QindaOffice::Calc::Value(50.0))
                .background.has_value());
    doc->undoStack()->undo();
    QVERIFY(doc->workbook()->sheetAt(0).condFormats().isEmpty());
}

void CalcRulesUiTest::filteredRowsDisappearFromTheGrid()
{
    CalcWindow window;
    window.resize(700, 480);
    window.show();
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("Status"));
    doc->applyInput({1, 0}, QStringLiteral("Open"));
    doc->applyInput({2, 0}, QStringLiteral("Done"));
    doc->applyInput({3, 0}, QStringLiteral("Open"));

    QindaOffice::Calc::AutoFilter filter;
    filter.range = {{0, 0}, {3, 0}};
    filter.active = true;
    filter.allowed.insert(0, {QStringLiteral("Open")});
    doc->setAutoFilter(filter);
    window.spreadsheetView()->refreshAll();

    // A hidden row has no band at all, so the row BELOW it moves up into
    // its place — which is also what keeps clicks landing on the row the
    // user can see.
    SpreadsheetView *view = window.spreadsheetView();
    const int rowTwoTop = view->rowTopForTest(2);
    const int rowThreeTop = view->rowTopForTest(3);
    QCOMPARE(rowTwoTop, rowThreeTop);
}

void CalcRulesUiTest::commentsSurviveUndoAndRecovery()
{
    CalcWindow window;
    window.show();
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->setComment({2, 1}, QindaOffice::Calc::CellComment{QStringLiteral("jc"),
                                                           QStringLiteral("verify this")});
    QVERIFY(doc->workbook()->sheetAt(0).commentAt({2, 1}) != nullptr);

    // The recovery payload is the document's own state: a rule it does not
    // carry is a rule the user loses on recovery, silently.
    const QByteArray payload = doc->recoverySnapshot();
    SpreadsheetDocument restored;
    QString error;
    QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
    const QindaOffice::Calc::CellComment *back =
        restored.workbook()->sheetAt(0).commentAt({2, 1});
    QVERIFY(back != nullptr);
    QCOMPARE(back->text, QStringLiteral("verify this"));

    doc->undoStack()->undo();
    QVERIFY(doc->workbook()->sheetAt(0).commentAt({2, 1}) == nullptr);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaCalc"));
    CalcRulesUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_rules_ui.moc"
