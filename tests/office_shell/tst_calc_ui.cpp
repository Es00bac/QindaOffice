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
#include "wrapping_tool_bar.h"

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QImage>
#include <QLineEdit>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
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
    void cursorOutlineLandsOnTheCursorCell();
    void formatStripWrapsInsteadOfHidingCommands();
    void replaceEditsInputsAndUndoesInOneStep();
};

namespace ActionId = ::QindaOffice::Shell::ActionId;

// Bounding box of every pixel painted exactly `colour`, or a null rect.
static QRect pixelBoundsOf(const QImage &image, const QColor &colour)
{
    const QRgb want = colour.rgb();
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if ((image.pixel(x, y) | 0xff000000u) != (want | 0xff000000u)) {
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

void CalcUiTest::toolbarsExposeNamedActions()
{
    CalcWindow window;
    auto *mainBar = window.findChild<::QindaOffice::Shell::WrappingToolBar *>(QStringLiteral("mainToolBar"));
    auto *formatBar = window.findChild<::QindaOffice::Shell::WrappingToolBar *>(QStringLiteral("formatToolBar"));
    QVERIFY(mainBar != nullptr);
    QVERIFY(formatBar != nullptr);
    for (const char *id : {ActionId::kFileSave, ActionId::kEditUndo, ActionId::kEditCopy,
                           ActionId::kEditPaste, ActionId::kEditFind}) {
        QAction *action = window.action(QString::fromLatin1(id));
        QVERIFY2(action != nullptr, id);
        QVERIFY2(mainBar->stripActions().contains(action), id);
    }
    const CalcFormatActions &format = window.formatController()->actions();
    for (QAction *action : format.all()) {
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
    }
    QVERIFY(formatBar->stripActions().contains(format.bold));
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

// The grid is painted inside p.translate(kHeaderWidth, kHeaderHeight), so a
// rect that is ALSO .translated() by the header offset gets it twice. The
// cursor outline did exactly that: the selection fill sat on the cursor cell
// while its outline was drawn one header down and to the right.
void CalcUiTest::cursorOutlineLandsOnTheCursorCell()
{
    CalcWindow window;
    SpreadsheetView *view = window.spreadsheetView();
    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    view->setCursorCell({0, 0}); // A1, as the name box reports it
    QCoreApplication::processEvents();

    QImage shot(view->viewport()->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    view->viewport()->render(&shot);

    const QColor outline = view->palette().highlight().color().darker(140);
    const QRect painted = pixelBoundsOf(shot, outline);
    QVERIFY2(painted.isValid(), "the cursor outline was not painted at all");

    // The outline is a 2px pen stroked on the cell rect, so its pixel bounds
    // sit within one pen width of the cell's viewport rect.
    const QRect expected = view->cursorViewportRect();
    QVERIFY2(qAbs(painted.left() - expected.left()) <= 2
                 && qAbs(painted.top() - expected.top()) <= 2,
             qPrintable(QStringLiteral("outline at %1,%2 but cell A1 is at %3,%4")
                            .arg(painted.left())
                            .arg(painted.top())
                            .arg(expected.left())
                            .arg(expected.top())));
    // And specifically NOT one header offset away, which is what the bug did.
    QVERIFY(painted.left() < expected.left() + 40);
    QVERIFY(painted.top() < expected.top() + 20);
}

// A toolbar shows its commands. Plain QToolBar hid whatever did not fit
// behind the "»" extension chevron, so at ordinary window widths the
// formatting strip silently lost Align Left/Center/Right — enabled buttons
// the user could never see. The strip must wrap instead.
void CalcUiTest::formatStripWrapsInsteadOfHidingCommands()
{
    CalcWindow window;
    window.resize(1200, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *bar = window.findChild<::QindaOffice::Shell::WrappingToolBar *>(
        QStringLiteral("formatToolBar"));
    QVERIFY(bar != nullptr);

    // Every format command has a visible button, at a wide window...
    const QList<QToolButton *> buttons = bar->findChildren<QToolButton *>();
    const QStringList wanted = {QString::fromLatin1(ActionId::kFormatBold),
                                QString::fromLatin1(ActionId::kFormatItalic),
                                QString::fromLatin1(ActionId::kFormatUnderline),
                                QString::fromLatin1(ActionId::kFormatTextColor),
                                QString::fromLatin1(ActionId::kCalcFillColor),
                                QString::fromLatin1(ActionId::kFormatAlignLeft),
                                QString::fromLatin1(ActionId::kFormatAlignCenter),
                                QString::fromLatin1(ActionId::kFormatAlignRight)};
    const auto buttonFor = [&buttons](const QString &id) -> QToolButton * {
        for (QToolButton *b : buttons) {
            if (b->defaultAction() != nullptr && b->defaultAction()->objectName() == id) {
                return b;
            }
        }
        return nullptr;
    };
    const int wideHeight = bar->height();
    for (const QString &id : wanted) {
        QToolButton *button = buttonFor(id);
        QVERIFY2(button != nullptr, qPrintable(id));
        QVERIFY2(button->isVisible(), qPrintable(id));
    }

    // ...and still at a window far too narrow for one row, where the strip
    // grows taller rather than dropping commands.
    window.resize(560, 600);
    QTRY_VERIFY(bar->height() > wideHeight);
    for (const QString &id : wanted) {
        QToolButton *button = buttonFor(id);
        QVERIFY2(button != nullptr, qPrintable(id));
        QVERIFY2(button->isVisible(), qPrintable(id));
        // Inside the strip, not clipped off its right edge.
        QVERIFY2(button->geometry().right() <= bar->strip()->width(), qPrintable(id));
    }
}

// Ctrl+H was a dead menu entry: the find bar had no replace at all.
void CalcUiTest::replaceEditsInputsAndUndoesInOneStep()
{
    CalcWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    doc->applyInput({0, 0}, QStringLiteral("alpha one"));
    doc->applyInput({1, 0}, QStringLiteral("alpha two"));
    doc->applyInput({2, 0}, QStringLiteral("=1+1"));
    const int stepsBefore = doc->undoStack()->count();

    QAction *replace = window.action(QString::fromLatin1(ActionId::kEditReplace));
    QVERIFY(replace != nullptr);
    QVERIFY2(replace->isEnabled(), "Edit > Replace must be reachable");
    replace->trigger(); // opens the bar with the replace row revealed

    auto *findEdit = window.findChild<QLineEdit *>(QStringLiteral("calcFindEdit"));
    auto *replaceEdit = window.findChild<QLineEdit *>(QStringLiteral("calcReplaceEdit"));
    auto *all = window.findChild<QToolButton *>(QStringLiteral("calcReplaceAllButton"));
    QVERIFY(findEdit != nullptr);
    QVERIFY(replaceEdit != nullptr);
    QVERIFY(all != nullptr);
    QVERIFY2(replaceEdit->isVisible(), "Ctrl+H reveals the replace row");

    findEdit->setText(QStringLiteral("alpha"));
    replaceEdit->setText(QStringLiteral("beta"));
    all->click();

    QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("beta one"));
    QCOMPARE(doc->inputTextAt({1, 0}), QStringLiteral("beta two"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 2, 0)), QStringLiteral("2")); // untouched
    // One sweep is one undo step, however many cells it touched.
    QCOMPARE(doc->undoStack()->count(), stepsBefore + 1);
    doc->undoStack()->undo();
    QCOMPARE(doc->inputTextAt({0, 0}), QStringLiteral("alpha one"));
    QCOMPARE(doc->inputTextAt({1, 0}), QStringLiteral("alpha two"));

    // Find-only mode keeps the replace row out of the way.
    window.action(QString::fromLatin1(ActionId::kEditFind))->trigger();
    QVERIFY(!replaceEdit->isVisible());
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
