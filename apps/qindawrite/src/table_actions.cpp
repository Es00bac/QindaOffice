// SPDX-License-Identifier: GPL-3.0-or-later
#include "table_actions.h"

#include "menu_tree.h"
#include "page_text_control.h"
#include "standard_actions.h"
#include "write_document.h"

#include <QTextTable>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

QList<ActionEntry *> TableActions::all() const
{
    return {insertRowAbove, insertRowBelow, insertColumnLeft, insertColumnRight,
            deleteRow,      deleteColumn,   mergeCells,       splitCell};
}

TableController::TableController(PageTextControl *control, WriteDocument *document,
                                 Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    using namespace Quick::ActionId;
    const auto make = [model](const char *id, const QString &text, const char *icon) {
        return model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
    };
    auto &a = m_actions;
    a.insertRowAbove = make(kTableInsertRowAbove, tr("Insert Row &Above"), "between-horizontal-start");
    a.insertRowBelow = make(kTableInsertRowBelow, tr("Insert Row &Below"), "rows-2");
    a.insertColumnLeft = make(kTableInsertColumnLeft, tr("Insert Column &Left"), "between-vertical-start");
    a.insertColumnRight = make(kTableInsertColumnRight, tr("Insert Column &Right"), "columns-2");
    a.deleteRow = make(kTableDeleteRow, tr("Delete Ro&w"), "separator-horizontal");
    a.deleteColumn = make(kTableDeleteColumn, tr("Delete Colum&n"), "separator-vertical");
    a.mergeCells = make(kTableMergeCells, tr("&Merge Cells"), "group");
    a.splitCell = make(kTableSplitCell, tr("&Split Cell"), "ungroup");

    connect(a.insertRowAbove, &ActionEntry::triggered, this, [this] { insertRows(1, true); });
    connect(a.insertRowBelow, &ActionEntry::triggered, this, [this] { insertRows(1, false); });
    connect(a.insertColumnLeft, &ActionEntry::triggered, this, [this] { insertColumns(1, true); });
    connect(a.insertColumnRight, &ActionEntry::triggered, this, [this] { insertColumns(1, false); });
    connect(a.deleteRow, &ActionEntry::triggered, this, [this] { deleteRows(); });
    connect(a.deleteColumn, &ActionEntry::triggered, this, [this] { deleteColumns(); });
    connect(a.mergeCells, &ActionEntry::triggered, this, [this] { mergeCells(); });
    connect(a.splitCell, &ActionEntry::triggered, this, [this] { splitCell(); });

    syncFromCursor();
    connect(m_control, &PageTextControl::cursorPositionChanged, this, [this] { syncFromCursor(); });
}

QList<MenuNode> TableController::tableMenuItems() const
{
    const auto a = [](const ActionEntry *entry) { return MenuNode::action(entry->id()); };
    return {a(m_actions.insertRowAbove),   a(m_actions.insertRowBelow),
            a(m_actions.insertColumnLeft), a(m_actions.insertColumnRight),
            MenuNode::separator(),         a(m_actions.deleteRow),
            a(m_actions.deleteColumn),     MenuNode::separator(),
            a(m_actions.mergeCells),       a(m_actions.splitCell)};
}

QStringList TableController::stripIds() const
{
    QStringList ids;
    for (const ActionEntry *entry : m_actions.all()) {
        ids.append(entry->id());
    }
    return ids;
}

void TableController::insertRows(int count, bool above)
{
    const QTextCursor cursor = m_control->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    table->insertRows(cell.row() + (above ? 0 : 1), count);
    m_document->setDirty(true);
    syncFromCursor();
}

void TableController::insertColumns(int count, bool left)
{
    const QTextCursor cursor = m_control->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    table->insertColumns(cell.column() + (left ? 0 : 1), count);
    m_document->setDirty(true);
    syncFromCursor();
}

void TableController::deleteRows()
{
    const QTextCursor cursor = m_control->textCursor();
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
// surviving cell so the next table op and the enabled state stay
// meaningful. Unconditional: cheap and always correct.
void TableController::keepCursorInTable(QTextTable *table, const QTextTableCell &was)
{
    const int row = qMin(was.row(), table->rows() - 1);
    const int col = qMin(was.column(), table->columns() - 1);
    const QTextTableCell target = table->cellAt(row, col);
    if (!target.isValid()) {
        syncFromCursor();
        return; // table gone: leave the cursor where Qt put it
    }
    // AGENT-NOTE (Qt quirk): on an EMPTY cell, firstCursorPosition() sits
    // on the cell-boundary character, and cellAt() there answers invalid
    // (so the next table op would no-op). Step into the cell content.
    QTextCursor cursor = target.firstCursorPosition();
    if (!table->cellAt(cursor).isValid() || table->cellAt(cursor).row() != row
        || table->cellAt(cursor).column() != col) {
        cursor.movePosition(QTextCursor::NextCharacter);
        if (!table->cellAt(cursor).isValid()) {
            syncFromCursor();
            return;
        }
    }
    m_control->setTextCursor(cursor);
}

void TableController::deleteColumns()
{
    const QTextCursor cursor = m_control->textCursor();
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
    const QTextCursor cursor = m_control->textCursor();
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
    table->mergeCells(cell.row(), cell.column(), lastRow - cell.row() + 1, lastCol - cell.column() + 1);
    m_document->setDirty(true);
    syncFromCursor();
}

void TableController::splitCell()
{
    const QTextCursor cursor = m_control->textCursor();
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    // Qt's numRows/numCols are the spans AFTER splitting: 1x1 restores
    // the individual cells regardless of how wide the merge was.
    table->splitCell(cell.row(), cell.column(), 1, 1);
    m_document->setDirty(true);
    syncFromCursor();
}

void TableController::syncFromCursor()
{
    const QTextCursor cursor = m_control->textCursor();
    QTextTable *table = cursor.currentTable();
    const bool inTable = table != nullptr;
    m_actions.insertRowAbove->setEnabled(inTable);
    m_actions.insertRowBelow->setEnabled(inTable);
    m_actions.insertColumnLeft->setEnabled(inTable);
    m_actions.insertColumnRight->setEnabled(inTable);
    m_actions.deleteRow->setEnabled(inTable && table->rows() > 1);
    m_actions.deleteColumn->setEnabled(inTable && table->columns() > 1);
    const QTextTableCell cell = inTable ? table->cellAt(cursor) : QTextTableCell();
    m_actions.splitCell->setEnabled(inTable && cell.isValid()
                                    && (cell.rowSpan() > 1 || cell.columnSpan() > 1));
    // Merge needs a neighbour to absorb.
    m_actions.mergeCells->setEnabled(inTable && cell.isValid() && cell.column() + 1 < table->columns());
}

} // namespace QindaOffice::Apps::Write
