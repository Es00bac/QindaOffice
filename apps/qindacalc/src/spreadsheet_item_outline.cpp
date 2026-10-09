// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem's M66 parts (D-075): the outline gutter — level buttons
// and each group's bracket and +/- button, for rows beside the row headers
// and for columns above the column headers — which links a cell has, the
// border round the spill under the cursor, and the cursor stepping over
// hidden rows and columns. The item still never changes the document: a
// gutter click is a signal the host turns into an undoable edit.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "hyperlink.h"
#include "sheet.h"

#include <QPainter>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

constexpr qreal kButton = 10.0; // the +/- and level buttons

struct GutterButton {
    QRectF rect;
    qint32 line = 0;
    quint8 level = 1;
    bool collapsed = false;
    qreal spanStart = 0; // the bracket, along the axis (item coordinates)
    qreal spanEnd = 0;
};

} // namespace

qreal SpreadsheetItem::rowGutterPx() const
{
    const Sheet *sheet = activeSheet();
    const quint8 levels = sheet != nullptr ? sheet->outline().maxLevel(OutlineAxis::Rows) : quint8(0);
    return levels == 0 ? 0.0 : (levels + 1) * qreal(kGutterStep);
}

qreal SpreadsheetItem::colGutterPx() const
{
    const Sheet *sheet = activeSheet();
    const quint8 levels = sheet != nullptr ? sheet->outline().maxLevel(OutlineAxis::Columns) : quint8(0);
    return levels == 0 ? 0.0 : (levels + 1) * qreal(kGutterStep);
}

bool SpreadsheetItem::hasLink(const CellAddr &addr) const
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr) {
        return false;
    }
    if (sheet->hyperlinkAt(addr) != nullptr) {
        return true;
    }
    const Cell *cell = sheet->cellAt(addr);
    return cell != nullptr && cell->ast && cell->ast->kind == Node::Kind::Call
        && cell->ast->text.compare(QLatin1String("HYPERLINK"), Qt::CaseInsensitive) == 0;
}

QString SpreadsheetItem::linkTip(const CellAddr &addr) const
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr || !hasLink(addr)) {
        return {};
    }
    QString where;
    if (const Hyperlink *link = sheet->hyperlinkAt(addr)) {
        where = link->tooltip.isEmpty() ? hyperlinkTarget(*link) : link->tooltip;
    } else {
        where = hyperlinkFormulaTarget(*m_document->workbook(), m_document->activeSheet(), addr);
    }
    return tr("%1\nCtrl+click to follow the link.").arg(where);
}

qint32 SpreadsheetItem::visibleStep(qint32 from, int delta, bool rows) const
{
    const Sheet *sheet = activeSheet();
    const qint32 limit = rows ? Sheet::kMaxRows : Sheet::kMaxCols;
    qint32 next = from + delta;
    while (sheet != nullptr && next >= 0 && next < limit
           && (rows ? sheet->isRowHidden(next) : sheet->isColumnHidden(next))) {
        next += delta;
    }
    return next < 0 || next >= limit ? from : next;
}

// Every group button the gutter shows on screen now, in item coordinates.
static QList<GutterButton> gutterButtons(const Sheet &sheet, bool rows, const QVector<qreal> &starts,
                                         const QVector<qreal> &ends, const QVector<qint32> &indices, qreal across)
{
    QList<GutterButton> out;
    const auto bandOf = [&indices](qint32 line) { return indices.indexOf(line); };
    for (const OutlineGroup &group : outlineGroups(sheet.outline(), rows ? OutlineAxis::Rows : OutlineAxis::Columns)) {
        GutterButton button;
        button.line = group.summary >= 0 ? group.summary : group.first;
        button.level = group.level;
        button.collapsed = group.collapsed;
        const int at = bandOf(button.line);
        if (at < 0) {
            continue; // its button line is off screen (or hidden itself)
        }
        const qreal centre = across + (group.level - 0.5) * SpreadsheetItem::kGutterStep;
        const qreal mid = (starts.at(at) + ends.at(at)) / 2;
        button.rect = rows ? QRectF(centre - kButton / 2, mid - kButton / 2, kButton, kButton)
                           : QRectF(mid - kButton / 2, centre - kButton / 2, kButton, kButton);
        qreal first = -1;
        qreal last = -1;
        for (int i = 0; i < indices.size(); ++i) {
            if (indices.at(i) >= group.first && indices.at(i) <= group.last) {
                first = first < 0 ? starts.at(i) : qMin(first, starts.at(i));
                last = qMax(last, ends.at(i));
            }
        }
        button.spanStart = first;
        button.spanEnd = last;
        out.append(button);
    }
    return out;
}

static void bandsOnScreen(const QVector<qint32> &index, const QVector<qreal> &pos, const QVector<qreal> &size,
                          qreal origin, QVector<qreal> *starts, QVector<qreal> *ends)
{
    for (int i = 0; i < index.size(); ++i) {
        starts->append(origin + pos.at(i));
        ends->append(origin + pos.at(i) + size.at(i));
    }
}

void SpreadsheetItem::paintOutline(QPainter &p)
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr || (rowGutterPx() <= 0 && colGutterPx() <= 0)) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    const auto paintButtons = [&](bool rows) {
        const QVector<Band> bands = rows ? visibleRowBands() : visibleColumnBands();
        QVector<qint32> indices;
        QVector<qreal> pos;
        QVector<qreal> size;
        for (const Band &band : bands) {
            indices.append(band.index);
            pos.append(band.pos);
            size.append(band.size);
        }
        QVector<qreal> starts;
        QVector<qreal> ends;
        bandsOnScreen(indices, pos, size, rows ? headerHeight() : headerWidth(), &starts, &ends);
        p.save();
        p.setClipRect(rows ? QRectF(0, headerHeight(), rowGutterPx(), height()) : QRectF(headerWidth(), 0, width(), colGutterPx()));
        p.fillRect(rows ? QRectF(0, 0, rowGutterPx(), height()) : QRectF(0, 0, width(), colGutterPx()), m_headerColor);
        for (const GutterButton &button : gutterButtons(*sheet, rows, starts, ends, indices, 0)) {
            p.setPen(QPen(m_headerTextColor, 1));
            if (!button.collapsed && button.spanStart >= 0) {
                const qreal line = rows ? button.rect.center().x() : button.rect.center().y();
                if (rows) {
                    p.drawLine(QPointF(line, button.spanStart + 2), QPointF(line, button.spanEnd - 2));
                } else {
                    p.drawLine(QPointF(button.spanStart + 2, line), QPointF(button.spanEnd - 2, line));
                }
            }
            p.fillRect(button.rect, m_headerColor);
            p.drawRect(button.rect);
            const QPointF c = button.rect.center();
            p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() + 3, c.y()));
            if (button.collapsed) {
                p.drawLine(QPointF(c.x(), c.y() - 3), QPointF(c.x(), c.y() + 3));
            }
        }
        p.restore();
    };
    if (rowGutterPx() > 0) {
        paintButtons(true);
    }
    if (colGutterPx() > 0) {
        paintButtons(false);
    }
    // The level buttons in the corner: 1 … deepest+1, per axis.
    p.setPen(m_headerTextColor);
    QFont small = m_font;
    small.setPointSizeF(qMax(6.0, m_font.pointSizeF() * 0.8));
    p.setFont(small);
    const auto levelButton = [&](bool rows, int n) {
        const qreal along = (n - 0.5) * kGutterStep - kButton / 2;
        return rows ? QRectF(along, colGutterPx() + (kHeaderHeight - kButton) / 2, kButton, kButton)
                    : QRectF(rowGutterPx() + (kHeaderWidth - kButton) / 2, along, kButton, kButton);
    };
    for (const bool rows : {true, false}) {
        const quint8 levels = sheet->outline().maxLevel(rows ? OutlineAxis::Rows : OutlineAxis::Columns);
        for (int n = 1; levels > 0 && n <= levels + 1; ++n) {
            const QRectF r = levelButton(rows, n);
            p.drawRect(r);
            p.drawText(r, Qt::AlignCenter, QString::number(n));
        }
    }
    p.restore();
}

bool SpreadsheetItem::outlineClick(const QPointF &pos)
{
    const Sheet *sheet = activeSheet();
    if (sheet == nullptr) {
        return false;
    }
    // A level button (the corner) first.
    if (pos.x() < rowGutterPx() && pos.y() >= colGutterPx() && pos.y() < headerHeight()) {
        emit outlineLevelClicked(true, int(pos.x() / kGutterStep) + 1);
        return true;
    }
    if (pos.y() < colGutterPx() && pos.x() >= rowGutterPx() && pos.x() < headerWidth()) {
        emit outlineLevelClicked(false, int(pos.y() / kGutterStep) + 1);
        return true;
    }
    for (const bool rows : {true, false}) {
        const QVector<Band> bands = rows ? visibleRowBands() : visibleColumnBands();
        QVector<qint32> indices;
        QVector<qreal> starts;
        QVector<qreal> ends;
        for (const Band &band : bands) {
            indices.append(band.index);
            starts.append((rows ? headerHeight() : headerWidth()) + band.pos);
            ends.append((rows ? headerHeight() : headerWidth()) + band.pos + band.size);
        }
        for (const GutterButton &button : gutterButtons(*sheet, rows, starts, ends, indices, 0)) {
            if (button.rect.adjusted(-2, -2, 2, 2).contains(pos)) {
                emit outlineToggled(rows, int(button.line), int(button.level));
                return true;
            }
        }
    }
    return false;
}

void SpreadsheetItem::paintSpillBorder(QPainter &p)
{
    if (m_document == nullptr) {
        return;
    }
    const Workbook &wb = *m_document->workbook();
    const qint32 active = m_document->activeSheet();
    const CellAddr anchor = wb.spillAnchorOf(active, m_cursor);
    const auto spill = anchor.isValid() ? wb.spillAt(active, anchor) : std::nullopt;
    if (!spill || spill->blocked || spill->extent.cellCount() < 2) {
        return;
    }
    // Excel's thin border round the whole spill while the cursor is in it.
    const QRectF first = cellRect(spill->extent.topLeft);
    const QRectF last = cellRect(spill->extent.bottomRight);
    p.setPen(QPen(m_selectionColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(first.united(last).adjusted(0, 0, -1, -1));
}

} // namespace QindaOffice::Apps::QindaCalc
