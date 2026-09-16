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
    if (rect.left() < kHeaderWidth) {
        h->setValue(h->value() + rect.left() - kHeaderWidth);
    } else if (rect.right() > viewport()->width()) {
        h->setValue(h->value() + rect.right() - viewport()->width());
    }
    if (rect.top() < kHeaderHeight) {
        v->setValue(v->value() + rect.top() - kHeaderHeight);
    } else if (rect.bottom() > viewport()->height()) {
        v->setValue(v->value() + rect.bottom() - viewport()->height());
    }
    viewport()->update();
    emit cursorChanged(m_cursor, inputTextAt(next), displayTextAt(m_document->activeSheet(), next));
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

int SpreadsheetView::rowTop(qint32 row) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    qreal y = 0;
    for (qint32 r = 0; r < row; ++r) {
        y += rowHeightPx(r);
    }
    return int(y - verticalScrollBar()->value());
}

int SpreadsheetView::colLeft(qint32 col) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    qreal x = 0;
    for (qint32 c = 0; c < col; ++c) {
        x += colWidthPx(c);
    }
    return int(x - horizontalScrollBar()->value());
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
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const qreal x = pos.x() + horizontalScrollBar()->value();
    const qreal y = pos.y() + verticalScrollBar()->value();
    qint32 col = 0;
    qreal acc = 0;
    while (col < Sheet::kMaxCols - 1 && acc + colWidthPx(col) < x) {
        acc += colWidthPx(col);
        ++col;
    }
    qint32 row = 0;
    acc = 0;
    while (row < Sheet::kMaxRows - 1 && acc + rowHeightPx(row) < y) {
        acc += rowHeightPx(row);
        ++row;
    }
    return {row, col};
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
    verticalScrollBar()->setRange(0, int(qMax<qreal>(0, height - viewport()->height())));
    horizontalScrollBar()->setRange(0, int(qMax<qreal>(0, width - viewport()->width())));
    verticalScrollBar()->setSingleStep(kDefaultRowPx);
    horizontalScrollBar()->setSingleStep(96);
}

void SpreadsheetView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}




void SpreadsheetView::mousePressEvent(QMouseEvent *event)
{
    if (event->position().x() < kHeaderWidth || event->position().y() < kHeaderHeight) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    cancelEditor();
    setCursorCell(cellAtPos(event->position().toPoint() - QPoint(kHeaderWidth, kHeaderHeight)),
                  event->modifiers() & Qt::ShiftModifier);
}

void SpreadsheetView::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton
        && event->position().x() >= kHeaderWidth && event->position().y() >= kHeaderHeight) {
        setCursorCell(
            cellAtPos(event->position().toPoint() - QPoint(kHeaderWidth, kHeaderHeight)), true);
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
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
            // Commit and advance right (Shift+Tab: left).
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
    const bool extend = event->modifiers() & Qt::ShiftModifier;
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
    case Qt::Key_PageDown:
        setCursorCell({m_cursor.row + 20, m_cursor.col}, extend);
        return;
    case Qt::Key_PageUp:
        setCursorCell({m_cursor.row - 20, m_cursor.col}, extend);
        return;
    case Qt::Key_Home:
        setCursorCell(event->modifiers() & Qt::ControlModifier ? CalcEng::CellAddr{0, 0}
                                                               : CalcEng::CellAddr{m_cursor.row, 0},
                      extend);
        return;
    case Qt::Key_F2:
        startEditor();
        return;
    default:
        break;
    }
    // Printable input starts the editor prefilled.
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint() && !(event->modifiers() & Qt::ControlModifier)
        && !(event->modifiers() & Qt::AltModifier)) {
        startEditor();
        m_editor->setText(text);
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
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
