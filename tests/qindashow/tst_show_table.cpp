// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow's tables through the real window (M67, D-076): Insert ▸ Table
// lands a table in the deck's default style and the Table Design / Table
// Layout tabs appear; a click into a cell edits it in place, typing goes
// into the cell, Tab and Shift+Tab walk the cells and Tab in the last cell
// adds a row; the table commands act on the selected cells, each one undo
// step; dragging a column boundary resizes; a PowerPoint table edited in
// the window is saved as .pptx with its style kept and its edits read back.
// Grabs (QQO_GRAB_DIR): a table being edited.

#include "show_test_support.h"

#include "qindashow/src/show_table_actions.h"
#include "show_model/pptx_fixture_slides.h"
#include "support/quick_dump.h"

#include "pptx_reader.h"

#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QUndoStack>

using namespace ShowTest;
using namespace QindaOffice::Apps::Show;
namespace ShowModel = QindaOffice::Show;
namespace TableEdit = QindaOffice::Show::TableEdit;

namespace {

QString sid(const char *id)
{
    return QString::fromLatin1(id);
}

const ShowModel::Shape *selectedTable(SlideCanvasItem *canvas)
{
    const QString id = canvas->tableShapeId();
    return id.isEmpty() ? nullptr : canvas->slide()->findShape(id);
}

QString cellText(const ShowModel::Shape *shape, int row, int column)
{
    QString text;
    for (const ShowModel::TextParagraph &p : shape->table.rows.at(row).cells.at(column).paragraphs) {
        for (const ShowModel::TextRun &r : p.runs) {
            text += r.text;
        }
    }
    return text;
}

// QTest::keyClicks takes widgets only: one key per character.
void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text) {
        QTest::keyClick(window, c.toLatin1());
    }
}

// The window point over the middle of cell (row, column).
QPoint cellPoint(SlideCanvasItem *canvas, const ShowModel::Shape *shape, int row, int column)
{
    const ShowModel::TableGrid grid = ShowModel::tableGrid(shape->table, shape->rect.normalized());
    return canvas->mapToScene(grid.cellRect(row, column).center()).toPoint();
}

} // namespace

class ShowTableTest : public QObject {
    Q_OBJECT
private slots:
    void insertTableLandsARealTable()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        QVERIFY(!ribbon->property("contextIds").toStringList().contains(QStringLiteral("table")));
        QSignalSpy asked(&scene.host, &ShowHost::insertTableRequested);
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kInsertTable)));
        QCOMPARE(asked.count(), 1);
        const int before = scene.doc->undoStack()->count();
        scene.host.insertTable(3, 4);
        const ShowModel::Shape *table = selectedTable(scene.canvas);
        QVERIFY(table != nullptr);
        QCOMPARE(table->table.rows.size(), 3);
        QCOMPARE(table->table.columns.size(), 4);
        QCOMPARE(table->table.styleId, QStringLiteral("{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"));
        QVERIFY(table->table.firstRow && table->table.bandRows && table->table.look.known);
        QCOMPARE(scene.doc->undoStack()->count(), before + 1);
        // The table's contextual tabs, and its commands lit.
        QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("table")));
        QVERIFY(scene.host.action(sid(TableActionId::kRowBelow))->isEnabled());
        QVERIFY(scene.host.action(QStringLiteral("showTableHeaderRowAction"))->isChecked());
        QVERIFY(!scene.host.action(sid(TableActionId::kMerge))->isEnabled()); // no cells selected
        scene.doc->undoStack()->undo();
        QVERIFY(scene.canvas->tableShapeId().isEmpty());
    }

    void clickIntoACellAndTabThroughIt()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(scene.window));
        scene.host.insertTable(2, 2);
        QTest::qWait(100);
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->tableShapeId();
        QVERIFY(!id.isEmpty());
        // The table is selected: a click into a cell puts the caret there.
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, cellPoint(canvas, selectedTable(canvas), 0, 0));
        QTRY_VERIFY(canvas->editingCell());
        QQuickItem *editor = scene.item(QStringLiteral("shapeTextEditor"));
        QVERIFY(editor != nullptr && editor->isVisible());
        QTRY_VERIFY(editor->hasActiveFocus());
        // The editor sits over the cell, inside its margins.
        const QRectF cell = ShowModel::tableGrid(selectedTable(canvas)->table, selectedTable(canvas)->rect).cellRect(0, 0);
        QCOMPARE(canvas->editorRect(), cell.adjusted(7.2, 3.6, -7.2, -3.6));
        typeText(scene.window, QStringLiteral("Region"));
        QTest::keyClick(scene.window, Qt::Key_Tab);
        QTRY_COMPARE(cellText(selectedTable(canvas), 0, 0), QStringLiteral("Region"));
        QVERIFY(canvas->editingCell());
        QCOMPARE(canvas->cellSelection(), (ShowModel::TableRange{0, 1, 0, 1}));
        // Typed into the header, the text takes the header's look.
        const ShowModel::TextRun typed = selectedTable(canvas)->table.rows.at(0).cells.at(0).paragraphs.first().runs.first();
        QVERIFY(typed.bold);
        QCOMPARE(typed.color, selectedTable(canvas)->table.look.firstRow.text);
        typeText(scene.window, QStringLiteral("Sales"));
        QTest::keyClick(scene.window, Qt::Key_Tab);
        QTest::keyClick(scene.window, Qt::Key_Tab);
        typeText(scene.window, QStringLiteral("North"));
        QTest::keyClick(scene.window, Qt::Key_Backtab);
        QTRY_COMPARE(canvas->cellSelection(), (ShowModel::TableRange{1, 0, 1, 0}));
        QCOMPARE(cellText(selectedTable(canvas), 1, 1), QStringLiteral("North"));
        QVERIFY(!selectedTable(canvas)->table.rows.at(1).cells.at(1).paragraphs.first().runs.first().bold); // body look
        // Tab in the last cell adds a row, as PowerPoint does.
        QTest::keyClick(scene.window, Qt::Key_Tab);
        QTest::keyClick(scene.window, Qt::Key_Tab);
        QTRY_COMPARE(selectedTable(canvas)->table.rows.size(), 3);
        QCOMPARE(canvas->cellSelection(), (ShowModel::TableRange{2, 0, 2, 0}));
        QQuickItem *grab = scene.item(QStringLiteral("slideEditor"));
        QVERIFY(grab != nullptr);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindashow_table_editing"));
        QTest::keyClick(scene.window, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->editingCell());
        // Each cell edit is its own undo step.
        scene.doc->undoStack()->undo(); // the added row
        QCOMPARE(selectedTable(canvas)->table.rows.size(), 2);
        scene.doc->undoStack()->undo();
        QCOMPARE(cellText(selectedTable(canvas), 1, 1), QString());
    }

    void aClickIntoAnUnselectedTableEditsTheCell()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.insertTable(2, 2);
        QTest::qWait(100);
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->tableShapeId();
        const ShowModel::Shape table = *selectedTable(canvas);
        canvas->clearSelection();
        QVERIFY(canvas->tableShapeId().isEmpty());
        // One click selects the table and puts the caret in the cell, as in
        // PowerPoint; a click on the frame's edge only selects it.
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, cellPoint(canvas, &table, 1, 1));
        QTRY_VERIFY(canvas->editingCell());
        QCOMPARE(canvas->tableShapeId(), id);
        QCOMPARE(canvas->cellSelection(), (ShowModel::TableRange{1, 1, 1, 1}));
        QTest::keyClick(scene.window, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->editingCell());
        canvas->clearSelection();
        const QPointF edge(table.rect.left() + 1, table.rect.center().y());
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, canvas->mapToScene(edge).toPoint());
        QCOMPARE(canvas->tableShapeId(), id);
        QVERIFY(!canvas->editingCell());
    }

    void commandsActOnTheSelectedCells()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.insertTable(3, 3);
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->tableShapeId();
        QUndoStack *undo = scene.doc->undoStack();
        canvas->selectCells(id, {1, 1, 1, 1});
        QVERIFY(scene.host.action(sid(TableActionId::kSplit))->isEnabled());
        const qreal width = selectedTable(canvas)->rect.width();
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kColumnRight)));
        QCOMPARE(selectedTable(canvas)->table.columns.size(), 4);
        QCOMPARE(selectedTable(canvas)->rect.width(), width + selectedTable(canvas)->table.columns.at(1));
        QCOMPARE(canvas->cellSelection(), (ShowModel::TableRange{1, 2, 1, 2})); // the new column's cell
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kRowAbove)));
        QCOMPARE(selectedTable(canvas)->table.rows.size(), 4);
        canvas->selectCells(id, {2, 0, 3, 1});
        QVERIFY(scene.host.action(sid(TableActionId::kMerge))->isEnabled());
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kMerge)));
        QCOMPARE(selectedTable(canvas)->table.rows.at(2).cells.at(0).gridSpan, 2);
        QCOMPARE(selectedTable(canvas)->table.rows.at(2).cells.at(0).rowSpan, 2);
        QSignalSpy split(&scene.host, &ShowHost::splitCellsRequested);
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kSplit)));
        QCOMPARE(split.count(), 1);
        QCOMPARE(split.first().at(0).toInt(), 2); // a merged block offers its own size
        scene.host.splitCells(2, 2);
        QCOMPARE(selectedTable(canvas)->table.rows.at(2).cells.at(0).gridSpan, 1);
        canvas->selectCells(id, {0, 0, 0, 3});
        scene.host.applyTableShading(QColor(200, 30, 30));
        QCOMPARE(selectedTable(canvas)->table.rows.at(0).cells.at(3).fill, QColor(200, 30, 30));
        scene.host.setTablePenColor(QColor(Qt::blue));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showTableBordersOutsideAction")));
        QCOMPARE(selectedTable(canvas)->table.rows.at(0).cells.at(0).top.color, QColor(Qt::blue));
        QVERIFY(selectedTable(canvas)->table.rows.at(1).cells.at(0).top.set); // the neighbour's facing edge
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showTableHeaderRowAction")));
        QVERIFY(!selectedTable(canvas)->table.firstRow);
        QVERIFY(!scene.host.action(QStringLiteral("showTableHeaderRowAction"))->isChecked());
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kDistributeColumns)));
        QCOMPARE(selectedTable(canvas)->table.columns.at(0), selectedTable(canvas)->table.columns.at(3));
        canvas->selectCells(id, {0, 1, 0, 1});
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kDeleteColumns)));
        QCOMPARE(selectedTable(canvas)->table.columns.size(), 3);
        canvas->selectCells(id, {0, 0, 0, 0});
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kDeleteRows)));
        QCOMPARE(selectedTable(canvas)->table.rows.size(), 3);
        // Every command was one step; undoing all of them restores the table.
        while (undo->canUndo() && undo->undoText() != QObject::tr("Insert shape")) {
            undo->undo();
        }
        QCOMPARE(selectedTable(canvas)->table.rows.size(), 3);
        QCOMPARE(selectedTable(canvas)->table.columns.size(), 3);
        QVERIFY(selectedTable(canvas)->table.firstRow);
    }

    void boundaryDragResizesAColumn()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.insertTable(2, 3);
        QTest::qWait(100);
        SlideCanvasItem *canvas = scene.canvas;
        const ShowModel::Shape *table = selectedTable(canvas);
        const QList<qreal> before = table->table.columns;
        const ShowModel::TableGrid grid = ShowModel::tableGrid(table->table, table->rect);
        const QPointF boundary(grid.xs.at(1), (grid.ys.at(0) + grid.ys.at(1)) / 2);
        const QPointF dragged = boundary + QPointF(30, 0);
        const int steps = scene.doc->undoStack()->count();
        QTest::mousePress(scene.window, Qt::LeftButton, {}, canvas->mapToScene(boundary).toPoint());
        QTest::mouseMove(scene.window, canvas->mapToScene(boundary + QPointF(15, 0)).toPoint());
        QTest::mouseMove(scene.window, canvas->mapToScene(dragged).toPoint());
        QTest::mouseRelease(scene.window, Qt::LeftButton, {}, canvas->mapToScene(dragged).toPoint());
        table = selectedTable(canvas);
        QVERIFY(qAbs(table->table.columns.at(0) - (before.at(0) + 30)) < 2);
        QVERIFY(qAbs(table->table.columns.at(0) + table->table.columns.at(1) - (before.at(0) + before.at(1))) < 0.01);
        QCOMPARE(table->table.columns.at(2), before.at(2));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        QVERIFY(!canvas->editingCell()); // a drag is not a click into a cell
    }

    void powerPointTableEditedAndSaved()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("deck.pptx"));
        QFile file(source);
        const QByteArray bytes = PptxFixture::build();
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
        file.close();
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(scene.host.loadPath(source));
        scene.host.selectSlideRow(1);
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->slide()->shapes.first().id;
        canvas->setSelected(id, true);
        QCOMPARE(canvas->tableShapeId(), id);
        canvas->selectCells(id, {1, 1, 1, 1});
        QVERIFY(scene.host.actions()->trigger(sid(TableActionId::kColumnRight)));
        canvas->beginCellEdit(id, 1, 2);
        QVERIFY(canvas->editingCell());
        QTextCursor(canvas->editorDocument()).insertText(QStringLiteral("C")); // in the cell's text look
        canvas->commitTextEdit(false);
        const QString saved = dir.filePath(QStringLiteral("saved.pptx"));
        QVERIFY(scene.doc->save(saved));
        QString error;
        const ShowModel::Presentation back = ShowModel::readPptxFile(saved, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const ShowModel::Table &table = back.slides.at(1).shapes.first().table;
        QCOMPARE(table.columns.size(), 3);
        QCOMPARE(table.styleId, QStringLiteral("{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"));
        QCOMPARE(table.rows.at(0).cells.at(0).gridSpan, 2); // a column beside a merged block stays outside it
        QCOMPARE(cellText(&back.slides.at(1).shapes.first(), 1, 2), QStringLiteral("C"));
        QCOMPARE(table, scene.doc->presentation()->slides.at(1).shapes.first().table);
    }
};

SHOW_TEST_MAIN(ShowTableTest)

#include "tst_show_table.moc"
