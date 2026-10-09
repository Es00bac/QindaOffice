// SPDX-License-Identifier: GPL-3.0-or-later
#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "sheet.h"


namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

SpreadsheetItem::SpreadsheetItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, false);
    setFlag(ItemHasContents, true);
    setActiveFocusOnTab(true);
    setOpaquePainting(true);
    m_font.setPointSizeF(10.0);
}

void SpreadsheetItem::setDocument(SpreadsheetDocument *document)
{
    m_document = document;
    m_anchor = {0, 0};
    m_cursor = {0, 0};
    m_scrollX = 0;
    m_scrollY = 0;
    m_splits.clear();
    m_split = SplitState();
    m_splitSheet.clear();
    refreshAll();
    emit cursorChanged();
    emitSelectionChanged();
}

const Sheet *SpreadsheetItem::activeSheet() const
{
    if (m_document == nullptr || m_document->workbook()->sheetCount() == 0) {
        return nullptr;
    }
    return &m_document->workbook()->sheetAt(sheetIndex());
}

void SpreadsheetItem::refreshAll()
{
    syncSplitSheet(); // M82: the sheet on show may have changed
    updateScrollRanges();
    update();
    emit cursorChanged(); // the formula text may have changed under the cursor
}

// ---- cursor and selection ---------------------------------------------------

QString SpreadsheetItem::cursorText() const
{
    return addrText(m_cursor);
}

QString SpreadsheetItem::selectionText() const
{
    const CellRange range = selectionRange();
    if (range.cellCount() > 1) {
        return QStringLiteral("%1:%2").arg(addrText(range.topLeft), addrText(range.bottomRight));
    }
    return addrText(m_cursor);
}

QString SpreadsheetItem::formulaText() const
{
    return inputTextAt(m_cursor);
}

QString SpreadsheetItem::inputTextAt(const CellAddr &addr) const
{
    return m_document != nullptr ? m_document->inputTextAt(addr) : QString();
}

void SpreadsheetItem::setCursorCell(int row, int col, bool extendSelection)
{
    setCursorCell(CellAddr{qint32(row), qint32(col)}, extendSelection);
}

void SpreadsheetItem::setCursorCell(const CellAddr &addr, bool extendSelection)
{
    const CellAddr next{qBound(qint32(0), addr.row, Sheet::kMaxRows - 1),
                        qBound(qint32(0), addr.col, Sheet::kMaxCols - 1)};
    syncSplitSheet();
    m_cursor = next;
    if (!extendSelection) {
        m_anchor = next;
        m_selectionKind = CellSelection;
    }
    // Scroll into view in the pane the cursor is measured in (M82): the
    // frozen lines never scroll; under a split, the active pane does.
    scrollIntoView(m_cursor);
    update();
    emit cursorChanged();
    emitSelectionChanged();
}

CellRange SpreadsheetItem::selectionRange() const
{
    CellRange range;
    range.topLeft = {qMin(m_anchor.row, m_cursor.row), qMin(m_anchor.col, m_cursor.col)};
    range.bottomRight = {qMax(m_anchor.row, m_cursor.row), qMax(m_anchor.col, m_cursor.col)};
    return range;
}

CellRange SpreadsheetItem::usedRange() const
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr || sheet->cells().isEmpty()) {
        return {};
    }
    qint32 maxRow = 0;
    qint32 maxCol = 0;
    for (auto it = sheet->cells().constBegin(); it != sheet->cells().constEnd(); ++it) {
        maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu));
        maxCol = qMax(maxCol, qint32(it.key() >> 32));
    }
    return {{0, 0}, {maxRow, maxCol}};
}

void SpreadsheetItem::emitSelectionChanged()
{
    emit selectionChanged(selectionRange());
}

void SpreadsheetItem::selectColumn(int col, bool extendSelection)
{
    const CellRange used = usedRange();
    const qint32 lastRow = used.isValid() ? used.bottomRight.row : 0;
    m_anchor = extendSelection ? CellAddr{0, m_anchor.col} : CellAddr{0, qint32(col)};
    setCursorCell(CellAddr{lastRow, qint32(col)}, true);
    m_selectionKind = ColumnSelection;
}

void SpreadsheetItem::selectRow(int row, bool extendSelection)
{
    const CellRange used = usedRange();
    const qint32 lastCol = used.isValid() ? used.bottomRight.col : 0;
    m_anchor = extendSelection ? CellAddr{m_anchor.row, 0} : CellAddr{qint32(row), 0};
    setCursorCell(CellAddr{qint32(row), lastCol}, true);
    m_selectionKind = RowSelection;
}

void SpreadsheetItem::selectUsedRange()
{
    const CellRange used = usedRange();
    if (!used.isValid()) {
        setCursorCell(CellAddr{0, 0});
        return;
    }
    m_anchor = used.topLeft;
    setCursorCell(used.bottomRight, true);
}

// ---- geometry ----------------------------------------------------------------

qreal SpreadsheetItem::viewportWidth() const
{
    return qMax<qreal>(0, width() - headerWidth());
}

qreal SpreadsheetItem::viewportHeight() const
{
    return qMax<qreal>(0, height() - headerHeight());
}

qreal SpreadsheetItem::frozenHeightPx() const
{
    qreal y = 0;
    for (qint32 r = 0; r < frozen().row; ++r) {
        y += rowHeightPx(r);
    }
    return y;
}

qreal SpreadsheetItem::frozenWidthPx() const
{
    qreal x = 0;
    for (qint32 c = 0; c < frozen().col; ++c) {
        x += colWidthPx(c);
    }
    return x;
}

QFont SpreadsheetItem::scaledFont() const
{
    QFont scaled = m_font;
    const qreal points = scaled.pointSizeF() > 0 ? scaled.pointSizeF() : 10.0;
    scaled.setPointSizeF(qMax(4.0, points * m_zoom));
    return scaled;
}

// A hidden row or column (Format ▸ Hide, a collapsed group — M66) is
// zero-sized, so it has no band and nothing paints or hits it.
qreal SpreadsheetItem::colWidthPx(qint32 col) const
{
    const Sheet *sheet = activeSheet();
    if (sheet != nullptr && sheet->isColumnHidden(col)) {
        return 0.0;
    }
    return (sheet != nullptr ? sheet->columnWidth(col) : 96.0) * m_zoom;
}

qreal SpreadsheetItem::rowHeightPx(qint32 row) const
{
    const Sheet *sheet = activeSheet();
    if (sheet != nullptr && sheet->isRowHidden(row)) {
        return 0.0;
    }
    return (sheet != nullptr ? sheet->rowHeight(row) : kDefaultRowPx) * m_zoom;
}

qreal SpreadsheetItem::visibleRowHeightPx(qint32 row, const QSet<qint32> &hidden) const
{
    return hidden.contains(row) ? 0.0 : rowHeightPx(row);
}

QRectF SpreadsheetItem::cellRect(const CellAddr &addr) const
{
    return {qreal(colLeft(addr.col)), qreal(rowTop(addr.row)), colWidthPx(addr.col),
            rowHeightPx(addr.row)};
}

QRectF SpreadsheetItem::cursorItemRect() const
{
    return cellRect(m_cursor).translated(headerWidth(), headerHeight());
}

CellAddr SpreadsheetItem::cellAtPos(const QPointF &pos) const
{
    // Hit-testing walks the SAME band lists painting does, of the pane
    // under the point (frozen panes; M82: a split's four panes).
    const QVector<AxisPane> rowList = panes(true);
    const QVector<AxisPane> colList = panes(false);
    const QVector<Band> rows = paneBands(rowList.at(paneIndexAt(pos.y(), true)), true);
    const QVector<Band> cols = paneBands(colList.at(paneIndexAt(pos.x(), false)), false);
    qint32 row = rows.isEmpty() ? 0 : rows.last().index;
    for (const Band &band : rows) {
        if (pos.y() < band.pos + band.size) {
            row = band.index;
            break;
        }
    }
    qint32 col = cols.isEmpty() ? 0 : cols.last().index;
    for (const Band &band : cols) {
        if (pos.x() < band.pos + band.size) {
            col = band.index;
            break;
        }
    }
    return {qMax(qint32(0), row), qMax(qint32(0), col)};
}

void SpreadsheetItem::updateScrollRanges()
{
    const Sheet *sheet = activeSheet();
    // Default-size estimate keeps the scroll bars stable for sparse sheets;
    // grows as content extends.
    qint32 maxRow = 200;
    qint32 maxCol = 40;
    if (sheet != nullptr) {
        for (auto it = sheet->cells().constBegin(); it != sheet->cells().constEnd(); ++it) {
            maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu) + 20);
            maxCol = qMax(maxCol, qint32(it.key() >> 32) + 5);
        }
    }
    qreal totalH = 0;
    for (qint32 r = 0; r < maxRow; ++r) {
        totalH += rowHeightPx(r);
    }
    qreal totalW = 0;
    for (qint32 c = 0; c < maxCol; ++c) {
        totalW += colWidthPx(c);
    }
    // The frozen band is always on screen: it belongs to neither the
    // scrollable content nor the space available to it.
    m_scrollHeight = qMax<qreal>(0, totalH - frozenHeightPx());
    m_scrollWidth = qMax<qreal>(0, totalW - frozenWidthPx());
    m_scrollX = qBound<qreal>(0, m_scrollX, maxScroll(false));
    m_scrollY = qBound<qreal>(0, m_scrollY, maxScroll(true));
    emit scrollChanged();
}

// How far the scrolling pane (the bottom / right one of a split) goes: the
// content past the frozen lines less what the pane shows.
qreal SpreadsheetItem::maxScroll(bool rows) const
{
    const AxisPane pane = panes(rows).last();
    return qMax<qreal>(0, (rows ? m_scrollHeight : m_scrollWidth) - (pane.end - pane.start));
}

void SpreadsheetItem::setScrollX(qreal value)
{
    const qreal next = qBound<qreal>(0, value, maxScroll(false));
    if (qFuzzyCompare(next + 1, m_scrollX + 1)) {
        return;
    }
    m_scrollX = next;
    update();
    emit scrollChanged();
    if (m_editing) {
        emit editingChanged();
    }
}

void SpreadsheetItem::setScrollY(qreal value)
{
    const qreal next = qBound<qreal>(0, value, maxScroll(true));
    if (qFuzzyCompare(next + 1, m_scrollY + 1)) {
        return;
    }
    m_scrollY = next;
    update();
    emit scrollChanged();
    if (m_editing) {
        emit editingChanged();
    }
}

void SpreadsheetItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateScrollRanges();
    }
}

CalcEng::CellAddr SpreadsheetItem::frozen() const
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr) {
        return {0, 0};
    }
    return {sheet->freezeRows(), sheet->freezeCols()};
}

void SpreadsheetItem::setZoom(qreal zoom)
{
    const qreal next = qBound(0.5, zoom, 3.0);
    if (qFuzzyCompare(next, m_zoom)) {
        return;
    }
    m_zoom = next;
    updateScrollRanges();
    update();
    emit zoomChanged();
    if (m_editing) {
        emit editingChanged();
    }
}

void SpreadsheetItem::setCellFont(const QFont &font)
{
    if (m_font == font) {
        return;
    }
    m_font = font;
    update();
    emit cellFontChanged();
}

// ---- editor contract ----------------------------------------------------------

QRectF SpreadsheetItem::editorRect() const
{
    return cursorItemRect();
}

void SpreadsheetItem::startEditor()
{
    if (m_editing || m_document == nullptr || m_readOnly) {
        return;
    }
    m_editing = true;
    m_editorText = inputTextAt(m_cursor);
    m_editorSelectAll = true;
    emit editingChanged();
}

void SpreadsheetItem::startEditorWith(const QString &text)
{
    if (m_document == nullptr || m_readOnly) {
        return;
    }
    m_editing = true;
    m_editorText = text;
    m_editorSelectAll = false;
    emit editingChanged();
}

void SpreadsheetItem::commitEditor(const QString &text, bool moveDown)
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    emit editingChanged();
    // Only a changed input is an edit: committing an untouched cell must
    // not push an undo step.
    if (text != inputTextAt(m_cursor)) {
        emit editCommitted(m_cursor, text);
    }
    if (moveDown) {
        setCursorCell(CellAddr{m_cursor.row + 1, m_cursor.col});
    } else {
        update();
        emit cursorChanged();
    }
}

void SpreadsheetItem::cancelEditor()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    emit editingChanged();
    update();
}

void SpreadsheetItem::stepCursor(int rowDelta, int colDelta)
{
    setCursorCell(CellAddr{m_cursor.row + rowDelta, m_cursor.col + colDelta});
}

void SpreadsheetItem::commitFormulaBar(const QString &text)
{
    if (m_document == nullptr) {
        return;
    }
    if (text != inputTextAt(m_cursor)) {
        emit editCommitted(m_cursor, text);
    }
    update();
}

void SpreadsheetItem::setChrome(QColor &slot, const QColor &value)
{
    if (slot == value) {
        return;
    }
    slot = value;
    update();
    emit chromeChanged();
}

} // namespace QindaOffice::Apps::QindaCalc
