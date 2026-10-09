// SPDX-License-Identifier: GPL-3.0-or-later
// SlideCanvasItem tables (M67, D-076): what a pointer and the keyboard do
// to the SELECTED table, as in PowerPoint — a click into a cell puts the
// caret there (the in-place editor over the cell's box, the cell's text
// look for an empty cell), a drag selects cells, Shift extends, a drag on
// a column or row boundary resizes (the right and bottom edges resize the
// last column and row, the left and top edges and a thin frame band move
// the table), Tab / Shift+Tab walk the cells (Tab in the last cell adds a
// row), Delete empties the selected cells, Enter / F2 edit, the arrows
// move the selection.
//
// AGENT-GUARD: geometry comes from show_model's tableGrid() — the grid the
// renderer paints — never from the column widths alone: rows grow to their
// text, so a hit test that ignored it would land a click on the wrong row.

#include "slide_canvas_item.h"

#include "text_bridge.h"

#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QTextDocument>

namespace QindaOffice::Apps::Show {

namespace {

using ShowModel::TableRange;
namespace TableEdit = ShowModel::TableEdit;

constexpr qreal kBoundaryGrip = 3.0; // slide points either side of a boundary
constexpr qreal kFrameBand = 4.0;    // the table's frame edge moves it
const QColor kCellSelection(61, 123, 204, 60);
const TableRange kNoCells{-1, -1, -1, -1};

TableRange spanning(const TableRange &a, const TableRange &b)
{
    return {qMin(a.top, b.top), qMin(a.left, b.left), qMax(a.bottom, b.bottom), qMax(a.right, b.right)};
}

// The grid cell under `pos` (clamped into the table): {row, column}.
std::pair<int, int> cellAt(const ShowModel::Shape &shape, QPointF pos)
{
    const ShowModel::TableGrid grid = ShowModel::tableGrid(shape.table, shape.rect.normalized());
    if (grid.xs.size() < 2 || grid.ys.size() < 2) {
        return {-1, -1};
    }
    pos.setX(qBound(grid.xs.first(), pos.x(), grid.xs.last() - 0.01));
    pos.setY(qBound(grid.ys.first(), pos.y(), grid.ys.last() - 0.01));
    int row = 0;
    while (row + 2 < grid.ys.size() && pos.y() >= grid.ys.at(row + 1)) {
        ++row;
    }
    int column = 0;
    while (column + 2 < grid.xs.size() && pos.x() >= grid.xs.at(column + 1)) {
        ++column;
    }
    return {row, column};
}

// The boundary under `pos`: the index of the column left of it (or the
// row above it); -1 for none. The left and top edges are not boundaries.
int boundaryAt(const ShowModel::Shape &shape, const QPointF &pos, bool *columns)
{
    const ShowModel::TableGrid grid = ShowModel::tableGrid(shape.table, shape.rect.normalized());
    if (grid.xs.size() < 2 || grid.ys.size() < 2) {
        return -1;
    }
    const bool withinY = pos.y() >= grid.ys.first() - kBoundaryGrip && pos.y() <= grid.ys.last() + kBoundaryGrip;
    const bool withinX = pos.x() >= grid.xs.first() - kBoundaryGrip && pos.x() <= grid.xs.last() + kBoundaryGrip;
    for (int c = 1; c < grid.xs.size() && withinY; ++c) {
        if (qAbs(pos.x() - grid.xs.at(c)) <= kBoundaryGrip) {
            *columns = true;
            return c - 1;
        }
    }
    for (int r = 1; r < grid.ys.size() && withinX; ++r) {
        if (qAbs(pos.y() - grid.ys.at(r)) <= kBoundaryGrip) {
            *columns = false;
            return r - 1;
        }
    }
    return -1;
}

bool isAnchor(const ShowModel::Table &table, int row, int column)
{
    const ShowModel::TableCell &cell = table.rows.at(row).cells.at(column);
    return !cell.hMerge && !cell.vMerge;
}

bool validCell(const ShowModel::Table &table, int row, int column)
{
    return row >= 0 && column >= 0 && row < table.rows.size() && column < table.rows.at(row).cells.size();
}

} // namespace

QString SlideCanvasItem::tableShapeId() const
{
    if (m_slide == nullptr || m_selected.size() != 1) {
        return {};
    }
    const ShowModel::Shape *shape = m_slide->findShape(*m_selected.cbegin());
    return shape != nullptr && shape->kind == ShowModel::ShapeKind::Table && !shape->table.isEmpty() ? shape->id : QString();
}

ShowModel::TableRange SlideCanvasItem::cellSelection() const
{
    return !m_tableId.isEmpty() && m_tableId == tableShapeId() ? m_cells : kNoCells;
}

void SlideCanvasItem::selectCells(const QString &shapeId, ShowModel::TableRange range)
{
    const ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(shapeId) : nullptr;
    if (shape == nullptr || shape->kind != ShowModel::ShapeKind::Table || shape->table.isEmpty()) {
        return;
    }
    const ShowModel::Table &table = shape->table;
    range.top = qBound(0, range.top, int(table.rows.size()) - 1);
    range.bottom = qBound(range.top, range.bottom, int(table.rows.size()) - 1);
    range.left = qBound(0, range.left, int(table.columns.size()) - 1);
    range.right = qBound(range.left, range.right, int(table.columns.size()) - 1);
    m_tableId = shapeId;
    m_cells = TableEdit::expandToBlocks(table, range);
    m_cellAnchor = TableEdit::blockAt(table, m_cells.top, m_cells.left);
    m_selected = {shapeId};
    notifySelection();
    emit cellSelectionChanged();
}

void SlideCanvasItem::dropCellSelectionIfGone()
{
    const ShowModel::Shape *shape = m_slide != nullptr && !m_tableId.isEmpty() ? m_slide->findShape(m_tableId) : nullptr;
    if (shape == nullptr || !m_selected.contains(m_tableId) || shape->kind != ShowModel::ShapeKind::Table) {
        const bool had = m_cells.isValid();
        m_tableId.clear();
        m_cells = kNoCells;
        m_cellAnchor = kNoCells;
        if (had) {
            emit cellSelectionChanged();
        }
        return;
    }
    if (m_cells.isValid() && (m_cells.bottom >= shape->table.rows.size() || m_cells.right >= shape->table.columns.size())) {
        m_cells = TableEdit::expandToBlocks(shape->table, {qMin(m_cells.top, int(shape->table.rows.size()) - 1),
                                                           qMin(m_cells.left, int(shape->table.columns.size()) - 1),
                                                           qMin(m_cells.bottom, int(shape->table.rows.size()) - 1),
                                                           qMin(m_cells.right, int(shape->table.columns.size()) - 1)});
        m_cellAnchor = m_cells;
        emit cellSelectionChanged();
    }
}

void SlideCanvasItem::beginCellEdit(const QString &shapeId, int row, int column)
{
    commitTextEdit(false);
    ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(shapeId) : nullptr;
    if (shape == nullptr || shape->kind != ShowModel::ShapeKind::Table || !validCell(shape->table, row, column)) {
        return;
    }
    const TableRange block = TableEdit::blockAt(shape->table, row, column);
    if (!validCell(shape->table, block.top, block.left)) {
        return;
    }
    const ShowModel::TableCell &cell = shape->table.rows.at(block.top).cells.at(block.left);
    m_selected = {shape->id};
    m_tableId = shape->id;
    m_cells = block;
    m_cellAnchor = block;
    m_textShapeId = shape->id;
    m_textRow = block.top;
    m_textColumn = block.left;
    m_textOriginal = cell.paragraphs;
    const ShowModel::TextRun look =
        TableEdit::textLook(shape->table, block.top, block.left, m_tableTextLook ? m_tableTextLook() : ShowModel::TextRun());
    QList<ShowModel::TextParagraph> paragraphs = cell.paragraphs;
    if (paragraphs.isEmpty()) {
        paragraphs.append(ShowModel::TextParagraph());
    }
    m_textDocument = documentFromParagraphs(paragraphs, this, &look);
    m_textDocument->setTextWidth(qMax<qreal>(editorRect().width(), 20));
    notifySelection();
    emit editingChanged();
    emit cellSelectionChanged();
}

void SlideCanvasItem::commitCellEdit(bool cancelled)
{
    const QString shapeId = m_textShapeId;
    const int row = m_textRow;
    const int column = m_textColumn;
    const QList<ShowModel::TextParagraph> committed = paragraphsFromDocument(*m_textDocument);
    m_textDocument->deleteLater();
    m_textDocument = nullptr;
    m_textShapeId.clear();
    m_textOriginal.clear();
    m_textRow = -1;
    m_textColumn = -1;
    emit editingChanged();
    ShowModel::Shape *shape = m_slide != nullptr && !cancelled ? m_slide->findShape(shapeId) : nullptr;
    if (shape != nullptr && shape->kind == ShowModel::ShapeKind::Table && validCell(shape->table, row, column)
        && shape->table.rows.at(row).cells.at(column).paragraphs != committed) {
        const ShowModel::Shape before = *shape;
        shape->table.rows[row].cells[column].paragraphs = committed;
        TableEdit::fitFrame(*shape);
        emit shapeEdited(shapeId, before, *shape, tr("Edit cell"));
        emit slideEdited();
    }
    update();
}

bool SlideCanvasItem::moveCellEdit(int step)
{
    if (m_textRow < 0 || step == 0) {
        return false;
    }
    const QString id = m_textShapeId;
    const int row = m_textRow;
    const int column = m_textColumn;
    commitTextEdit(false);
    const ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(id) : nullptr;
    if (shape == nullptr) {
        return true;
    }
    const ShowModel::Table &table = shape->table;
    const int columns = int(table.columns.size());
    const int count = int(table.rows.size()) * columns;
    for (int i = row * columns + column + step; i >= 0 && i < count; i += step) {
        if (validCell(table, i / columns, i % columns) && isAnchor(table, i / columns, i % columns)) {
            beginCellEdit(id, i / columns, i % columns);
            return true;
        }
    }
    if (step > 0) {
        // Tab in the last cell: a new row like the last one, as in PowerPoint.
        const int rows = int(table.rows.size());
        editShape(id, tr("Insert row"), [rows](ShowModel::Shape &s) { return TableEdit::insertRows(s.table, rows, 1, rows - 1); });
        beginCellEdit(id, rows, 0);
    } else {
        beginCellEdit(id, row, column);
    }
    return true;
}

bool SlideCanvasItem::editShape(const QString &shapeId, const QString &label,
                                const std::function<bool(ShowModel::Shape &)> &mutate)
{
    commitTextEdit(false);
    ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(shapeId) : nullptr;
    if (shape == nullptr) {
        return false;
    }
    const ShowModel::Shape before = *shape;
    if (!mutate(*shape)) {
        *shape = before;
        return false;
    }
    TableEdit::fitFrame(*shape);
    if (*shape == before) {
        *shape = before;
        return false;
    }
    dropCellSelectionIfGone();
    emit shapeEdited(shapeId, before, *shape, label);
    emit slideEdited();
    update();
    return true;
}

QRectF SlideCanvasItem::cellEditorRect(const ShowModel::Shape &shape) const
{
    if (!validCell(shape.table, m_textRow, m_textColumn)) {
        return {};
    }
    const ShowModel::TableGrid grid = ShowModel::tableGrid(shape.table, shape.rect.normalized());
    const TableRange block = TableEdit::blockAt(shape.table, m_textRow, m_textColumn);
    const ShowModel::TableCell &cell = shape.table.rows.at(m_textRow).cells.at(m_textColumn);
    return grid.cellRect(block.top, block.left, block.rows(), block.columns())
        .adjusted(cell.marginLeft, cell.marginTop, -cell.marginRight, -cell.marginBottom);
}

ShowModel::Shape SlideCanvasItem::paintedWhileEditing(const ShowModel::Shape &shape) const
{
    // The cell being edited keeps its layout (the row keeps its height)
    // but not its ink: the editor draws the text over it.
    ShowModel::Shape painted = shape;
    if (validCell(painted.table, m_textRow, m_textColumn)) {
        for (ShowModel::TextParagraph &paragraph : painted.table.rows[m_textRow].cells[m_textColumn].paragraphs) {
            paragraph.bulletColor = Qt::transparent;
            for (ShowModel::TextRun &run : paragraph.runs) {
                run.color = Qt::transparent;
                run.highlight = QColor();
                run.underline.clear();
                run.strike.clear();
            }
        }
    }
    return painted;
}

void SlideCanvasItem::paintTableSelection(QPainter *painter) const
{
    const TableRange cells = cellSelection();
    const ShowModel::Shape *shape = cells.isValid() && m_textRow < 0 ? m_slide->findShape(m_tableId) : nullptr;
    if (shape == nullptr) {
        return;
    }
    const ShowModel::TableGrid grid = ShowModel::tableGrid(shape->table, shape->rect.normalized());
    painter->fillRect(grid.cellRect(cells.top, cells.left, cells.rows(), cells.columns()), kCellSelection);
}

bool SlideCanvasItem::tablePress(const QPointF &pos, Qt::KeyboardModifiers modifiers)
{
    const QString id = tableShapeId();
    ShowModel::Shape *shape = id.isEmpty() ? nullptr : m_slide->findShape(id);
    if (shape == nullptr
        || !shape->rect.normalized().adjusted(-kBoundaryGrip, -kBoundaryGrip, kBoundaryGrip, kBoundaryGrip).contains(pos)) {
        return false;
    }
    m_pressPos = pos;
    m_moved = false;
    m_draggingShapeId = id;
    bool columns = false;
    const int boundary = boundaryAt(*shape, pos, &columns);
    if (boundary >= 0) {
        m_gesture = columns ? Gesture::ColumnBoundary : Gesture::RowBoundary;
        m_boundary = boundary;
        m_tableBefore = *shape;
        return true;
    }
    if (!shape->rect.normalized().adjusted(kFrameBand, kFrameBand, -kFrameBand, -kFrameBand).contains(pos)) {
        m_draggingShapeId.clear();
        return false; // the frame's edge: the table moves
    }
    const auto [row, column] = cellAt(*shape, pos);
    if (row < 0) {
        m_draggingShapeId.clear();
        return false;
    }
    const TableRange block = TableEdit::blockAt(shape->table, row, column);
    if ((modifiers & Qt::ShiftModifier) && m_tableId == id && m_cellAnchor.isValid()) {
        m_cells = TableEdit::expandToBlocks(shape->table, spanning(m_cellAnchor, block));
    } else {
        m_cellAnchor = block;
        m_cells = block;
    }
    m_tableId = id;
    m_gesture = Gesture::Cells;
    emit cellSelectionChanged();
    update();
    return true;
}

bool SlideCanvasItem::tableMove(const QPointF &pos)
{
    if (m_gesture != Gesture::Cells && m_gesture != Gesture::ColumnBoundary && m_gesture != Gesture::RowBoundary) {
        return false;
    }
    ShowModel::Shape *shape = m_slide->findShape(m_draggingShapeId);
    const QPointF delta = pos - m_pressPos;
    if (shape == nullptr || (!m_moved && delta.manhattanLength() < 2)) {
        return true;
    }
    m_moved = true;
    if (m_gesture == Gesture::Cells) {
        const auto [row, column] = cellAt(*shape, pos);
        if (row >= 0) {
            m_cells = TableEdit::expandToBlocks(shape->table, spanning(m_cellAnchor, TableEdit::blockAt(shape->table, row, column)));
            emit cellSelectionChanged();
        }
    } else {
        ShowModel::Table table = m_tableBefore.table;
        if (m_gesture == Gesture::ColumnBoundary) {
            TableEdit::moveColumnBoundary(table, m_boundary, delta.x());
        } else {
            // From the height the row is drawn at (text may have grown it).
            const ShowModel::TableGrid grid = ShowModel::tableGrid(table, m_tableBefore.rect.normalized());
            TableEdit::setRowHeight(table, m_boundary, grid.ys.at(m_boundary + 1) - grid.ys.at(m_boundary) + delta.y());
        }
        shape->table = table;
        TableEdit::fitFrame(*shape);
    }
    update();
    return true;
}

bool SlideCanvasItem::tableRelease()
{
    if (m_gesture != Gesture::Cells && m_gesture != Gesture::ColumnBoundary && m_gesture != Gesture::RowBoundary) {
        return false;
    }
    const Gesture gesture = m_gesture;
    const QString id = m_draggingShapeId;
    m_gesture = Gesture::None;
    m_draggingShapeId.clear();
    ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(id) : nullptr;
    if (shape != nullptr && gesture == Gesture::Cells && !m_moved && m_cells == m_cellAnchor) {
        beginCellEdit(id, m_cells.top, m_cells.left); // a click into a cell edits it
    } else if (shape != nullptr && gesture != Gesture::Cells && m_moved && !(*shape == m_tableBefore)) {
        emit shapeEdited(id, m_tableBefore, *shape, gesture == Gesture::ColumnBoundary ? tr("Column width") : tr("Row height"));
        emit slideEdited();
    }
    m_tableBefore = ShowModel::Shape();
    update();
    return true;
}

bool SlideCanvasItem::tableKey(QKeyEvent *event)
{
    const TableRange cells = cellSelection();
    const ShowModel::Shape *shape = cells.isValid() ? m_slide->findShape(m_tableId) : nullptr;
    if (shape == nullptr) {
        return false;
    }
    const QString id = m_tableId;
    const ShowModel::Table &table = shape->table;
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
        // Emptying the selected cells; the table goes only with its frame.
        editShape(id, tr("Clear cells"), [cells](ShowModel::Shape &s) {
            for (int r = cells.top; r <= cells.bottom; ++r) {
                for (int c = cells.left; c <= cells.right && validCell(s.table, r, c); ++c) {
                    QList<ShowModel::TextParagraph> &paragraphs = s.table.rows[r].cells[c].paragraphs;
                    for (ShowModel::TextParagraph &paragraph : paragraphs) {
                        paragraph.runs.clear();
                    }
                    if (paragraphs.size() > 1) {
                        paragraphs.resize(1);
                    }
                }
            }
            return true;
        });
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F2:
        beginCellEdit(id, cells.top, cells.left);
        return true;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        beginCellEdit(id, cells.top, cells.left);
        return moveCellEdit(event->key() == Qt::Key_Tab ? 1 : -1);
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int dr = event->key() == Qt::Key_Down ? 1 : event->key() == Qt::Key_Up ? -1 : 0;
        const int dc = event->key() == Qt::Key_Right ? 1 : event->key() == Qt::Key_Left ? -1 : 0;
        const int row = qBound(0, (dr > 0 ? cells.bottom : cells.top) + dr, int(table.rows.size()) - 1);
        const int column = qBound(0, (dc > 0 ? cells.right : cells.left) + dc, int(table.columns.size()) - 1);
        const TableRange block = TableEdit::blockAt(table, row, column);
        m_cellAnchor = block;
        m_cells = block;
        emit cellSelectionChanged();
        update();
        return true;
    }
    default:
        return false;
    }
}

void SlideCanvasItem::hoverMoveEvent(QHoverEvent *event)
{
    const QString id = tableShapeId();
    const ShowModel::Shape *shape = id.isEmpty() ? nullptr : m_slide->findShape(id);
    bool columns = false;
    if (shape != nullptr && boundaryAt(*shape, event->position(), &columns) >= 0) {
        setCursor(QCursor(columns ? Qt::SplitHCursor : Qt::SplitVCursor));
    } else {
        unsetCursor();
    }
    QQuickPaintedItem::hoverMoveEvent(event);
}

} // namespace QindaOffice::Apps::Show
