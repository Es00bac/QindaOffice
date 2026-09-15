// SPDX-License-Identifier: GPL-3.0-or-later
// M12 Calc editing-depth acceptance: undo/redo restores cell values AND
// rebuilds the dependency graph (formulas re-evaluate after undo);
// clipboard copy/cut/paste keeps formulas exact inside the app and accepts
// plain TSV; the formatting toolbar patches cell styles with one undo step;
// the find bar locates and selects matches with wrap-around.

#include "qindacalc/src/calc_clipboard.h"
#include "qindacalc/src/calc_find_bar.h"
#include "qindacalc/src/calc_format_controller.h"
#include "qindacalc/src/calc_window.h"
#include "qindacalc/src/spreadsheet_document.h"
#include "qindacalc/src/spreadsheet_view.h"

#include "standard_actions.h"

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QToolBar>
#include <QtTest>

using namespace QindaOffice::Apps::QindaCalc;

class CalcUiTest : public QObject {
    Q_OBJECT
private slots:
    void toolbarsExposeNamedActions();
    void undoRedoRestoresValuesAndRecalculates();
    void richClipboardKeepsFormulas();
    void textClipboardParsesValues();
    void cutClearsSourceInOneStep();
    void formattingPatchesStylesUndoably();
    void findBarSelectsMatchesWithWrap();
};

namespace ActionId = ::QindaOffice::Shell::ActionId;

void CalcUiTest::toolbarsExposeNamedActions()
{
    CalcWindow window;
    QToolBar *mainBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QToolBar *formatBar = window.findChild<QToolBar *>(QStringLiteral("formatToolBar"));
    QVERIFY(mainBar != nullptr);
    QVERIFY(formatBar != nullptr);
    for (const char *id : {ActionId::kFileSave, ActionId::kEditUndo, ActionId::kEditCopy,
                           ActionId::kEditPaste, ActionId::kEditFind}) {
        QAction *action = window.action(QString::fromLatin1(id));
        QVERIFY2(action != nullptr, id);
        QVERIFY2(mainBar->actions().contains(action), id);
    }
    const CalcFormatActions &format = window.formatController()->actions();
    for (QAction *action : format.all()) {
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
    }
    QVERIFY(formatBar->actions().contains(format.bold));
    QVERIFY(formatBar->findChild<QComboBox *>(QStringLiteral("cellNumberFormatCombo"))
            != nullptr);
    // Fresh document: nothing to undo yet, pasteboard may be anything.
    QVERIFY(!window.action(QString::fromLatin1(ActionId::kEditUndo))->isEnabled());
}

void CalcUiTest::undoRedoRestoresValuesAndRecalculates()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("41"));
    doc->applyInput({0, 1}, QStringLiteral("=A1*2"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("82"));

    doc->applyInput({0, 0}, QStringLiteral("1"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("2"));

    QAction *undo = window.action(QString::fromLatin1(ActionId::kEditUndo));
    QAction *redo = window.action(QString::fromLatin1(ActionId::kEditRedo));
    QVERIFY(undo->isEnabled());
    undo->trigger();
    // The undo restore must rebuild the dependency index, not just the
    // stored values - the formula re-evaluates against restored A1.
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 0)), QStringLiteral("41"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("82"));
    QVERIFY(redo->isEnabled());
    redo->trigger();
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 0)), QStringLiteral("1"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("2"));
}

void CalcUiTest::richClipboardKeepsFormulas()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("7"));
    doc->applyInput({1, 0}, QStringLiteral("=A1*3"));
    SpreadsheetView *view = window.spreadsheetView();
    view->setCursorCell({0, 0});
    view->setCursorCell({1, 0}, /*extendSelection=*/true);

    copyRangeToClipboard(doc, view->selectionRange());
    QVERIFY(QGuiApplication::clipboard()->mimeData()->hasFormat(
        QString::fromLatin1(richCellsMimeType())));

    pasteClipboardAt(doc, {0, 3});
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 3)), QStringLiteral("7"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 1, 3)), QStringLiteral("21"));
}

void CalcUiTest::textClipboardParsesValues()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    QMimeData *mime = new QMimeData;
    mime->setText(QStringLiteral("1.5\t42\thello\n=1+2"));
    QGuiApplication::clipboard()->setMimeData(mime);

    QVERIFY(pasteClipboardAt(doc, {2, 1}));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 2, 1)), QStringLiteral("1.5"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 2, 2)), QStringLiteral("42"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 2, 3)), QStringLiteral("hello"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 3, 1)), QStringLiteral("3"));
}

void CalcUiTest::cutClearsSourceInOneStep()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("99"));
    SpreadsheetView *view = window.spreadsheetView();
    view->setCursorCell({0, 0});

    cutRangeToClipboard(doc, view->selectionRange());
    QVERIFY(CalcEng::isBlank(doc->workbook()->cellValue(0, 0, 0)));
    QVERIFY(pasteClipboardAt(doc, {2, 2}));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 2, 2)), QStringLiteral("99"));

    // Cut + paste were two undo steps; undoing the paste then the cut
    // restores the original cell exactly once.
    doc->undoStack()->undo();
    doc->undoStack()->undo();
    QCOMPARE(toText(doc->workbook()->cellValue(0, 0, 0)), QStringLiteral("99"));
}

void CalcUiTest::formattingPatchesStylesUndoably()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("3.14159"));
    CalcFormatController *controller = window.formatController();
    SpreadsheetView *view = window.spreadsheetView();
    view->setCursorCell({0, 0});

    controller->actions().bold->trigger();
    const qint32 id = doc->workbook()->sheetAt(0).styleIdAt({0, 0});
    const CalcEng::CellStyle *style = doc->workbook()->style(id);
    QVERIFY(style != nullptr);
    QVERIFY(style->bold);
    QVERIFY(controller->actions().bold->isChecked());

    // Number format through the controller's patch seam.
    StylePatch patch;
    patch.numberFormat = QStringLiteral("#,##0.00");
    doc->restyleRange(view->selectionRange(), patch, QStringLiteral("Number format"));
    const qint32 numberId = doc->workbook()->sheetAt(0).styleIdAt({0, 0});
    const CalcEng::CellStyle *after = doc->workbook()->style(numberId);
    QVERIFY(after != nullptr);
    QCOMPARE(after->numberFormat, QStringLiteral("#,##0.00"));
    QCOMPARE(doc->displayTextAt({0, 0}), QStringLiteral("3.14"));

    // Alignment action writes through the same patch seam.
    controller->actions().alignCenter->trigger();
    const qint32 alignId = doc->workbook()->sheetAt(0).styleIdAt({0, 0});
    const CalcEng::CellStyle *aligned = doc->workbook()->style(alignId);
    QCOMPARE(aligned->alignment, 1);

    // Bold, number format and alignment were three undo steps.
    doc->undoStack()->undo();
    doc->undoStack()->undo();
    doc->undoStack()->undo();
    // AGENT-NOTE: undo move-assigns the workbook, so Sheet/CellStyle
    // references must never outlive an undo step - re-fetch from the
    // document (same rule as real window code).
    QCOMPARE(doc->workbook()->sheetAt(0).styleIdAt({0, 0}), 0);
}

void CalcUiTest::findBarSelectsMatchesWithWrap()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("alpha"));
    doc->applyInput({4, 1}, QStringLiteral("beta alpha"));
    doc->applyInput({9, 2}, QStringLiteral("ALPHA"));
    SpreadsheetView *view = window.spreadsheetView();
    view->setCursorCell({0, 0});

    CalcFindBar *bar = window.findBar();
    QVERIFY(bar->isHidden());
    window.action(QString::fromLatin1(ActionId::kEditFind))->trigger();
    QVERIFY(!bar->isHidden());
    auto *edit = bar->findChild<QLineEdit *>(QStringLiteral("calcFindEdit"));
    QVERIFY(edit != nullptr);
    edit->setText(QStringLiteral("alpha"));

    bar->findNext();
    QCOMPARE(view->cursorCell().row, 4);
    QCOMPARE(view->cursorCell().col, 1);
    bar->findNext(); // wraps from row 4 to row 9 (case-insensitive)
    QCOMPARE(view->cursorCell().row, 9);
    bar->findNext(); // wraps to the start
    QCOMPARE(view->cursorCell().row, 0);
    bar->findPrevious(); // backwards wraps to the end
    QCOMPARE(view->cursorCell().row, 9);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaCalc"));
    CalcUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_ui.moc"
