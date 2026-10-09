// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem input: pointer handling (including the row and column
// headers), hover comments, the wheel and keyboard navigation. The editor
// itself is the QML Tk.TextField, which answers Return/Tab/Escape through
// the item's commit/cancel/step invokables.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "sheet.h"

#include <QCursor>
#include <QFontMetricsF>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {
constexpr qreal kMinCellSize = 8.0;
constexpr qreal kMaxCellSize = 2000.0;
} // namespace

// Widest rendered text in the column, clamped (the header double-click and
// Format > Optimal Width).
qreal SpreadsheetItem::autofitColumnWidth(int col) const
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr) {
        return 96.0;
    }
    const Workbook &wb = *m_document->workbook();
    const QFontMetricsF metrics(m_font);
    qreal widest = 0;
    for (const auto &[packed, cell] : sheet->cells().asKeyValueRange()) {
        if (qint32(packed >> 32) != col || isBlank(cell.value)) {
            continue;
        }
        const CellAddr addr{qint32(packed & 0xffffffffu), qint32(col)};
        widest = qMax(widest, metrics.horizontalAdvance(m_document->displayTextAt(addr)));
        Q_UNUSED(wb);
    }
    // Autofit never squeezes below the default width or runs off the screen.
    return qBound(48.0, widest + 12.0, 600.0);
}

qint32 SpreadsheetItem::columnBoundaryAt(qreal x) const
{
    for (const Band &band : visibleColumnBands()) {
        if (std::abs(x - (band.pos + band.size)) <= kGripPx) {
            return band.index;
        }
    }
    return -1;
}

qint32 SpreadsheetItem::rowBoundaryAt(qreal y) const
{
    for (const Band &band : visibleRowBands()) {
        if (std::abs(y - (band.pos + band.size)) <= kGripPx) {
            return band.index;
        }
    }
    return -1;
}

qint32 SpreadsheetItem::filterButtonAt(const QPointF &gridPos) const
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr) {
        return -1;
    }
    const AutoFilter &filter = sheet->autoFilter();
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

void SpreadsheetItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool inColumnHeader = pos.y() >= colGutterPx() && pos.y() < headerHeight() && pos.x() >= headerWidth();
    const bool inRowHeader = pos.x() >= rowGutterPx() && pos.x() < headerWidth() && pos.y() >= headerHeight();
    const Sheet *sheet = activeSheet();
    event->accept();
    if (sheet == nullptr) {
        return;
    }
    // The outline gutter (M66): level buttons and +/- buttons.
    if (pos.x() < rowGutterPx() || pos.y() < colGutterPx()) {
        cancelEditor();
        static_cast<void>(outlineClick(pos));
        return;
    }
    if (pos.x() < headerWidth() && pos.y() < headerHeight()) {
        cancelEditor();
        selectUsedRange(); // the corner box selects the sheet
        return;
    }
    if (inColumnHeader) {
        cancelEditor();
        const qint32 boundary = columnBoundaryAt(pos.x() - headerWidth());
        if (boundary >= 0 && event->button() == Qt::LeftButton) {
            m_resizeColumn = boundary;
            m_resizeOriginal = sheet->columnWidth(boundary);
            m_resizeOrigin = pos.x();
            return;
        }
        selectColumn(cellAtPos({pos.x() - headerWidth(), 0}).col, extend);
        return;
    }
    if (inRowHeader) {
        cancelEditor();
        const qint32 boundary = rowBoundaryAt(pos.y() - headerHeight());
        if (boundary >= 0 && event->button() == Qt::LeftButton) {
            m_resizeRow = boundary;
            m_resizeOriginal = sheet->rowHeight(boundary);
            m_resizeOrigin = pos.y();
            return;
        }
        selectRow(cellAtPos({0, pos.y() - headerHeight()}).row, extend);
        return;
    }
    cancelEditor();
    const QPointF gridPos = pos - QPointF(headerWidth(), headerHeight());
    // The filter dropdown sits inside a header cell: test it before the
    // cell under it takes the click.
    if (event->button() == Qt::LeftButton) {
        const qint32 filterColumn = filterButtonAt(gridPos);
        if (filterColumn >= 0) {
            emit filterButtonClicked(filterColumn, pos.x(), pos.y());
            return;
        }
    }
    const CellAddr hit = cellAtPos(gridPos);
    // Ctrl+click follows a link (M66); a plain click selects, as always.
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) != 0 && hasLink(hit)) {
        setCursorCell(hit, false);
        emit linkActivated(int(hit.row), int(hit.col));
        return;
    }
    if (event->button() == Qt::RightButton) {
        // A right-click inside the current selection keeps it: acting on a
        // multi-cell selection is the whole point of the context menu.
        if (!selectionRange().contains(hit)) {
            setCursorCell(hit, false);
        }
        emit contextMenuRequested(pos.x(), pos.y());
        return;
    }
    setCursorCell(hit, extend);
}

void SpreadsheetItem::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    Sheet *sheet = m_document != nullptr && m_document->workbook()->sheetCount() > 0
        ? &m_document->workbook()->sheetAt(m_document->activeSheet())
        : nullptr;
    event->accept();
    if (sheet == nullptr) {
        return;
    }
    if (m_resizeColumn >= 0) {
        const qreal width = qBound(kMinCellSize, m_resizeOriginal + (pos.x() - m_resizeOrigin) / m_zoom,
                                   kMaxCellSize);
        // Live feedback writes straight to the sheet; the undoable step is
        // pushed on release from the ORIGINAL width.
        sheet->setColumnWidth(m_resizeColumn, width);
        updateScrollRanges();
        update();
        return;
    }
    if (m_resizeRow >= 0) {
        const qreal height = qBound(kMinCellSize, m_resizeOriginal + (pos.y() - m_resizeOrigin) / m_zoom,
                                    kMaxCellSize);
        sheet->setRowHeight(m_resizeRow, height);
        updateScrollRanges();
        update();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && pos.x() >= headerWidth() && pos.y() >= headerHeight()) {
        setCursorCell(cellAtPos(pos - QPointF(headerWidth(), headerHeight())), true);
    }
}

void SpreadsheetItem::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    Sheet *sheet = m_document != nullptr && m_document->workbook()->sheetCount() > 0
        ? &m_document->workbook()->sheetAt(m_document->activeSheet())
        : nullptr;
    if (sheet == nullptr) {
        return;
    }
    if (m_resizeColumn >= 0) {
        const qreal applied = sheet->columnWidth(m_resizeColumn);
        // AGENT-GUARD: put the original size back before reporting the new
        // one — the undo snapshot the host is about to take would otherwise
        // already contain the change and Ctrl+Z would do nothing.
        sheet->setColumnWidth(m_resizeColumn, m_resizeOriginal);
        const qint32 col = m_resizeColumn;
        m_resizeColumn = -1;
        emit columnWidthChanged(col, applied);
        return;
    }
    if (m_resizeRow >= 0) {
        const qreal applied = sheet->rowHeight(m_resizeRow);
        sheet->setRowHeight(m_resizeRow, m_resizeOriginal);
        const qint32 row = m_resizeRow;
        m_resizeRow = -1;
        emit rowHeightChanged(row, applied);
        return;
    }
}

void SpreadsheetItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    event->accept();
    if (pos.y() < headerHeight() && pos.x() >= headerWidth()) {
        const qint32 boundary = columnBoundaryAt(pos.x() - headerWidth());
        if (boundary >= 0) {
            emit columnWidthChanged(boundary, autofitColumnWidth(boundary));
        }
        return;
    }
    if (pos.x() >= headerWidth() && pos.y() >= headerHeight()) {
        setCursorCell(cellAtPos(pos - QPointF(headerWidth(), headerHeight())));
        startEditor();
    }
}

void SpreadsheetItem::hoverMoveEvent(QHoverEvent *event)
{
    const QPointF pos = event->position();
    // Resize affordance: the pointer says where the grab band is.
    if (pos.y() < headerHeight() && pos.x() >= headerWidth()) {
        setCursor(QCursor(columnBoundaryAt(pos.x() - headerWidth()) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor));
    } else if (pos.x() < headerWidth() && pos.y() >= headerHeight()) {
        setCursor(QCursor(rowBoundaryAt(pos.y() - headerHeight()) >= 0 ? Qt::SplitVCursor : Qt::ArrowCursor));
    } else {
        setCursor(QCursor(Qt::ArrowCursor));
    }
    // Cell comments as a hover tooltip (the QML shows a Tk.ToolTip).
    QString comment;
    const Sheet *sheet = activeSheet();
    if (sheet != nullptr && pos.x() >= headerWidth() && pos.y() >= headerHeight()) {
        const CellAddr addr = cellAtPos(pos - QPointF(headerWidth(), headerHeight()));
        if (const CellComment *found = sheet->commentAt(addr)) {
            comment = found->author.isEmpty()
                ? found->text
                : QStringLiteral("%1:\n%2").arg(found->author, found->text);
        } else {
            comment = linkTip(addr); // where a link goes (M66)
        }
    }
    if (comment != m_hoverComment || (!comment.isEmpty() && pos != m_hoverPoint)) {
        m_hoverComment = comment;
        m_hoverPoint = pos;
        emit hoverChanged();
    }
}

void SpreadsheetItem::hoverLeaveEvent(QHoverEvent *event)
{
    Q_UNUSED(event);
    if (!m_hoverComment.isEmpty()) {
        m_hoverComment.clear();
        emit hoverChanged();
    }
}

void SpreadsheetItem::wheelEvent(QWheelEvent *event)
{
    // Ctrl+wheel zooms (View ▸ Zoom does the same), Shift+wheel scrolls
    // sideways, a plain wheel scrolls rows. Pixel deltas from a touchpad
    // win over the angle steps.
    const QPoint pixels = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = angle.y() / 120;
        if (steps != 0) {
            setZoom(m_zoom * (steps > 0 ? 1.25 : 0.8));
        }
        event->accept();
        return;
    }
    qreal dx = 0;
    qreal dy = 0;
    if (!pixels.isNull()) {
        dx = -pixels.x();
        dy = -pixels.y();
    } else {
        dy = -(angle.y() / 120.0) * kDefaultRowPx * 3 * m_zoom;
        dx = -(angle.x() / 120.0) * 96.0 * m_zoom;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        std::swap(dx, dy);
    }
    setScrollX(m_scrollX + dx);
    setScrollY(m_scrollY + dy);
    event->accept();
}

void SpreadsheetItem::keyPressEvent(QKeyEvent *event)
{
    if (m_editing || m_document == nullptr) {
        // The QML field has focus while editing; nothing arrives here.
        event->ignore();
        return;
    }
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool control = (event->modifiers() & Qt::ControlModifier) != 0;
    const CellRange used = usedRange();
    event->accept();
    switch (event->key()) {
    // Arrows step over hidden rows and columns, as in Excel (M66).
    case Qt::Key_Left:
    case Qt::Key_Backtab:
        setCursorCell(CellAddr{m_cursor.row, visibleStep(m_cursor.col, -1, false)}, extend);
        return;
    case Qt::Key_Right:
    case Qt::Key_Tab:
        setCursorCell(CellAddr{m_cursor.row, visibleStep(m_cursor.col, 1, false)}, extend);
        return;
    case Qt::Key_Up:
        setCursorCell(CellAddr{visibleStep(m_cursor.row, -1, true), m_cursor.col}, extend);
        return;
    case Qt::Key_Down:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        setCursorCell(CellAddr{visibleStep(m_cursor.row, 1, true), m_cursor.col}, extend);
        return;
    case Qt::Key_PageDown:
        setCursorCell(CellAddr{m_cursor.row + 20, m_cursor.col}, extend);
        return;
    case Qt::Key_PageUp:
        setCursorCell(CellAddr{m_cursor.row - 20, m_cursor.col}, extend);
        return;
    case Qt::Key_Home:
        setCursorCell(control ? CellAddr{0, 0} : CellAddr{m_cursor.row, 0}, extend);
        return;
    case Qt::Key_End:
        // Ctrl+End is "the far corner of the data".
        setCursorCell(control && used.isValid()
                          ? used.bottomRight
                          : CellAddr{m_cursor.row, used.isValid() ? used.bottomRight.col : m_cursor.col},
                      extend);
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        // AGENT-GUARD: the item never mutates the document; the host owns
        // the undo step.
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
        startEditorWith(text);
        return;
    }
    event->ignore();
}

} // namespace QindaOffice::Apps::QindaCalc
