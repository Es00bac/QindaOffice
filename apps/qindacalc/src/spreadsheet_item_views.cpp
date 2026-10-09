// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem's part of Excel's Custom Views (M82, D-091): what a view
// keeps of this window — the zoom, a split's bars and where each pane is
// scrolled to, as the first cell it shows — read for Add and put back for
// Show (calc_host_views.cpp). The panes are spreadsheet_item_panes.cpp's.
// And View ▸ New Window's grid: a sheet of its own, view only.
//
// AGENT-NOTE: a pane's scroll is the px past its first line (AxisPane::base:
// 0, or the first line after frozen ones), so a cell's position along an
// axis is the sum of the sizes before it — rows the filter hides take none,
// exactly as paneBands() lays them out.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "sheet.h"

#include <functional>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

// The px before `line` along an axis, from the sheet's first line.
qreal offsetOf(qint32 line, const std::function<qreal(qint32)> &size)
{
    qreal offset = 0;
    for (qint32 l = 0; l < line; ++l) {
        offset += size(l);
    }
    return offset;
}

} // namespace

qint32 SpreadsheetItem::sheetIndex() const
{
    if (m_document == nullptr || m_document->workbook()->sheetCount() == 0) {
        return -1;
    }
    if (!m_viewSheet.isEmpty()) {
        // A sheet renamed or deleted under New Window's grid: back to the
        // document's active sheet.
        const qint32 index = m_document->workbook()->sheetIndex(m_viewSheet);
        if (index >= 0) {
            return index;
        }
    }
    return m_document->activeSheet();
}

void SpreadsheetItem::setViewSheet(const QString &sheet)
{
    if (sheet == m_viewSheet) {
        return;
    }
    cancelEditor();
    m_viewSheet = sheet;
    m_anchor = {0, 0};
    m_cursor = {0, 0};
    m_scrollX = 0;
    m_scrollY = 0;
    refreshAll();
    emit viewSheetChanged();
    emit scrollChanged();
    emitSelectionChanged();
}

void SpreadsheetItem::setReadOnly(bool readOnly)
{
    if (readOnly == m_readOnly) {
        return;
    }
    cancelEditor();
    m_readOnly = readOnly;
    emit readOnlyChanged();
}

SpreadsheetItem::SheetWindow SpreadsheetItem::sheetWindow(const QString &sheet) const
{
    SheetWindow out;
    out.zoom = m_zoom;
    const bool onShow = sheet == m_splitSheet;
    const SplitState split = onShow ? m_split : m_splits.value(sheet);
    out.splitX = split.x;
    out.splitY = split.y;
    if (!onShow) {
        return out;
    }
    for (const bool rows : {true, false}) {
        const QVector<AxisPane> list = panes(rows);
        const QVector<Band> first = paneBands(list.first(), rows);
        const QVector<Band> far = paneBands(list.last(), rows);
        const qint32 a = first.isEmpty() ? 0 : first.first().index;
        const qint32 b = far.isEmpty() ? a : far.first().index;
        (rows ? out.topLeft.row : out.topLeft.col) = a;
        (rows ? out.farTopLeft.row : out.farTopLeft.col) = b;
    }
    return out;
}

void SpreadsheetItem::setSheetWindow(const QString &sheet, const SheetWindow &window)
{
    syncSplitSheet();
    if (sheet != m_splitSheet) {
        SplitState split;
        split.x = qMax<qreal>(0, window.splitX);
        split.y = qMax<qreal>(0, window.splitY);
        if (split.x > 0 || split.y > 0) {
            m_splits.insert(sheet, split);
        } else {
            m_splits.remove(sheet);
        }
        return;
    }
    setZoom(window.zoom);
    const Sheet *shown = activeSheet();
    const QSet<qint32> hidden = shown != nullptr ? shown->filteredOutRows() : QSet<qint32>();
    const auto rowSize = [&](qint32 row) { return visibleRowHeightPx(row, hidden); };
    const auto colSize = [&](qint32 col) { return colWidthPx(col); };
    const CellAddr frozenAt = frozen();
    if (frozenAt.row > 0 || frozenAt.col > 0) {
        // A frozen sheet has no split; its scrolling pane starts at the
        // first line after the frozen ones.
        clearSplit();
        setScrollY(offsetOf(qMax(window.farTopLeft.row, frozenAt.row), rowSize) - offsetOf(frozenAt.row, rowSize));
        setScrollX(offsetOf(qMax(window.farTopLeft.col, frozenAt.col), colSize) - offsetOf(frozenAt.col, colSize));
        return;
    }
    setSplitX(window.splitX);
    setSplitY(window.splitY);
    // Each split axis: the first pane at topLeft, the far one at
    // farTopLeft; an axis without a split scrolls to topLeft.
    if (m_split.y > 0) {
        setSplitScrollY(offsetOf(window.topLeft.row, rowSize));
        setScrollY(offsetOf(window.farTopLeft.row, rowSize));
    } else {
        setScrollY(offsetOf(window.topLeft.row, rowSize));
    }
    if (m_split.x > 0) {
        setSplitScrollX(offsetOf(window.topLeft.col, colSize));
        setScrollX(offsetOf(window.farTopLeft.col, colSize));
    } else {
        setScrollX(offsetOf(window.topLeft.col, colSize));
    }
    update();
}

} // namespace QindaOffice::Apps::QindaCalc
