// SPDX-License-Identifier: GPL-3.0-or-later
#include "spreadsheet_view.h"

#include "number_format.h"
#include "spreadsheet_document.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

SpreadsheetView::SpreadsheetView(SpreadsheetDocument *document, QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_document(document)
    , m_editor(new QLineEdit(viewport()))
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setAutoFillBackground(false);
    // Cell comments are shown as tooltips; without this the viewport never
    // receives QEvent::ToolTip at all.
    viewport()->setMouseTracking(true);
    m_editor->hide();
    // AGENT-GUARD: intercept Return/Tab ON the editor — the QLineEdit has
    // focus while editing, so the view's keyPressEvent never saw them; Return
    // committed via editingFinished without moving and Tab walked focus away
    // (the spreadsheet's core commit-and-advance convention was dead).
    m_editor->installEventFilter(this);
    connect(m_editor, &QLineEdit::editingFinished, this, [this] {
        if (m_editing) {
            commitEditor(false);
        }
    });
    updateScrollBars();
    emit cursorChanged(m_cursor, QString(), QString());
}

void SpreadsheetView::refreshAll()
{
    updateScrollBars();
    viewport()->update();
}

void SpreadsheetView::setCursorCell(const CalcEng::CellAddr &addr, bool extendSelection)
{
    const qint32 maxRow = Sheet::kMaxRows - 1;
    const qint32 maxCol = Sheet::kMaxCols - 1;
    CalcEng::CellAddr next{qBound(qint32(0), addr.row, maxRow), qBound(qint32(0), addr.col, maxCol)};
    m_cursor = next;
    if (!extendSelection) {
        m_anchor = next;
    }
    // Scroll into view (QAbstractScrollArea has no ensureVisible). Viewport
    // coordinates: this runs outside the painter transform.
    const QRect rect = cursorViewportRect();
    QScrollBar *h = horizontalScrollBar();
    QScrollBar *v = verticalScrollBar();
    // The frozen band is painted over the scrolling one, so "visible" starts
    // after it — scrolling a cell to just below the header would hide it
    // underneath the frozen rows.
    const int leftEdge = kHeaderWidth + int(frozenWidthPx());
    const int topEdge = kHeaderHeight + int(frozenHeightPx());
    if (m_cursor.col >= m_frozen.col) {
        if (rect.left() < leftEdge) {
            h->setValue(h->value() + rect.left() - leftEdge);
        } else if (rect.right() > viewport()->width()) {
            h->setValue(h->value() + rect.right() - viewport()->width());
        }
    }
    if (m_cursor.row >= m_frozen.row) {
        if (rect.top() < topEdge) {
            v->setValue(v->value() + rect.top() - topEdge);
        } else if (rect.bottom() > viewport()->height()) {
            v->setValue(v->value() + rect.bottom() - viewport()->height());
        }
    }
    viewport()->update();
    emit cursorChanged(m_cursor, inputTextAt(next), displayTextAt(m_document->activeSheet(), next));
    emitSelectionChanged();
}

QString SpreadsheetView::inputTextAt(const CalcEng::CellAddr &addr) const
{
    // The document owns the text seams (clipboard, find bar and the view
    // all read through the same active-sheet accessors).
    return m_document->inputTextAt(addr);
}

QString SpreadsheetView::displayTextAt(qint32 sheet, const CalcEng::CellAddr &addr) const
{
    return m_document->displayTextAt(sheet, addr);
}

CalcEng::CellRange SpreadsheetView::selectionRange() const
{
    CalcEng::CellRange range;
    range.topLeft = {qMin(m_anchor.row, m_cursor.row), qMin(m_anchor.col, m_cursor.col)};
    range.bottomRight = {qMax(m_anchor.row, m_cursor.row), qMax(m_anchor.col, m_cursor.col)};
    return range;
}

CalcEng::CellRange SpreadsheetView::usedRange() const
{
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    if (sheet.cells().isEmpty()) {
        return {};
    }
    qint32 maxRow = 0;
    qint32 maxCol = 0;
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu));
        maxCol = qMax(maxCol, qint32(it.key() >> 32));
    }
    return {{0, 0}, {maxRow, maxCol}};
}

void SpreadsheetView::emitSelectionChanged()
{
    emit selectionChanged(selectionRange());
}

void SpreadsheetView::selectColumn(qint32 col, bool extendSelection)
{
    const CalcEng::CellRange used = usedRange();
    const qint32 lastRow = used.isValid() ? used.bottomRight.row : 0;
    if (!extendSelection) {
        m_anchor = {0, col};
    } else {
        m_anchor = {0, m_anchor.col};
    }
    setCursorCell({lastRow, col}, /*extendSelection=*/true);
}

void SpreadsheetView::selectRow(qint32 row, bool extendSelection)
{
    const CalcEng::CellRange used = usedRange();
    const qint32 lastCol = used.isValid() ? used.bottomRight.col : 0;
    if (!extendSelection) {
        m_anchor = {row, 0};
    } else {
        m_anchor = {m_anchor.row, 0};
    }
    setCursorCell({row, lastCol}, /*extendSelection=*/true);
}

void SpreadsheetView::selectUsedRange()
{
    const CalcEng::CellRange used = usedRange();
    if (!used.isValid()) {
        setCursorCell({0, 0});
        return;
    }
    m_anchor = used.topLeft;
    setCursorCell(used.bottomRight, /*extendSelection=*/true);
}

qreal SpreadsheetView::frozenHeightPx() const
{
    qreal y = 0;
    for (qint32 r = 0; r < m_frozen.row; ++r) {
        y += rowHeightPx(r);
    }
    return y;
}

qreal SpreadsheetView::frozenWidthPx() const
{
    qreal x = 0;
    for (qint32 c = 0; c < m_frozen.col; ++c) {
        x += colWidthPx(c);
    }
    return x;
}

int SpreadsheetView::rowTop(qint32 row) const
{
    const QSet<qint32> hidden =
        m_document->workbook()->sheetAt(m_document->activeSheet()).filteredOutRows();
    qreal y = 0;
    for (qint32 r = 0; r < row; ++r) {
        y += visibleRowHeightPx(r, hidden);
    }
    // A frozen row sits at its own offset; everything below the split moves
    // with the scroll bar, whose value counts only the scrollable band.
    return int(row < m_frozen.row ? y : y - verticalScrollBar()->value());
}

int SpreadsheetView::colLeft(qint32 col) const
{
    qreal x = 0;
    for (qint32 c = 0; c < col; ++c) {
        x += colWidthPx(c);
    }
    return int(col < m_frozen.col ? x : x - horizontalScrollBar()->value());
}

QVector<SpreadsheetView::Band> SpreadsheetView::visibleRowBands() const
{
    QVector<Band> bands;
    const qreal viewportHeight = viewport()->height() - kHeaderHeight;
    // AGENT-GUARD: a filtered-out row has NO band. Everything that walks
    // rows — painting, the header band, hit-testing, scroll-into-view —
    // goes through this list, so a hidden row is uniformly absent rather
    // than "painted blank", which is what keeps clicks landing on the row
    // the user can actually see.
    const QSet<qint32> hidden =
        m_document->workbook()->sheetAt(m_document->activeSheet()).filteredOutRows();
    qreal y = 0;
    for (qint32 r = 0; r < m_frozen.row && y < viewportHeight; ++r) {
        if (hidden.contains(r)) {
            continue;
        }
        const qreal h = rowHeightPx(r);
        bands.append({r, y, h});
        y += h;
    }
    const qreal split = y;
    // Walk forward to the first scrolling row that reaches past the split.
    const qreal scroll = verticalScrollBar()->value();
    qint32 row = m_frozen.row;
    qreal top = split;
    while (row < Sheet::kMaxRows - 1 && top + visibleRowHeightPx(row, hidden) <= split + scroll) {
        top += visibleRowHeightPx(row, hidden);
        ++row;
    }
    qreal pos = top - scroll;
    while (pos < viewportHeight && row < Sheet::kMaxRows) {
        const qreal h = visibleRowHeightPx(row, hidden);
        if (h > 0) {
            bands.append({row, pos, h});
        }
        pos += h;
        ++row;
    }
    return bands;
}

qreal SpreadsheetView::visibleRowHeightPx(qint32 row, const QSet<qint32> &hidden) const
{
    return hidden.contains(row) ? 0.0 : rowHeightPx(row);
}

QVector<SpreadsheetView::Band> SpreadsheetView::visibleColumnBands() const
{
    QVector<Band> bands;
    const qreal viewportWidth = viewport()->width() - kHeaderWidth;
    qreal x = 0;
    for (qint32 c = 0; c < m_frozen.col && x < viewportWidth; ++c) {
        const qreal w = colWidthPx(c);
        bands.append({c, x, w});
        x += w;
    }
    const qreal split = x;
    const qreal scroll = horizontalScrollBar()->value();
    qint32 col = m_frozen.col;
    qreal left = split;
    while (col < Sheet::kMaxCols - 1 && left + colWidthPx(col) <= split + scroll) {
        left += colWidthPx(col);
        ++col;
    }
    qreal pos = left - scroll;
    while (pos < viewportWidth && col < Sheet::kMaxCols) {
        const qreal w = colWidthPx(col);
        bands.append({col, pos, w});
        pos += w;
        ++col;
    }
    return bands;
}

void SpreadsheetView::setFrozen(qint32 row, qint32 col)
{
    const CalcEng::CellAddr next{qMax(qint32(0), row), qMax(qint32(0), col)};
    if (next == m_frozen) {
        return;
    }
    m_frozen = next;
    // The frozen band eats viewport space, so the scroll range changes with
    // it; without this the sheet's last rows became unreachable.
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
    updateScrollBars();
    viewport()->update();
}

QFont SpreadsheetView::scaledFont() const
{
    QFont scaled = font();
    const qreal points = scaled.pointSizeF() > 0 ? scaled.pointSizeF() : 10.0;
    scaled.setPointSizeF(qMax(4.0, points * m_zoom));
    return scaled;
}

qreal SpreadsheetView::colWidthPx(qint32 col) const
{
    return m_document->workbook()->sheetAt(m_document->activeSheet()).columnWidth(col) * m_zoom;
}

qreal SpreadsheetView::rowHeightPx(qint32 row) const
{
    return m_document->workbook()->sheetAt(m_document->activeSheet()).rowHeight(row) * m_zoom;
}

void SpreadsheetView::setZoom(qreal zoom)
{
    const qreal next = qBound(0.5, zoom, 3.0);
    if (qFuzzyCompare(next, m_zoom)) {
        return;
    }
    m_zoom = next;
    updateScrollBars();
    if (m_editing) {
        m_editor->setGeometry(cursorViewportRect());
    }
    viewport()->update();
}

QRect SpreadsheetView::cursorViewportRect() const
{
    return cellRect(m_cursor).translated(kHeaderWidth, kHeaderHeight);
}

QRect SpreadsheetView::cellRect(const CalcEng::CellAddr &addr) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    return {colLeft(addr.col), rowTop(addr.row), int(colWidthPx(addr.col)),
            int(rowHeightPx(addr.row))};
}

CalcEng::CellAddr SpreadsheetView::cellAtPos(const QPoint &pos) const
{
    // AGENT-GUARD: hit-testing walks the SAME band lists painting does. It
    // used to re-derive positions from the scroll value alone, which stops
    // being true the moment a pane is frozen (clicks in the frozen band
    // landed on whatever had scrolled underneath it).
    const QVector<Band> rows = visibleRowBands();
    const QVector<Band> cols = visibleColumnBands();
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

void SpreadsheetView::updateScrollBars()
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    // Default-size estimate keeps the scrollbars stable for sparse sheets;
    // grows as content extends.
    qint32 maxRow = 200;
    qint32 maxCol = 40;
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu) + 20);
        maxCol = qMax(maxCol, qint32(it.key() >> 32) + 5);
    }
    qreal height = 0;
    for (qint32 r = 0; r < maxRow; ++r) {
        height += rowHeightPx(r);
    }
    qreal width = 0;
    for (qint32 c = 0; c < maxCol; ++c) {
        width += colWidthPx(c);
    }
    // The frozen band is always on screen, so it belongs to neither the
    // scrollable content nor the space available to it.
    const qreal frozenH = frozenHeightPx();
    const qreal frozenW = frozenWidthPx();
    const qreal visibleH = qMax<qreal>(0, viewport()->height() - kHeaderHeight - frozenH);
    const qreal visibleW = qMax<qreal>(0, viewport()->width() - kHeaderWidth - frozenW);
    verticalScrollBar()->setRange(0, int(qMax<qreal>(0, (height - frozenH) - visibleH)));
    horizontalScrollBar()->setRange(0, int(qMax<qreal>(0, (width - frozenW) - visibleW)));
    verticalScrollBar()->setSingleStep(kDefaultRowPx);
    horizontalScrollBar()->setSingleStep(96);
}

void SpreadsheetView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}




void SpreadsheetView::startEditor()
{
    if (m_editing) {
        return;
    }
    m_editing = true;
    // The editor is a child widget, so it is positioned in viewport space.
    m_editor->setGeometry(cursorViewportRect());
    m_editor->setText(inputTextAt(m_cursor));
    m_editor->selectAll();
    m_editor->show();
    m_editor->setFocus();
}

void SpreadsheetView::commitEditor(bool moveDown)
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_editor->hide();
    const QString input = m_editor->text();
    setFocus();
    if (input != inputTextAt(m_cursor)) {
        emit editCommitted(m_cursor, input);
    }
    if (moveDown) {
        setCursorCell({m_cursor.row + 1, m_cursor.col});
    } else {
        viewport()->update();
        emit cursorChanged(m_cursor, inputTextAt(m_cursor),
                           displayTextAt(m_document->activeSheet(), m_cursor));
    }
}

void SpreadsheetView::cancelEditor()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_editor->hide();
    setFocus();
    viewport()->update();
}

void SpreadsheetView::commitFormulaBar(const QString &text)
{
    if (text != inputTextAt(m_cursor)) {
        emit editCommitted(m_cursor, text);
    }
}

} // namespace QindaOffice::Apps::QindaCalc
