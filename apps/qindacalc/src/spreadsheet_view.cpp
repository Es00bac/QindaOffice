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

void SpreadsheetView::selectUsedRange()
{
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    if (sheet.cells().isEmpty()) {
        setCursorCell({0, 0});
        return;
    }
    qint32 maxRow = 0;
    qint32 maxCol = 0;
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu));
        maxCol = qMax(maxCol, qint32(it.key() >> 32));
    }
    m_anchor = {0, 0};
    setCursorCell({maxRow, maxCol}, /*extendSelection=*/true);
}

int SpreadsheetView::rowTop(qint32 row) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    qreal y = 0;
    for (qint32 r = 0; r < row; ++r) {
        y += sheet.rowHeight(r);
    }
    return int(y - verticalScrollBar()->value());
}

int SpreadsheetView::colLeft(qint32 col) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    qreal x = 0;
    for (qint32 c = 0; c < col; ++c) {
        x += sheet.columnWidth(c);
    }
    return int(x - horizontalScrollBar()->value());
}

QRect SpreadsheetView::cursorViewportRect() const
{
    return cellRect(m_cursor).translated(kHeaderWidth, kHeaderHeight);
}

QRect SpreadsheetView::cellRect(const CalcEng::CellAddr &addr) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    return {colLeft(addr.col), rowTop(addr.row), int(sheet.columnWidth(addr.col)),
            int(sheet.rowHeight(addr.row))};
}

CalcEng::CellAddr SpreadsheetView::cellAtPos(const QPoint &pos) const
{
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const qreal x = pos.x() + horizontalScrollBar()->value();
    const qreal y = pos.y() + verticalScrollBar()->value();
    qint32 col = 0;
    qreal acc = 0;
    while (col < Sheet::kMaxCols - 1 && acc + sheet.columnWidth(col) < x) {
        acc += sheet.columnWidth(col);
        ++col;
    }
    qint32 row = 0;
    acc = 0;
    while (row < Sheet::kMaxRows - 1 && acc + sheet.rowHeight(row) < y) {
        acc += sheet.rowHeight(row);
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
        height += sheet.rowHeight(r);
    }
    qreal width = 0;
    for (qint32 c = 0; c < maxCol; ++c) {
        width += sheet.columnWidth(c);
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

void SpreadsheetView::paintHeaderBands(QPainter &p, const QRect &clip)
{
    const QRect corner(0, 0, kHeaderWidth, kHeaderHeight);
    p.fillRect(corner, palette().button().color());
    p.setPen(palette().buttonText().color());
    p.drawText(corner, Qt::AlignCenter, QStringLiteral("•"));

    // AGENT-GUARD: the column band must walk the sheet's REAL column widths —
    // the grid renders with sheet.columnWidth(); the band previously assumed
    // a fixed 96 px, so header letters drifted out of alignment after any
    // xlsx import brought different widths in.
    p.save();
    p.translate(kHeaderWidth, 0);
    {
        const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
        const qint32 scrollX = horizontalScrollBar()->value();
        // Find the first column whose right edge is at/after the scroll pos.
        qint32 col = 0;
        qreal x = -scrollX;
        while (col < Sheet::kMaxCols && x + sheet.columnWidth(col) <= 0) {
            x += sheet.columnWidth(col);
            ++col;
        }
        while (x < clip.width() && col < Sheet::kMaxCols) {
            const qreal w = sheet.columnWidth(col);
            const QRectF r(x, 0, w, kHeaderHeight);
            if (r.right() >= 0) {
                p.fillRect(r, palette().button().color());
                p.setPen(palette().buttonText().color());
                p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, colName(col));
                p.setPen(palette().dark().color());
                p.drawLine(r.bottomLeft(), r.bottomRight());
                p.drawLine(r.topRight(), r.bottomRight());
            }
            x += w;
            ++col;
        }
    }
    p.restore();

    p.save();
    p.translate(0, kHeaderHeight);
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    qint32 row = 0;
    qreal y = -verticalScrollBar()->value();
    while (y < clip.height()) {
        const qreal h = sheet.rowHeight(row);
        if (y + h > 0) {
            const QRect r(0, int(y), kHeaderWidth, int(h));
            p.fillRect(r, palette().button().color());
            p.setPen(palette().buttonText().color());
            p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, QString::number(row + 1));
            p.setPen(palette().dark().color());
            p.drawLine(r.bottomLeft(), r.bottomRight());
            p.drawLine(r.bottomRight(), r.topRight());
        }
        y += h;
        ++row;
    }
    p.restore();
}

void SpreadsheetView::paintGrid(QPainter &p, const QRect &clip)
{
    const qint32 sheetIndex = m_document->activeSheet();
    const Workbook &wb = *m_document->workbook();
    const Sheet &sheet = wb.sheetAt(sheetIndex);

    p.save();
    p.translate(kHeaderWidth, kHeaderHeight);

    // Visible row band.
    qint32 firstRow = 0;
    qreal y = -verticalScrollBar()->value();
    while (firstRow < Sheet::kMaxRows && y + sheet.rowHeight(firstRow) < 0) {
        y += sheet.rowHeight(firstRow);
        ++firstRow;
    }
    const QFont baseFont = font();
    const QFont boldFont([this] {
        QFont f = font();
        f.setBold(true);
        return f;
    }());

    for (qint32 row = firstRow; y < clip.height() - kHeaderHeight && row < Sheet::kMaxRows;) {
        const qreal h = sheet.rowHeight(row);
        qint32 x = -horizontalScrollBar()->value();
        qint32 firstCol = 0;
        while (firstCol < Sheet::kMaxCols && x + sheet.columnWidth(firstCol) < 0) {
            x += sheet.columnWidth(firstCol);
            ++firstCol;
        }
        for (qint32 col = firstCol; x < clip.width() - kHeaderWidth
                                    && col < Sheet::kMaxCols;) {
            const qreal w = sheet.columnWidth(col);
            const QRectF cell(x, y, w, h);
            const CellAddr addr{row, col};
            const bool inSelection =
                addr.row >= qMin(m_anchor.row, m_cursor.row)
                && addr.row <= qMax(m_anchor.row, m_cursor.row)
                && addr.col >= qMin(m_anchor.col, m_cursor.col)
                && addr.col <= qMax(m_anchor.col, m_cursor.col);
            const bool isCursor = addr == m_cursor;
            if (inSelection) {
                p.fillRect(cell, isCursor ? palette().highlight().color()
                                          : palette().highlight().color().lighter(160));
            }
            const Cell *cellData = sheet.cellAt(addr);
            // Render imported style attributes: bg fill, font family/size/
            // style, fg colour and alignment. The formatting toolbar set
            // these while the grid rendered only bold — users believed the
            // formatting applied when it was invisible.
            const CellStyle *style = cellData != nullptr
                ? wb.style(sheet.styleIdAt(addr))
                : nullptr;
            if (style != nullptr && style->bg.has_value() && !inSelection) {
                p.fillRect(cell, QColor((*style->bg).r, (*style->bg).g, (*style->bg).b));
            }
            if (cellData != nullptr && !isBlank(cellData->value)) {
                const QString text =
                    NumberFormat(style != nullptr && !style->numberFormat.isEmpty()
                                     ? style->numberFormat
                                     : QStringLiteral("General"))
                        .format(cellData->value)
                        .text;
                if (style != nullptr && (style->bold || style->italic || style->underline
                                         || style->fontSize > 0 || !style->fontFamily.isEmpty())) {
                    QFont cellFont = baseFont;
                    if (!style->fontFamily.isEmpty()) {
                        cellFont.setFamily(style->fontFamily);
                    }
                    if (style->fontSize > 0) {
                        cellFont.setPointSizeF(style->fontSize);
                    }
                    cellFont.setBold(style->bold);
                    cellFont.setItalic(style->italic);
                    cellFont.setUnderline(style->underline);
                    p.setFont(cellFont);
                } else {
                    p.setFont(style != nullptr && style->bold ? boldFont : baseFont);
                }
                if (inSelection && isCursor) {
                    p.setPen(palette().highlightedText().color());
                } else if (style != nullptr && style->fg.has_value()) {
                    p.setPen(QColor((*style->fg).r, (*style->fg).g, (*style->fg).b));
                } else {
                    p.setPen(palette().text().color());
                }
                // Explicit alignment wins; otherwise numbers right, text left.
                Qt::Alignment alignment = Qt::AlignVCenter;
                if (style != nullptr && style->alignment != 0) {
                    alignment |= style->alignment == 1 ? Qt::AlignHCenter : Qt::AlignRight;
                } else {
                    alignment |= isNumber(cellData->value) || isBool(cellData->value)
                        ? Qt::AlignRight
                        : Qt::AlignLeft;
                }
                p.drawText(cell.adjusted(4, 0, -4, 0), alignment, text);
            }
            p.setPen(palette().mid().color());
            p.drawLine(cell.bottomLeft(), cell.bottomRight());
            p.drawLine(cell.topRight(), cell.bottomRight());
            x += w;
            ++col;
        }
        y += h;
        ++row;
    }

    // Cursor outline last. AGENT-GUARD: GRID coordinates — the painter is
    // still inside p.translate(kHeaderWidth, kHeaderHeight) from the top of
    // this block, so translating the rect here too applied the header offset
    // TWICE and drew the outline a full header down and to the right of the
    // cell it belongs to (the selection fill, painted in the loop above, sat
    // in the right place, which is what made it look so odd).
    p.setPen(QPen(palette().highlight().color().darker(140), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cellRect(m_cursor));
    p.restore();
}

void SpreadsheetView::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), palette().base().color());
    paintGrid(p, event->rect());
    paintHeaderBands(p, event->rect());
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
