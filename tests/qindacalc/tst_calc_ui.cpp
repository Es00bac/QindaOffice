// SPDX-License-Identifier: GPL-3.0-or-later
// QindaCalc through the real window: named strips, undo/redo that
// recalculates, the clipboard (rich and TSV), formatting through the
// action entries, find/replace with wrap, the cursor outline on the cursor
// cell, the strip wrapping instead of hiding, the D-015 grid commands and
// the window's structural fixtures.

#include "calc_test_support.h"

#include "qindacalc/src/calc_clipboard.h"
#include "support/quick_dump.h"

#include "standard_actions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QUndoStack>

using namespace CalcTest;
using namespace QindaOffice::Apps::QindaCalc;
namespace ActionId = QindaOffice::Quick::ActionId;
namespace Calc = QindaOffice::Calc;

class CalcUiTest final : public QObject {
    Q_OBJECT

    static QRect pixelBoundsOf(const QImage &image, const QColor &colour)
    {
        const QRgb want = colour.rgb() | 0xff000000u;
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if ((image.pixel(x, y) | 0xff000000u) != want) {
                    continue;
                }
                left = qMin(left, x);
                top = qMin(top, y);
                right = qMax(right, x);
                bottom = qMax(bottom, y);
            }
        }
        return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
    }

private slots:
    void toolbarsExposeNamedCommands()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(100);
        QQuickItem *mainBar = scene.item(QStringLiteral("mainBar"));
        QQuickItem *formatBar = scene.item(QStringLiteral("formatBar"));
        QVERIFY(mainBar != nullptr && formatBar != nullptr);
        for (const char *id : {ActionId::kFileSave, ActionId::kEditUndo, ActionId::kEditCopy,
                               ActionId::kEditPaste, ActionId::kEditFind}) {
            QVERIFY2(findItem(mainBar, QStringLiteral("toolButton_") + QString::fromLatin1(id)) != nullptr, id);
        }
        QVERIFY(findItem(formatBar, QStringLiteral("toolButton_formatBoldAction")) != nullptr);
        QVERIFY(findItem(formatBar, QStringLiteral("cellNumberFormatCombo")) != nullptr);
        QVERIFY(findItem(formatBar, QStringLiteral("cellFontFamilyCombo")) != nullptr);
        // Fresh document: nothing to undo yet.
        QVERIFY(!scene.host.actions()->entry(QString::fromLatin1(ActionId::kEditUndo))->isEnabled());
    }

    void undoRedoRestoresValuesAndRecalculates()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("41"));
        doc->applyInput({0, 1}, QStringLiteral("=A1*2"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("82"));
        doc->applyInput({0, 0}, QStringLiteral("1"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("2"));
        QVERIFY(scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditUndo)));
        // The undo restore rebuilds the dependency index: the formula
        // re-evaluates against the restored A1.
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 0)), QStringLiteral("41"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("82"));
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kEditRedo))->isEnabled());
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditRedo));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 1)), QStringLiteral("2"));
    }

    void clipboardKeepsFormulasAndParsesText()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("7"));
        doc->applyInput({1, 0}, QStringLiteral("=A1*3"));
        scene.grid->setCursorCell(0, 0);
        scene.grid->setCursorCell(1, 0, true);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditCopy));
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasFormat(QString::fromLatin1(richCellsMimeType())));
        scene.grid->setCursorCell(0, 3);
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kEditPaste))->isEnabled());
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditPaste));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 3)), QStringLiteral("7"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 1, 3)), QStringLiteral("21"));
        // Plain TSV from another app.
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("1.5\t42\thello\n=1+2"));
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(pasteClipboardAt(doc, {2, 1}));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 2, 3)), QStringLiteral("hello"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 3, 1)), QStringLiteral("3"));
        // Cut clears the source in one step.
        scene.grid->setCursorCell(0, 0);
        const int before = doc->undoStack()->count();
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditCut));
        QVERIFY(Calc::isBlank(doc->workbook()->cellValue(0, 0, 0)));
        QCOMPARE(doc->undoStack()->count(), before + 1);
    }

    void formattingPatchesStylesUndoably()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("3.14159"));
        scene.grid->setCursorCell(0, 0);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kFormatBold));
        const Calc::CellStyle *style = doc->workbook()->style(doc->workbook()->sheetAt(0).styleIdAt({0, 0}));
        QVERIFY(style != nullptr && style->bold);
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kFormatBold))->isChecked());
        scene.host.applyNumberFormat(2); // #,##0.00
        QCOMPARE(doc->displayTextAt({0, 0}), QStringLiteral("3.14"));
        QCOMPARE(scene.host.cellNumberFormatIndex(), 2);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kFormatAlignCenter));
        QCOMPARE(doc->workbook()->style(doc->workbook()->sheetAt(0).styleIdAt({0, 0}))->alignment, 1);
        scene.host.applyFontSize(QStringLiteral("14"));
        QCOMPARE(scene.host.cellFontSize(), QStringLiteral("14"));
        scene.host.applyTextColor(QColor(200, 30, 40));
        QVERIFY(doc->workbook()->style(doc->workbook()->sheetAt(0).styleIdAt({0, 0}))->fg.has_value());
        // Five undo steps back to no style.
        for (int i = 0; i < 5; ++i) {
            doc->undoStack()->undo();
        }
        QCOMPARE(doc->workbook()->sheetAt(0).styleIdAt({0, 0}), 0);
        QVERIFY(!scene.host.actions()->entry(QString::fromLatin1(ActionId::kFormatBold))->isChecked());
    }

    void findBarSelectsMatchesWithWrapAndReplaces()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("alpha"));
        doc->applyInput({4, 1}, QStringLiteral("beta alpha"));
        doc->applyInput({9, 2}, QStringLiteral("ALPHA"));
        scene.grid->setCursorCell(0, 0);
        QQuickItem *bar = scene.item(QStringLiteral("findBar"));
        QVERIFY(bar != nullptr && !bar->isVisible());
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditFind));
        QTRY_VERIFY(bar->isVisible());
        QVERIFY(scene.host.findNext(QStringLiteral("alpha"), false));
        QCOMPARE(scene.grid->cursorRow(), 4);
        QCOMPARE(scene.grid->cursorCol(), 1);
        scene.host.findNext(QStringLiteral("alpha"), false); // case-insensitive → row 9
        QCOMPARE(scene.grid->cursorRow(), 9);
        scene.host.findNext(QStringLiteral("alpha"), false); // wraps to the start
        QCOMPARE(scene.grid->cursorRow(), 0);
        scene.host.findPrevious(QStringLiteral("alpha"), false); // backwards wraps to the end
        QCOMPARE(scene.grid->cursorRow(), 9);
        // Ctrl+H reveals the replace row; one sweep is one undo step.
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditReplace));
        QTRY_VERIFY(scene.item(QStringLiteral("replaceRow"))->isVisible());
        const int stepsBefore = doc->undoStack()->count();
        QCOMPARE(scene.host.replaceAll(QStringLiteral("alpha"), QStringLiteral("beta"), false), 3);
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("beta"));
        QCOMPARE(doc->inputTextAt({4, 1}), QStringLiteral("beta beta"));
        QCOMPARE(doc->undoStack()->count(), stepsBefore + 1);
        doc->undoStack()->undo();
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("alpha"));
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kEditFind));
        QTRY_VERIFY(!scene.item(QStringLiteral("replaceRow"))->isVisible());
    }

    // The grid is painted inside p.translate(kHeaderWidth, kHeaderHeight):
    // the cursor outline must land on the cursor cell, not one header away.
    void cursorOutlineLandsOnTheCursorCell()
    {
        Scene scene(QSize(900, 600));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.grid->setCursorCell(0, 0);
        QTest::qWait(100);
        const QImage shot = scene.window->grabWindow();
        QVERIFY(!shot.isNull());
        const QPointF origin = scene.grid->mapToScene(QPointF(0, 0));
        const QColor outline = scene.grid->cursorColor();
        const QRect gridRect(origin.toPoint(), QSize(int(scene.grid->width()), int(scene.grid->height())));
        const QRect painted = pixelBoundsOf(shot.copy(gridRect), outline);
        QVERIFY2(painted.isValid(), "the cursor outline was not painted at all");
        const QRectF expected = scene.grid->cursorItemRect();
        QVERIFY2(qAbs(painted.left() - expected.left()) <= 2 && qAbs(painted.top() - expected.top()) <= 2,
                 qPrintable(QStringLiteral("outline at %1,%2 but A1 is at %3,%4")
                                .arg(painted.left()).arg(painted.top()).arg(expected.left()).arg(expected.top())));
    }

    void formatStripWrapsInsteadOfHidingCommands()
    {
        Scene scene(QSize(1200, 600));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(100);
        QQuickItem *bar = scene.item(QStringLiteral("formatBar"));
        QVERIFY(bar != nullptr);
        const QStringList wanted = {QStringLiteral("toolButton_formatBoldAction"),
                                    QStringLiteral("toolButton_formatItalicAction"),
                                    QStringLiteral("toolButton_formatUnderlineAction"),
                                    QStringLiteral("toolButton_formatAlignLeftAction"),
                                    QStringLiteral("toolButton_formatAlignCenterAction"),
                                    QStringLiteral("toolButton_formatAlignRightAction")};
        const qreal wideHeight = bar->height();
        scene.window->resize(560, 600);
        QTRY_VERIFY(bar->height() > wideHeight);
        for (const QString &id : wanted) {
            QQuickItem *button = findItem(bar, id);
            QVERIFY2(button != nullptr && button->isVisible(), qPrintable(id));
            const QRectF rect = button->mapRectToItem(bar, QRectF(0, 0, button->width(), button->height()));
            QVERIFY2(rect.right() <= bar->width() + 0.5, qPrintable(id));
        }
    }

    void gridStartsOnA1WithATrueStatusLine()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QCOMPARE(scene.grid->cursorRow(), 0);
        QCOMPARE(scene.grid->cursorCol(), 0);
        QVERIFY2(scene.host.statusInfo().startsWith(QStringLiteral("A1")), qPrintable(scene.host.statusInfo()));
        QVERIFY(!scene.host.statusInfo().contains(QStringLiteral("Count")));
        scene.doc->applyInput({0, 0}, QStringLiteral("2"));
        scene.doc->applyInput({1, 0}, QStringLiteral("3"));
        scene.grid->setCursorCell(0, 0);
        scene.grid->setCursorCell(1, 0, true);
        const QString info = scene.host.statusInfo();
        QVERIFY2(info.contains(QStringLiteral("A1:A2")), qPrintable(info));
        QVERIFY2(info.contains(QStringLiteral("Sum: 5")), qPrintable(info));
        QVERIFY2(info.contains(QStringLiteral("Count: 2")), qPrintable(info));
        QCOMPARE(scene.host.cursorText(), QStringLiteral("A2"));
    }

    void deleteKeyClearsAndTypingEdits()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("keep me"));
        scene.grid->setCursorCell(0, 0);
        scene.grid->forceActiveFocus();
        QTRY_VERIFY(scene.grid->hasActiveFocus());
        QTest::keyClick(scene.window, Qt::Key_Delete);
        QVERIFY(doc->inputTextAt({0, 0}).isEmpty());
        doc->undoStack()->undo();
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("keep me"));
        // A printable key opens the editor with that character; Return
        // commits and moves down.
        scene.grid->setCursorCell(1, 1);
        QTest::keyClick(scene.window, Qt::Key_4);
        QTRY_VERIFY(scene.grid->isEditing());
        QQuickItem *editor = scene.item(QStringLiteral("cellEditor"));
        QVERIFY(editor != nullptr);
        QTRY_VERIFY(editor->hasActiveFocus());
        QCOMPARE(editor->property("text").toString(), QStringLiteral("4"));
        QTest::keyClick(scene.window, Qt::Key_2);
        QTest::keyClick(scene.window, Qt::Key_Return);
        QTRY_VERIFY(!scene.grid->isEditing());
        QCOMPARE(doc->inputTextAt({1, 1}), QStringLiteral("42"));
        QCOMPARE(scene.grid->cursorRow(), 2);
        // Escape cancels; the formula bar commits.
        QTest::keyClick(scene.window, Qt::Key_F2);
        QTRY_VERIFY(scene.grid->isEditing());
        QTest::keyClick(scene.window, Qt::Key_Escape);
        QTRY_VERIFY(!scene.grid->isEditing());
        scene.host.commitFormulaBar(QStringLiteral("=B2*2"));
        QCOMPARE(doc->displayTextAt({2, 1}), QStringLiteral("84"));
        QCOMPARE(scene.host.formulaText(), QStringLiteral("=B2*2"));
    }

    void headerClickSelectsTheWholeColumn()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.doc->applyInput({0, 1}, QStringLiteral("1"));
        scene.doc->applyInput({4, 1}, QStringLiteral("2"));
        scene.grid->selectColumn(1, false);
        const Calc::CellRange range = scene.grid->selectionRange();
        QCOMPARE(range.topLeft.col, 1);
        QCOMPARE(range.bottomRight.col, 1);
        QCOMPARE(range.topLeft.row, 0);
        QCOMPARE(range.bottomRight.row, 4); // bounded by the USED range
        QCOMPARE(scene.host.statusInfo().left(5), QStringLiteral("B1:B5"));
    }

    void structuralCommandsAreOneUndoStepEach()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("10"));
        doc->applyInput({1, 0}, QStringLiteral("=A1*2"));
        const int before = doc->undoStack()->count();
        scene.grid->setCursorCell(0, 0);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcInsertRows));
        QCOMPARE(doc->undoStack()->count(), before + 1);
        QCOMPARE(doc->inputTextAt({2, 0}), QStringLiteral("=A2*2"));
        doc->undoStack()->undo();
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcDeleteRows));
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("=#REF!*2"));
        doc->undoStack()->undo();
        // Sort keeps the header row; AutoSum totals the run above.
        doc->applyInput({0, 2}, QStringLiteral("Score"));
        doc->applyInput({1, 2}, QStringLiteral("30"));
        doc->applyInput({2, 2}, QStringLiteral("10"));
        doc->applyInput({3, 2}, QStringLiteral("20"));
        scene.grid->setCursorCell(1, 2);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcSortAscending));
        QCOMPARE(doc->displayTextAt({0, 2}), QStringLiteral("Score"));
        QCOMPARE(doc->displayTextAt({1, 2}), QStringLiteral("10"));
        scene.grid->setCursorCell(4, 2);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcAutoSum));
        QCOMPARE(doc->inputTextAt({4, 2}), QStringLiteral("=SUM(C2:C4)"));
        QCOMPARE(doc->displayTextAt({4, 2}), QStringLiteral("60"));
        // Merge from a selection, unmerge from any covered cell.
        scene.grid->setCursorCell(6, 0);
        scene.grid->setCursorCell(7, 2, true);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcMergeCells));
        QCOMPARE(doc->workbook()->sheetAt(0).merges().size(), 1);
        scene.grid->setCursorCell(7, 1);
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcUnmergeCells))->isEnabled());
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcUnmergeCells));
        QCOMPARE(doc->workbook()->sheetAt(0).merges().size(), 0);
        // Row height through the dialog's answer.
        scene.grid->setCursorCell(0, 0);
        scene.host.applyRowHeight(40);
        QCOMPARE(doc->workbook()->sheetAt(0).rowHeight(0), 40.0);
    }

    void sheetsRenameMoveAndDeleteThroughTheHost()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("7"));
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kInsertSheet));
        QCOMPARE(scene.host.sheetNames().size(), 2);
        scene.host.setActiveSheet(1);
        QCOMPARE(scene.host.activeSheet(), 1);
        doc->applyInput({0, 0}, QStringLiteral("=Sheet1!A1+1"));
        QCOMPARE(doc->displayTextAt(1, {0, 0}), QStringLiteral("8"));
        scene.host.renameSheet(0, QStringLiteral("Data"));
        QCOMPARE(scene.host.sheetNames().first(), QStringLiteral("Data"));
        QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("=Data!A1+1"));
        // A duplicate name is refused with a status line, not silently.
        scene.host.renameSheet(1, QStringLiteral("Data"));
        QCOMPARE(scene.host.sheetNames().at(1), QStringLiteral("Sheet2"));
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("already exists")));
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcSheetMoveLeft));
        QCOMPARE(scene.host.sheetNames().first(), QStringLiteral("Sheet2"));
        QSignalSpy ask(&scene.host, &CalcHost::deleteSheetRequested);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcSheetDelete));
        QCOMPARE(ask.count(), 1);
        scene.host.deleteSheetConfirmed(scene.host.activeSheet());
        QCOMPARE(scene.host.sheetNames().size(), 1);
        QVERIFY(!scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcSheetDelete))->isEnabled());
    }

    void freezePanesKeepsTheFirstRowVisible()
    {
        Scene scene(QSize(600, 400));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.grid->setCursorCell(1, 0);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcFreezePanes));
        QCOMPARE(scene.grid->frozenRow(), 1);
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcFreezePanes))->isChecked());
        // Scrolling far down leaves row 1 where it is.
        scene.grid->setScrollY(scene.grid->scrollHeight());
        scene.grid->setCursorCell(0, 0);
        QCOMPARE(scene.grid->cursorRow(), 0);
        QVERIFY(scene.grid->cursorItemRect().top() < 40);
    }

    void windowMatchesItsFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 760));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QQuickItem *dock = scene.item(QStringLiteral("calcDock"));
            QVERIFY(dock != nullptr);
            QVERIFY(QMetaObject::invokeMethod(dock, "resetLayout"));
            QTest::qWait(150);
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindacalc_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

CALC_TEST_MAIN(CalcUiTest)

#include "tst_calc_ui.moc"
