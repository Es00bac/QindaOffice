// SPDX-License-Identifier: GPL-3.0-or-later
#include "table_actions.h"

#include "wrapping_tool_bar.h"

#include "page_text_view.h"
#include "standard_actions.h"
#include "write_document.h"

#include <QIcon>
#include <QMenu>
#include <QTextTable>
#include <QToolBar>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

namespace QindaOffice::Apps::Write {

QList<QAction *> TableActions::all() const
{
    return {insertRowAbove,  insertRowBelow, insertColumnLeft, insertColumnRight,
            deleteRow,       deleteColumn,   mergeCells,       splitCell};
}

namespace {

QAction *makeAction(QObject *parent, const char *id, const QString &text)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    return action;
}

} // namespace

TableController::TableController(PageTextView *view, WriteDocument *document, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_document(document)
{
    using namespace Shell::ActionId;
    auto &a = m_actions;
    a.insertRowAbove = makeAction(this, kTableInsertRowAbove, tr("Insert Row &Above"));
    a.insertRowBelow = makeAction(this, kTableInsertRowBelow, tr("Insert Row &Below"));
    a.insertColumnLeft = makeAction(this, kTableInsertColumnLeft, tr("Insert Column &Left"));
    a.insertColumnRight = makeAction(this, kTableInsertColumnRight, tr("Insert Column &Right"));
    a.deleteRow = makeAction(this, kTableDeleteRow, tr("Delete Ro&w"));
    a.deleteColumn = makeAction(this, kTableDeleteColumn, tr("Delete Colum&n"));
    a.mergeCells = makeAction(this, kTableMergeCells, tr("&Merge Cells"));
    a.splitCell = makeAction(this, kTableSplitCell, tr("&Split Cell"));

    connect(a.insertRowAbove, &QAction::triggered, this, [this] { insertRows(1, true); });
    connect(a.insertRowBelow, &QAction::triggered, this, [this] { insertRows(1, false); });
    connect(a.insertColumnLeft, &QAction::triggered, this, [this] { insertColumns(1, true); });
    connect(a.insertColumnRight, &QAction::triggered, this, [this] { insertColumns(1, false); });
    connect(a.deleteRow, &QAction::triggered, this, [this] { deleteRows(); });
    connect(a.deleteColumn, &QAction::triggered, this, [this] { deleteColumns(); });
    connect(a.mergeCells, &QAction::triggered, this, [this] { mergeCells(); });
    connect(a.splitCell, &QAction::triggered, this, [this] { splitCell(); });

    syncFromCursor();
    connect(m_view->control(), &QWidgetTextControl::cursorPositionChanged, this,
            [this] { syncFromCursor(); });
}

void TableController::populateTableMenu(QMenu *menu)
{
    menu->addAction(m_actions.insertRowAbove);
    menu->addAction(m_actions.insertRowBelow);
    menu->addAction(m_actions.insertColumnLeft);
    menu->addAction(m_actions.insertColumnRight);
    menu->addSeparator();
    menu->addAction(m_actions.deleteRow);
    menu->addAction(m_actions.deleteColumn);
    menu->addSeparator();
    menu->addAction(m_actions.mergeCells);
    menu->addAction(m_actions.splitCell);
}

void TableController::appendToToolBar(QToolBar *toolBar)
{
    // The formatting strip wraps, so the table commands join the flow rather
    // than pushing the tail of it behind an extension chevron.
    if (auto *wrapping = qobject_cast<Shell::WrappingToolBar *>(toolBar)) {
        wrapping->addStripSeparator();
        for (QAction *action : m_actions.all()) {
            wrapping->addStripAction(action);
        }
        return;
    }
    toolBar->addSeparator();
    for (QAction *action : m_actions.all()) {
        toolBar->addAction(action);
    }
}

void TableController::insertRows(int count, bool above)
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    table->insertRows(cell.row() + (above ? 0 : 1), count);
    m_document->setDirty(true);
}

void TableController::insertColumns(int count, bool left)
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    table->insertColumns(cell.column() + (left ? 0 : 1), count);
    m_document->setDirty(true);
}

void TableController::deleteRows()
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    // v1: the cursor cell's row (multi-row selection arrives with ranged
    // table selection in a later pass).
    const QTextTableCell cell = table->cellAt(cursor);
    table->removeRows(cell.row(), 1);
    keepCursorInTable(table, cell);
    m_document->setDirty(true);
}

// Row/column removal can eject the cursor from the table (Qt keeps the
// position, which may now sit before the table); place it at the nearest
// surviving cell so the next table op and the enabled action state stay
// meaningful. Unconditional: cheap and always correct.
void TableController::keepCursorInTable(QTextTable *table, const QTextTableCell &was)
{
    const int row = qMin(was.row(), table->rows() - 1);
    const int col = qMin(was.column(), table->columns() - 1);
    const QTextTableCell target = table->cellAt(row, col);
    if (!target.isValid()) {
        return; // table gone: leave the cursor where Qt put it
    }
    // AGENT-NOTE (Qt quirk): on an EMPTY cell, firstCursorPosition() sits
    // on the cell-boundary character, and cellAt() there answers invalid
    // (so the next table op would no-op). Step into the cell content;
    // when the cell is empty this lands on its (empty) paragraph.
    QTextCursor cursor = target.firstCursorPosition();
    if (!table->cellAt(cursor).isValid() || table->cellAt(cursor).row() != row
        || table->cellAt(cursor).column() != col) {
        cursor.movePosition(QTextCursor::NextCharacter);
        if (!table->cellAt(cursor).isValid()) {
            return;
        }
    }
    m_view->control()->setTextCursor(cursor);
}

void TableController::deleteColumns()
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    table->removeColumns(cell.column(), 1);
    keepCursorInTable(table, cell);
    m_document->setDirty(true);
}

void TableController::mergeCells()
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    // Merge the cursor cell with the one to its right; Qt expands the
    // merged cell over the rectangular selection when one exists.
    const QTextTableCell cell = table->cellAt(cursor);
    const int lastRow = qMin(cell.row() + cell.rowSpan() - 1, table->rows() - 1);
    const int lastCol = qMin(cell.column() + cell.columnSpan(), table->columns() - 1);
    if (lastCol <= cell.column()) {
        return;
    }
    table->mergeCells(cell.row(), cell.column(), lastRow - cell.row() + 1,
                      lastCol - cell.column() + 1);
    m_document->setDirty(true);
}

void TableController::splitCell()
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    // Qt's numRows/numCols are the spans AFTER splitting: 1x1 restores
    // the individual cells regardless of how wide the merge was.
    table->splitCell(cell.row(), cell.column(), 1, 1);
    m_document->setDirty(true);
}

void TableController::syncFromCursor()
{
    const QTextCursor cursor = m_view->control()->textCursor();
    QTextTable *table = cursor.currentTable();
    const bool inTable = table != nullptr;
    m_actions.insertRowAbove->setEnabled(inTable);
    m_actions.insertRowBelow->setEnabled(inTable);
    m_actions.insertColumnLeft->setEnabled(inTable);
    m_actions.insertColumnRight->setEnabled(inTable);
    m_actions.deleteRow->setEnabled(inTable && table->rows() > 1);
    m_actions.deleteColumn->setEnabled(inTable && table->columns() > 1);
    m_actions.splitCell->setEnabled(inTable && (cursor.currentTable()->cellAt(cursor).rowSpan() > 1
                                                || cursor.currentTable()->cellAt(cursor).columnSpan()
                                                       > 1));
    // Merge needs a neighbour to absorb.
    m_actions.mergeCells->setEnabled(
        inTable && cursor.currentTable()->cellAt(cursor).column() + 1 < table->columns());
}

} // namespace QindaOffice::Apps::Write
