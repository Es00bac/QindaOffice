// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091): QindaCalc's view tools through the real window. View ▸ Split
// cuts the grid into four panes at the active cell; each pair scrolls on
// its own (with every pane scrolled home, A1 shows in all four — checked on
// the grab), a bar drags, a double click takes it away, and Split and
// Freeze Panes take each other's place. View ▸ New Window opens a second,
// view-only window on the workbook ("Book:2") that shows any sheet, keeps
// to it while the document window moves on, repaints with the document
// window's edits, takes no typing and closes with the document window.
// View ▸ Custom Views adds a view
// through its dialogs and Show puts back the zoom, the frozen panes, the
// hidden rows, the sheet and the cell — one undo step — and the views ride
// the crash-recovery payload; a workbook with a table turns the command
// off, as Excel does. QQO_GRAB_DIR keeps the split window at 1280 and 520
// and the new window.

#include "calc_m78_support.h"

#include "qindacalc/src/calc_recovery.h"
#include "qindacalc/src/calc_undo.h"

#include "custom_view.h"
#include "sheet.h"

#include <QUndoStack>

using namespace CalcTest;
using namespace CalcTest::M78;
namespace Calc = QindaOffice::Calc;
using QindaOffice::Apps::QindaCalc::WorkbookSnapshot;
using QindaOffice::Apps::QindaCalc::snapshotFromJson;
using QindaOffice::Apps::QindaCalc::snapshotToJson;
using QindaOffice::Apps::QindaCalc::takeSnapshot;

namespace {

bool isInk(const QImage &image, int x, int y)
{
    return image.pixelColor(x, y).lightness() < 150;
}

// Ink inside `box`, rows inset from the grid lines.
bool hasInk(const QImage &image, const QRectF &box)
{
    for (int y = int(box.top()) + 3; y < int(box.bottom()) - 2; ++y) {
        for (int x = int(box.left()) + 2; x < int(box.right()) - 2; ++x) {
            if (x >= 0 && y >= 0 && x < image.width() && y < image.height() && isInk(image, x, y)) {
                return true;
            }
        }
    }
    return false;
}

// A1's box in each pane, in window pixels: the panes start at the headers
// and past each split bar.
QList<QRectF> a1Boxes(Scene &scene)
{
    SpreadsheetItem *grid = scene.grid;
    const qreal w = grid->colLeftForTest(1) - grid->colLeftForTest(0);
    const qreal h = grid->rowTopForTest(1) - grid->rowTopForTest(0);
    const qreal x0 = grid->headerWidth();
    const qreal y0 = grid->headerHeight();
    const qreal x1 = grid->splitBarX() + grid->splitBarWidth();
    const qreal y1 = grid->splitBarY() + grid->splitBarWidth();
    QList<QRectF> boxes;
    for (const QPointF &at : {QPointF(x0, y0), QPointF(x1, y0), QPointF(x0, y1), QPointF(x1, y1)}) {
        const QPointF scenePos = grid->mapToScene(at);
        boxes.append(QRectF(scenePos, QSizeF(w, h)));
    }
    return boxes;
}

// A1's text, and numbers down column D so the sheet scrolls; column A
// holds nothing else, so ink in an A1-sized box at a pane's start is A1.
void fill(Scene &scene)
{
    type(scene, 0, 0, QStringLiteral("Alpha"));
    for (int r = 1; r < 60; ++r) {
        type(scene, r, 3, QString::number(r * 3));
    }
}

bool checked(Scene &scene, const char *id)
{
    return scene.host.actions()->entry(QString::fromLatin1(id))->isChecked();
}

bool enabled(Scene &scene, const char *id)
{
    return scene.host.actions()->entry(QString::fromLatin1(id))->isEnabled();
}

QQuickWindow *viewWindow()
{
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (window->objectName() == QLatin1String("calcViewWindow")) {
            return qobject_cast<QQuickWindow *>(window);
        }
    }
    return nullptr;
}

void press(Scene &scene, const QPointF &itemPos, bool doubleClick = false)
{
    const QPoint at = scene.grid->mapToScene(itemPos).toPoint();
    if (doubleClick) {
        QTest::mouseDClick(scene.window, Qt::LeftButton, {}, at);
    } else {
        QTest::mousePress(scene.window, Qt::LeftButton, {}, at);
    }
}

} // namespace

class CalcViewsUiTest final : public QObject {
    Q_OBJECT

    static void splitAt(Scene &scene)
    {
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fill(scene);
        scene.grid->setCursorCell(9, 3); // D10
        // Excel splits at the active cell's top-left corner, where the view
        // shows it now.
        const qreal x = scene.grid->colLeftForTest(3);
        const qreal y = scene.grid->rowTopForTest(9);
        trigger(scene, ActionId::kCalcViewSplit);
        QVERIFY(scene.grid->isSplit());
        QVERIFY(checked(scene, ActionId::kCalcViewSplit));
        QCOMPARE(scene.grid->splitX(), x);
        QCOMPARE(scene.grid->splitY(), y);
    }

private slots:
    void splitShowsFourPanes()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 760));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            splitAt(scene);
            if (QTest::currentTestFailed()) {
                return;
            }
            // The far panes go on from where the near ones end…
            QVERIFY(scene.grid->scrollX() > 0 && scene.grid->scrollY() > 0);
            grab(scene, QStringLiteral("qindacalc_m82_split_%1").arg(width));
            // …and scrolled home, every pane shows A1.
            scene.grid->setScrollX(0);
            scene.grid->setScrollY(0);
            QTest::qWait(150);
            const QImage shot = scene.window->grabWindow();
            const QList<QRectF> boxes = a1Boxes(scene);
            for (int pane = 0; pane < 4; ++pane) {
                QVERIFY2(hasInk(shot, boxes.at(pane)), qPrintable(QStringLiteral("A1 missing in pane %1 at %2").arg(pane).arg(width)));
            }
            grab(scene, QStringLiteral("qindacalc_m82_split_home_%1").arg(width));
            // Scroll the top panes down (they scroll together, as Excel's
            // do): the bottom ones stay.
            scene.grid->setSplitScrollY(scene.grid->rowTopForTest(20) - scene.grid->rowTopForTest(0));
            QTest::qWait(150);
            const QImage moved = scene.window->grabWindow();
            const QList<QRectF> after = a1Boxes(scene);
            QVERIFY2(!hasInk(moved, after.at(0)) && !hasInk(moved, after.at(1)), "the top panes did not scroll");
            QVERIFY2(hasInk(moved, after.at(2)) && hasInk(moved, after.at(3)), "the bottom panes scrolled with them");
        }
    }

    void splitBarsDragAndGo()
    {
        Scene scene(QSize(1280, 760));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        splitAt(scene);
        if (QTest::currentTestFailed()) {
            return;
        }
        SpreadsheetItem *grid = scene.grid;
        // Drag the vertical bar 120 px right (away from the crossing).
        const qreal before = grid->splitX();
        const QPointF bar(grid->splitBarX() + grid->splitBarWidth() / 2, grid->splitBarY() + 120);
        press(scene, bar);
        QTest::mouseMove(scene.window, grid->mapToScene(bar + QPointF(60, 0)).toPoint());
        QTest::mouseMove(scene.window, grid->mapToScene(bar + QPointF(120, 0)).toPoint());
        QTest::mouseRelease(scene.window, Qt::LeftButton, {}, grid->mapToScene(bar + QPointF(120, 0)).toPoint());
        QVERIFY2(qAbs(grid->splitX() - (before + 120)) <= 2, qPrintable(QString::number(grid->splitX() - before)));
        // A double click on the horizontal bar takes it away.
        press(scene, QPointF(grid->headerWidth() + 40, grid->splitBarY() + grid->splitBarWidth() / 2), true);
        QTRY_COMPARE(grid->splitY(), 0.0);
        QVERIFY(grid->splitX() > 0);
        QVERIFY(checked(scene, ActionId::kCalcViewSplit));
        // Freeze Panes takes the split's place; Split takes the freeze's.
        trigger(scene, ActionId::kCalcFreezePanes);
        QVERIFY(!grid->isSplit());
        QVERIFY(!checked(scene, ActionId::kCalcViewSplit));
        QVERIFY(scene.doc->workbook()->sheetAt(0).freezeRows() > 0 || scene.doc->workbook()->sheetAt(0).freezeCols() > 0);
        trigger(scene, ActionId::kCalcViewSplit);
        QVERIFY(grid->isSplit());
        QCOMPARE(scene.doc->workbook()->sheetAt(0).freezeRows(), 0);
        QCOMPARE(scene.doc->workbook()->sheetAt(0).freezeCols(), 0);
        trigger(scene, ActionId::kCalcViewSplit);
        QVERIFY(!grid->isSplit());
        QVERIFY(!checked(scene, ActionId::kCalcViewSplit));
    }

    void newWindowViewsTheWorkbook()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fill(scene);
        scene.doc->insertSheet(QStringLiteral("Notes"));
        scene.host.setActiveSheet(0);
        const QString first = scene.doc->workbook()->sheetName(0);
        trigger(scene, ActionId::kCalcViewNewWindow);
        QTRY_VERIFY(viewWindow() != nullptr);
        QPointer<QQuickWindow> view = viewWindow();
        QVERIFY(QTest::qWaitForWindowExposed(view.data()));
        QCOMPARE(view->title(), scene.host.title() + QStringLiteral(":2"));
        QVERIFY(view->width() > 0 && view->width() <= scene.window->width());
        auto *grid = qobject_cast<SpreadsheetItem *>(findItem(view->contentItem(), QStringLiteral("calcViewGrid")));
        QVERIFY(grid != nullptr);
        QVERIFY(grid->isReadOnly());
        QCOMPARE(grid->viewSheet(), first);
        QCOMPARE(findItem(view->contentItem(), QStringLiteral("viewWindowSheet"))->property("count").toInt(), 2);

        // Its own sheet: the document window stays where it is…
        view->setProperty("sheet", QStringLiteral("Notes"));
        QCOMPARE(grid->sheetIndex(), 1);
        QCOMPARE(scene.doc->activeSheet(), 0);
        // …and its edits on that sheet show at once.
        scene.host.setActiveSheet(1);
        type(scene, 0, 0, QStringLiteral("Seen from the second window"));
        scene.host.setActiveSheet(0);
        QCOMPARE(grid->sheetIndex(), 1);
        QTest::qWait(150);
        const QImage shot = view->grabWindow();
        const qreal w = grid->colLeftForTest(1) - grid->colLeftForTest(0);
        const qreal h = grid->rowTopForTest(1) - grid->rowTopForTest(0);
        const QPointF a1 = grid->mapToScene(QPointF(grid->headerWidth(), grid->headerHeight()));
        QVERIFY2(hasInk(shot, QRectF(a1, QSizeF(w, h))), "the view window does not show the edit");
        QindaOffice::Test::saveGrab(view, QStringLiteral("qindacalc_m82_new_window"));

        // It views: typing opens no editor and changes nothing.
        grid->forceActiveFocus();
        QTest::keyClick(view.data(), Qt::Key_X);
        QTest::keyClick(view.data(), Qt::Key_Delete);
        QVERIFY(!grid->isEditing());
        QCOMPARE(Calc::toText(scene.doc->workbook()->cellValue(1, 0, 0)), QStringLiteral("Seen from the second window"));
        // Split is its own.
        grid->setCursorCell(9, 3);
        grid->toggleSplit();
        QVERIFY(grid->isSplit());
        QVERIFY(!scene.grid->isSplit());

        // It closes with the document window.
        delete scene.window;
        scene.window = nullptr;
        QTRY_VERIFY(view.isNull());
    }

    void customViewsAddAndShow()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fill(scene);
        scene.doc->insertSheet(QStringLiteral("Notes"));
        scene.host.setActiveSheet(0);
        scene.doc->setFreeze(1, 0);
        scene.doc->setLinesHidden(Calc::OutlineAxis::Rows, 4, 5, true);
        scene.grid->setZoom(1.25);
        scene.grid->setCursorCell(6, 2); // C7
        QVERIFY(enabled(scene, ActionId::kCalcCustomViews));

        trigger(scene, ActionId::kCalcCustomViews);
        QVERIFY(dialogOpened(scene, "customViewsDialog"));
        QMetaObject::invokeMethod(popup(scene, "customViewsDialog"), "addRequested");
        QVERIFY(dialogOpened(scene, "customViewAddDialog"));
        QVERIFY(setText(scene, "customViewName", QStringLiteral("Review")));
        QVERIFY(answer(scene, "customViewAddDialog", true));
        QVERIFY(dialogClosed(scene, "customViewsDialog")); // Excel's Add closes both
        const Calc::Workbook &wb = *scene.doc->workbook();
        QCOMPARE(wb.customViews.size(), 1);
        QCOMPARE(wb.customViews.first().name, QStringLiteral("Review"));
        QCOMPARE(wb.customViews.first().activeSheet, wb.sheetName(0));
        const Calc::Sheet::CustomView *part = Calc::customViewOf(wb.sheetAt(0), wb.customViews.first().guid);
        QVERIFY(part != nullptr);
        QCOMPARE(part->scale, 125);
        QCOMPARE(part->activeCell, (Calc::CellAddr{6, 2}));
        QCOMPARE(part->hiddenRows, (QList<qint32>{4, 5}));

        // The view rides the recovery payload.
        WorkbookSnapshot recovered;
        QString error;
        QVERIFY2(snapshotFromJson(snapshotToJson(takeSnapshot(wb, 0)), &recovered, &error), qPrintable(error));
        QCOMPARE(recovered.customViews, wb.customViews);
        QCOMPARE(recovered.sheets.at(0).customViews, wb.sheetAt(0).customViews());

        // Everything changes; Show puts it back.
        scene.doc->setFreeze(0, 0);
        scene.doc->setLinesHidden(Calc::OutlineAxis::Rows, 4, 5, false);
        scene.grid->setZoom(1.0);
        scene.host.setActiveSheet(1);
        trigger(scene, ActionId::kCalcCustomViews);
        QVERIFY(dialogOpened(scene, "customViewsDialog"));
        QObject *dialog = popup(scene, "customViewsDialog");
        QCOMPARE(dialog->property("views").toList().size(), 1);
        dialog->setProperty("selected", 0);
        QMetaObject::invokeMethod(dialog, "show");
        QVERIFY(dialogClosed(scene, "customViewsDialog"));
        QCOMPARE(scene.doc->activeSheet(), 0);
        QCOMPARE(wb.sheetAt(0).freezeRows(), 1);
        QVERIFY(wb.sheetAt(0).outline().isHidden(Calc::OutlineAxis::Rows, 4));
        QVERIFY(wb.sheetAt(0).outline().isHidden(Calc::OutlineAxis::Rows, 5));
        QCOMPARE(scene.grid->zoom(), 1.25);
        QCOMPARE(scene.grid->cursorRow(), 6);
        QCOMPARE(scene.grid->cursorCol(), 2);
        grab(scene, QStringLiteral("qindacalc_m82_custom_view_1280"));

        // One undo step: the document part goes back.
        QCOMPARE(scene.doc->undoStack()->undoText(), QStringLiteral("Show Custom View"));
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->sheetAt(0).freezeRows(), 0);
        QVERIFY(!scene.doc->workbook()->sheetAt(0).outline().isHidden(Calc::OutlineAxis::Rows, 4));

        // Delete through the dialog.
        trigger(scene, ActionId::kCalcCustomViews);
        QVERIFY(dialogOpened(scene, "customViewsDialog"));
        dialog->setProperty("selected", 0);
        QQuickItem *remove = scene.item(QStringLiteral("customViewDelete"));
        QVERIFY(remove != nullptr);
        QMetaObject::invokeMethod(remove, "clicked");
        QVERIFY(scene.doc->workbook()->customViews.isEmpty());
        QCOMPARE(dialog->property("views").toList().size(), 0);
        QVERIFY(answer(scene, "customViewsDialog", true));

        // A table turns Custom Views off.
        QVERIFY(scene.doc->insertTable({{0, 3}, {10, 3}}));
        QVERIFY(!enabled(scene, ActionId::kCalcCustomViews));
        QVERIFY(!scene.host.addCustomView(QStringLiteral("No"), true, true).isEmpty());
    }
};

CALC_TEST_MAIN(CalcViewsUiTest)

#include "tst_calc_views_ui.moc"
