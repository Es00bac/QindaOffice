// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetView input: pointer handling (including the row and column
// headers), the context menu and keyboard navigation. Split out of
// spreadsheet_view.cpp to keep every file under the line budget.
//
// Rationale (D-015): clicking a header did nothing at all — a header press
// fell through to QAbstractScrollArea, so a whole row or column could not be
// selected and no boundary could be dragged, and Delete did not clear a
// cell. Those are the two gestures a spreadsheet is used through.

#include "spreadsheet_view.h"

#include "spreadsheet_document.h"

#include "number_format.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QToolTip>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {
constexpr qreal kMinCellSize = 8.0;
constexpr qreal kMaxCellSize = 2000.0;
// Autofit never squeezes below the default width or runs off the screen.
constexpr qreal kAutofitMin = 48.0;
constexpr qreal kAutofitMax = 600.0;
} // namespace

qint32 SpreadsheetView::columnBoundaryAt(int x) const
{
    for (const Band &band : visibleColumnBands()) {
        if (std::abs(x - int(band.pos + band.size)) <= kGripPx) {
            return band.index;
        }
    }
    return -1;
}

qint32 SpreadsheetView::rowBoundaryAt(int y) const
{
    for (const Band &band : visibleRowBands()) {
        if (std::abs(y - int(band.pos + band.size)) <= kGripPx) {
            return band.index;
        }
    }
    return -1;
}

qreal SpreadsheetView::autofitColumnWidth(qint32 col) const
{
    const Workbook &wb = *m_document->workbook();
    const Sheet &sheet = wb.sheetAt(m_document->activeSheet());
    const QFontMetricsF metrics(font());
    qreal widest = 0;
    for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
        if (qint32(packed >> 32) != col || isBlank(cell.value)) {
            continue;
        }
        const CellAddr addr{qint32(packed & 0xffffffffu), col};
        const CellStyle *style = wb.style(sheet.styleIdAt(addr));
        const QString text =
            NumberFormat(style != nullptr && !style->numberFormat.isEmpty()
                             ? style->numberFormat
                             : QStringLiteral("General"))
                .format(cell.value)
                .text;
        widest = qMax(widest, metrics.horizontalAdvance(text));
    }
    // +padding for the cell's own 4 px insets on each side.
    return qBound(kAutofitMin, widest + 12.0, kAutofitMax);
}

qint32 SpreadsheetView::filterButtonAt(const QPoint &gridPos) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const AutoFilter &filter = sheet.autoFilter();
    if (!filter.active || !filter.range.isValid()) {
        return -1;
    }
    for (const Band &rowBand : visibleRowBands()) {
        if (rowBand.index != filter.range.topLeft.row) {
            continue;
        }
        if (gridPos.y() < rowBand.pos || gridPos.y() > rowBand.pos + rowBand.size) {
            return -1;
        }
        for (const Band &colBand : visibleColumnBands()) {
            if (colBand.index < filter.range.topLeft.col
                || colBand.index > filter.range.bottomRight.col) {
                continue;
            }
            const qreal right = colBand.pos + colBand.size;
            if (gridPos.x() >= right - kFilterButtonPx && gridPos.x() <= right) {
                return colBand.index;
            }
        }
        return -1;
    }
    return -1;
}

bool SpreadsheetView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const QPoint pos = help->pos();
        if (pos.x() >= kHeaderWidth && pos.y() >= kHeaderHeight) {
            const CellAddr addr = cellAtPos(pos - QPoint(kHeaderWidth, kHeaderHeight));
            const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
            if (const CellComment *comment = sheet.commentAt(addr)) {
                QToolTip::showText(help->globalPos(),
                                   comment->author.isEmpty()
                                       ? comment->text
                                       : QStringLiteral("%1:\n%2").arg(comment->author,
                                                                       comment->text),
                                   viewport());
                return true;
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void SpreadsheetView::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool inColumnHeader = pos.y() < kHeaderHeight && pos.x() >= kHeaderWidth;
    const bool inRowHeader = pos.x() < kHeaderWidth && pos.y() >= kHeaderHeight;

    if (pos.x() < kHeaderWidth && pos.y() < kHeaderHeight) {
        cancelEditor();
        selectUsedRange(); // the corner box selects the sheet
        return;
    }
    if (inColumnHeader) {
        cancelEditor();
        const qint32 boundary = columnBoundaryAt(pos.x() - kHeaderWidth);
        if (boundary >= 0 && event->button() == Qt::LeftButton) {
            m_resizeColumn = boundary;
            m_resizeOriginal = m_document->workbook()
                                   ->sheetAt(m_document->activeSheet())
                                   .columnWidth(boundary);
            m_resizeOrigin = pos.x();
            return;
        }
        selectColumn(cellAtPos({pos.x() - kHeaderWidth, 0}).col, extend);
        return;
    }
    if (inRowHeader) {
        cancelEditor();
        const qint32 boundary = rowBoundaryAt(pos.y() - kHeaderHeight);
        if (boundary >= 0 && event->button() == Qt::LeftButton) {
            m_resizeRow = boundary;
            m_resizeOriginal =
                m_document->workbook()->sheetAt(m_document->activeSheet()).rowHeight(boundary);
            m_resizeOrigin = pos.y();
            return;
        }
        selectRow(cellAtPos({0, pos.y() - kHeaderHeight}).row, extend);
        return;
    }
    cancelEditor();
    // The filter dropdown sits inside a header cell, so it has to be tested
    // before the cell under it takes the click.
    if (event->button() == Qt::LeftButton) {
        const qint32 filterColumn = filterButtonAt(pos - QPoint(kHeaderWidth, kHeaderHeight));
        if (filterColumn >= 0) {
            emit filterButtonClicked(filterColumn, event->globalPosition().toPoint());
            return;
        }
    }
    const CellAddr hit = cellAtPos(pos - QPoint(kHeaderWidth, kHeaderHeight));
    // A right-click inside the current selection keeps it: acting on a
    // multi-cell selection is the whole point of the context menu.
    if (event->button() == Qt::RightButton && selectionRange().contains(hit)) {
        return;
    }
    setCursorCell(hit, extend);
}

void SpreadsheetView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_resizeColumn >= 0) {
        const qreal width =
            qBound(kMinCellSize, m_resizeOriginal + (pos.x() - m_resizeOrigin) / m_zoom,
                   kMaxCellSize);
        // Live feedback writes straight to the sheet; the undoable step is
        // pushed on release from the ORIGINAL width (see mouseReleaseEvent).
        m_document->workbook()->sheetAt(m_document->activeSheet())
            .setColumnWidth(m_resizeColumn, width);
        updateScrollBars();
        viewport()->update();
        return;
    }
    if (m_resizeRow >= 0) {
        const qreal height =
            qBound(kMinCellSize, m_resizeOriginal + (pos.y() - m_resizeOrigin) / m_zoom,
                   kMaxCellSize);
        m_document->workbook()->sheetAt(m_document->activeSheet())
            .setRowHeight(m_resizeRow, height);
        updateScrollBars();
        viewport()->update();
        return;
    }
    // Resize affordance: the pointer says where the grab band is.
    if (pos.y() < kHeaderHeight && pos.x() >= kHeaderWidth) {
        setCursor(columnBoundaryAt(pos.x() - kHeaderWidth) >= 0 ? Qt::SplitHCursor
                                                                : Qt::ArrowCursor);
    } else if (pos.x() < kHeaderWidth && pos.y() >= kHeaderHeight) {
        setCursor(rowBoundaryAt(pos.y() - kHeaderHeight) >= 0 ? Qt::SplitVCursor
                                                              : Qt::ArrowCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    if ((event->buttons() & Qt::LeftButton) && pos.x() >= kHeaderWidth
        && pos.y() >= kHeaderHeight) {
        setCursorCell(cellAtPos(pos - QPoint(kHeaderWidth, kHeaderHeight)), true);
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void SpreadsheetView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizeColumn >= 0) {
        Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
        const qreal applied = sheet.columnWidth(m_resizeColumn);
        // AGENT-GUARD: put the original size back before reporting the new
        // one. The drag mutated the sheet directly for live feedback, so the
        // undo snapshot the window is about to take would otherwise already
        // contain the change and Ctrl+Z would do nothing.
        sheet.setColumnWidth(m_resizeColumn, m_resizeOriginal);
        const qint32 col = m_resizeColumn;
        m_resizeColumn = -1;
        emit columnWidthChanged(col, applied);
        return;
    }
    if (m_resizeRow >= 0) {
        Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
        const qreal applied = sheet.rowHeight(m_resizeRow);
        sheet.setRowHeight(m_resizeRow, m_resizeOriginal);
        const qint32 row = m_resizeRow;
        m_resizeRow = -1;
        emit rowHeightChanged(row, applied);
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void SpreadsheetView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (pos.y() < kHeaderHeight && pos.x() >= kHeaderWidth) {
        const qint32 boundary = columnBoundaryAt(pos.x() - kHeaderWidth);
        if (boundary >= 0) {
            emit columnWidthChanged(boundary, autofitColumnWidth(boundary));
            return;
        }
    }
    if (pos.x() >= kHeaderWidth && pos.y() >= kHeaderHeight) {
        setCursorCell(cellAtPos(pos - QPoint(kHeaderWidth, kHeaderHeight)));
        startEditor();
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void SpreadsheetView::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

bool SpreadsheetView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && m_editing && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitEditor(!(key->modifiers() & Qt::ShiftModifier));
            key->accept();
            return true;
        case Qt::Key_Tab:
            // AGENT-GUARD: intercept Return/Tab ON the editor — the QLineEdit
            // has focus while editing, so the view's keyPressEvent never sees
            // them; Return committed via editingFinished without moving and
            // Tab walked focus away (the spreadsheet's core commit-and-advance
            // convention was dead).
            commitEditor(false);
            {
                const int step = key->modifiers() & Qt::ShiftModifier ? -1 : 1;
                setCursorCell({m_cursor.row, m_cursor.col + step}, false);
            }
            key->accept();
            return true;
        default:
            break;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void SpreadsheetView::keyPressEvent(QKeyEvent *event)
{
    if (m_editing) {
        if (event->key() == Qt::Key_Escape) {
            cancelEditor();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            commitEditor(!(event->modifiers() & Qt::ShiftModifier));
            event->accept();
            return;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool control = (event->modifiers() & Qt::ControlModifier) != 0;
    const CellRange used = usedRange();
    switch (event->key()) {
    case Qt::Key_Left:
        setCursorCell({m_cursor.row, m_cursor.col - 1}, extend);
        return;
    case Qt::Key_Right:
    case Qt::Key_Tab:
        setCursorCell({m_cursor.row, m_cursor.col + 1}, extend);
        return;
    case Qt::Key_Up:
        setCursorCell({m_cursor.row - 1, m_cursor.col}, extend);
        return;
    case Qt::Key_Down:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        setCursorCell({m_cursor.row + 1, m_cursor.col}, extend);
        return;
    case Qt::Key_Backtab:
        setCursorCell({m_cursor.row, m_cursor.col - 1}, extend);
        return;
    case Qt::Key_PageDown:
        setCursorCell({m_cursor.row + 20, m_cursor.col}, extend);
        return;
    case Qt::Key_PageUp:
        setCursorCell({m_cursor.row - 20, m_cursor.col}, extend);
        return;
    case Qt::Key_Home:
        setCursorCell(control ? CellAddr{0, 0} : CellAddr{m_cursor.row, 0}, extend);
        return;
    case Qt::Key_End:
        // Ctrl+End is "the far corner of the data", the standard way to find
        // out how big a sheet really is.
        setCursorCell(control && used.isValid() ? used.bottomRight
                                                : CellAddr{m_cursor.row,
                                                           used.isValid()
                                                               ? used.bottomRight.col
                                                               : m_cursor.col},
                      extend);
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        // AGENT-GUARD: the view never mutates the document. Delete used to
        // do nothing at all here, so the only way to empty a cell was Cut.
        emit clearRequested(selectionRange());
        return;
    case Qt::Key_F2:
        startEditor();
        return;
    default:
        break;
    }
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint() && !control
        && !(event->modifiers() & Qt::AltModifier)) {
        startEditor();
        m_editor->setText(text);
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

} // namespace QindaOffice::Apps::QindaCalc
