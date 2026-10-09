// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem's panes (M82, D-091). Along each axis the cell area is one
// pane, or two: the frozen lines and the scrolling rest (Freeze Panes,
// document state), or the two sides of a Split (View ▸ Split, the window's
// own view state). Painting, hit-testing and scrolling all go through
// panes(): a split's four panes show any part of the sheet each, so a line
// may be on screen twice and its position depends on the pane it is
// measured in (paneIndexFor).
//
// AGENT-GUARD: Freeze Panes wins over a split — a frozen sheet ignores its
// split (CalcHost clears one before setting the other). A split is kept per
// sheet NAME while the window lives; it is never saved and never undone.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "sheet.h"

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {
// A split bar dragged closer than this to an edge of the cell area goes
// away, as Excel's does.
constexpr qreal kMinSplitPane = 24.0;
} // namespace

QVector<SpreadsheetItem::AxisPane> SpreadsheetItem::panes(bool rows) const
{
    const qreal extent = rows ? viewportHeight() : viewportWidth();
    const qint32 frozenLines = rows ? frozen().row : frozen().col;
    if (frozenLines > 0) {
        // The frozen lines' extent as the bands lay them out (a filtered-out
        // frozen row takes no space).
        qreal size = 0;
        for (qint32 line = 0; line < frozenLines; ++line) {
            size += lineSize(line, rows);
        }
        return {AxisPane{0, qMin(size, extent), 0, frozenLines, 0, false},
                AxisPane{size, extent, frozenLines, -1, rows ? m_scrollY : m_scrollX, true}};
    }
    const qreal split = rows ? m_split.y : m_split.x;
    if (split > 0) {
        return {AxisPane{0, qMin(split, extent), 0, -1, rows ? m_split.scrollY : m_split.scrollX, true},
                AxisPane{split + kSplitBar, extent, 0, -1, rows ? m_scrollY : m_scrollX, true}};
    }
    return {AxisPane{0, extent, 0, -1, rows ? m_scrollY : m_scrollX, true}};
}

qreal SpreadsheetItem::lineSize(qint32 line, bool rows) const
{
    if (!rows) {
        return colWidthPx(line);
    }
    const Sheet *sheet = activeSheet();
    return sheet != nullptr && sheet->filteredOutRows().contains(line) ? 0.0 : rowHeightPx(line);
}

QVector<SpreadsheetItem::Band> SpreadsheetItem::paneBands(const AxisPane &pane, bool rows) const
{
    QVector<Band> bands;
    // AGENT-GUARD: a filtered-out row has NO band — painting, headers,
    // hit-testing and scroll-into-view all walk these lists.
    const Sheet *sheet = activeSheet();
    const QSet<qint32> hidden = rows && sheet != nullptr ? sheet->filteredOutRows() : QSet<qint32>();
    const auto size = [&](qint32 line) { return rows ? visibleRowHeightPx(line, hidden) : colWidthPx(line); };
    const qint32 last = pane.limit >= 0 ? pane.limit : (rows ? Sheet::kMaxRows : Sheet::kMaxCols);
    qint32 line = pane.base;
    qreal before = 0;
    while (line < last - 1 && before + size(line) <= pane.scroll) {
        before += size(line);
        ++line;
    }
    qreal pos = pane.start + before - pane.scroll;
    while (pos < pane.end && line < last) {
        const qreal s = size(line);
        if (s > 0) {
            bands.append({line, pos, s});
        }
        pos += s;
        ++line;
    }
    return bands;
}

QVector<SpreadsheetItem::Band> SpreadsheetItem::visibleRowBands() const
{
    QVector<Band> bands;
    for (const AxisPane &pane : panes(true)) {
        bands += paneBands(pane, true);
    }
    return bands;
}

QVector<SpreadsheetItem::Band> SpreadsheetItem::visibleColumnBands() const
{
    QVector<Band> bands;
    for (const AxisPane &pane : panes(false)) {
        bands += paneBands(pane, false);
    }
    return bands;
}

int SpreadsheetItem::paneIndexFor(qint32 line, bool rows) const
{
    const qint32 frozenLines = rows ? frozen().row : frozen().col;
    if (frozenLines > 0) {
        return line < frozenLines ? 0 : 1;
    }
    if ((rows ? m_split.y : m_split.x) <= 0) {
        return 0;
    }
    const int painting = rows ? m_paintRowPane : m_paintColPane;
    return qBound(0, painting >= 0 ? painting : (rows ? m_activeRowPane : m_activeColPane), 1);
}

int SpreadsheetItem::paneIndexAt(qreal pos, bool rows) const
{
    const QVector<AxisPane> list = panes(rows);
    for (int i = 0; i + 1 < list.size(); ++i) {
        if (pos < list.at(i).end) {
            return i;
        }
    }
    return int(list.size()) - 1;
}

qreal SpreadsheetItem::lineStart(qint32 line, const AxisPane &pane, bool rows) const
{
    const Sheet *sheet = activeSheet();
    const QSet<qint32> hidden = rows && sheet != nullptr ? sheet->filteredOutRows() : QSet<qint32>();
    const auto size = [&](qint32 l) { return rows ? visibleRowHeightPx(l, hidden) : colWidthPx(l); };
    qreal offset = 0;
    for (qint32 l = pane.base; l < line; ++l) {
        offset += size(l);
    }
    for (qint32 l = line; l < pane.base; ++l) {
        offset -= size(l);
    }
    return pane.start + offset - pane.scroll;
}

int SpreadsheetItem::rowTop(qint32 row) const
{
    const QVector<AxisPane> list = panes(true);
    return int(lineStart(row, list.at(qMin<qsizetype>(paneIndexFor(row, true), list.size() - 1)), true));
}

int SpreadsheetItem::colLeft(qint32 col) const
{
    const QVector<AxisPane> list = panes(false);
    return int(lineStart(col, list.at(qMin<qsizetype>(paneIndexFor(col, false), list.size() - 1)), false));
}

void SpreadsheetItem::scrollPaneTo(int index, bool rows, qreal value)
{
    const bool splitPane = index == 0 && frozen().row == 0 && frozen().col == 0 && (rows ? m_split.y : m_split.x) > 0;
    if (splitPane) {
        rows ? setSplitScrollY(value) : setSplitScrollX(value);
    } else {
        rows ? setScrollY(value) : setScrollX(value);
    }
}

void SpreadsheetItem::scrollIntoView(const CellAddr &addr)
{
    for (const bool rows : {true, false}) {
        const QVector<AxisPane> list = panes(rows);
        const qint32 line = rows ? addr.row : addr.col;
        const int index = qMin(paneIndexFor(line, rows), int(list.size()) - 1);
        const AxisPane &pane = list.at(index);
        if (!pane.scrolls) {
            continue; // the frozen lines never scroll
        }
        const qreal start = lineStart(line, pane, rows);
        const qreal size = lineSize(line, rows);
        qreal scroll = pane.scroll;
        if (start < pane.start) {
            scroll += start - pane.start;
        } else if (start + size > pane.end) {
            scroll += start + size - pane.end;
        }
        if (!qFuzzyCompare(scroll + 1, pane.scroll + 1)) {
            scrollPaneTo(index, rows, scroll);
        }
    }
}

// ---- Split ----------------------------------------------------------------------

void SpreadsheetItem::syncSplitSheet()
{
    const QString name = m_document != nullptr && m_document->workbook()->sheetCount() > 0
        ? m_document->workbook()->sheetName(sheetIndex())
        : QString();
    if (name == m_splitSheet) {
        return;
    }
    if (!m_splitSheet.isEmpty()) {
        m_splits.insert(m_splitSheet, m_split);
    }
    m_splitSheet = name;
    m_split = m_splits.value(name);
    m_activeRowPane = m_split.y > 0 ? 1 : 0;
    m_activeColPane = m_split.x > 0 ? 1 : 0;
    emit splitChanged();
}

void SpreadsheetItem::setSplitX(qreal x)
{
    const qreal next = x < kMinSplitPane || x > viewportWidth() - kMinSplitPane - kSplitBar ? 0.0 : x;
    if (qFuzzyCompare(next + 1, m_split.x + 1)) {
        return;
    }
    if (next == 0) {
        m_scrollX = m_split.scrollX; // the left pane's view stays
        m_split.scrollX = 0;
    }
    m_split.x = next;
    m_activeColPane = next > 0 ? qMin(m_activeColPane, 1) : 0;
    m_splits.insert(m_splitSheet, m_split);
    update();
    emit splitChanged();
    emit scrollChanged();
    if (m_editing) {
        emit editingChanged();
    }
}

void SpreadsheetItem::setSplitY(qreal y)
{
    const qreal next = y < kMinSplitPane || y > viewportHeight() - kMinSplitPane - kSplitBar ? 0.0 : y;
    if (qFuzzyCompare(next + 1, m_split.y + 1)) {
        return;
    }
    if (next == 0) {
        m_scrollY = m_split.scrollY; // the top pane's view stays
        m_split.scrollY = 0;
    }
    m_split.y = next;
    m_activeRowPane = next > 0 ? qMin(m_activeRowPane, 1) : 0;
    m_splits.insert(m_splitSheet, m_split);
    update();
    emit splitChanged();
    emit scrollChanged();
    if (m_editing) {
        emit editingChanged();
    }
}

void SpreadsheetItem::setSplitScrollX(qreal value)
{
    const qreal next = qBound<qreal>(0, value, qMax<qreal>(0, m_scrollWidth - kMinSplitPane));
    if (qFuzzyCompare(next + 1, m_split.scrollX + 1)) {
        return;
    }
    m_split.scrollX = next;
    m_splits.insert(m_splitSheet, m_split);
    update();
    emit scrollChanged();
}

void SpreadsheetItem::setSplitScrollY(qreal value)
{
    const qreal next = qBound<qreal>(0, value, qMax<qreal>(0, m_scrollHeight - kMinSplitPane));
    if (qFuzzyCompare(next + 1, m_split.scrollY + 1)) {
        return;
    }
    m_split.scrollY = next;
    m_splits.insert(m_splitSheet, m_split);
    update();
    emit scrollChanged();
}

void SpreadsheetItem::toggleSplit()
{
    syncSplitSheet();
    if (isSplit()) {
        clearSplit();
        return;
    }
    if (frozen().row > 0 || frozen().col > 0) {
        return; // CalcHost unfreezes first (a frozen sheet ignores its split)
    }
    // Excel splits at the active cell's top-left corner, as the view shows
    // it; at A1 (or a corner off screen) the middle of the view.
    const QRectF cell = cellRect(m_cursor);
    const bool corner = m_cursor.row == 0 && m_cursor.col == 0;
    qreal x = m_cursor.col > 0 ? cell.left() : 0;
    qreal y = m_cursor.row > 0 ? cell.top() : 0;
    if (corner || (x > 0 && (x < kMinSplitPane || x > viewportWidth() - kMinSplitPane - kSplitBar))) {
        x = corner || m_cursor.col > 0 ? viewportWidth() / 2 : 0;
    }
    if (corner || (y > 0 && (y < kMinSplitPane || y > viewportHeight() - kMinSplitPane - kSplitBar))) {
        y = corner || m_cursor.row > 0 ? viewportHeight() / 2 : 0;
    }
    // The view does not move: the right and bottom panes go on from where
    // the left and top ones end.
    const qreal oldX = m_scrollX;
    const qreal oldY = m_scrollY;
    if (x > 0) {
        m_split.scrollX = oldX;
        m_split.x = x;
        m_scrollX = oldX + x;
    }
    if (y > 0) {
        m_split.scrollY = oldY;
        m_split.y = y;
        m_scrollY = oldY + y;
    }
    m_activeColPane = x > 0 ? 1 : 0;
    m_activeRowPane = y > 0 ? 1 : 0;
    m_splits.insert(m_splitSheet, m_split);
    updateScrollRanges();
    update();
    emit splitChanged();
    if (m_editing) {
        emit editingChanged();
    }
}

void SpreadsheetItem::clearSplit()
{
    syncSplitSheet();
    setSplitX(0);
    setSplitY(0);
}

int SpreadsheetItem::splitBarAt(const QPointF &gridPos) const
{
    if (frozen().row > 0 || frozen().col > 0) {
        return 0;
    }
    const bool vertical = m_split.x > 0 && gridPos.x() >= m_split.x - 1 && gridPos.x() <= m_split.x + kSplitBar + 1;
    const bool horizontal = m_split.y > 0 && gridPos.y() >= m_split.y - 1 && gridPos.y() <= m_split.y + kSplitBar + 1;
    return (vertical ? 1 : 0) | (horizontal ? 2 : 0);
}

} // namespace QindaOffice::Apps::QindaCalc
