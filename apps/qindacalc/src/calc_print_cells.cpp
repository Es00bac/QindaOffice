// SPDX-License-Identifier: GPL-3.0-or-later
// The cells of a printed page (calc_print_internal.h; M78, D-087 — moved
// here and given Excel's overflow by M82, D-091). Like the grid
// (spreadsheet_item_cells.cpp) the page is drawn in passes, so a text that
// runs on over its empty neighbours is never painted over by their fills:
// fills and data bars, grid lines (none under overflowing text), icons and
// text, borders. Both follow calc_text_overflow.h, so paper and screen
// spill, fit numbers and show #### alike; the inset is the grid's 4 px
// (3 pt) at 100 %, so at 100 % they cut text at the same place too.
//
// A page's titles and body are separate segments: a title column's text
// runs on only over title columns, the body's over the print area's
// columns — and a text from a column on another page of the same print
// area shows the part that reaches onto this one.

#include "calc_print_internal.h"

#include "calc_cell_look.h"
#include "calc_sparkline_look.h"
#include "calc_text_overflow.h"

#include "number_format.h"

#include <QFontMetricsF>

#include <optional>

namespace QindaOffice::Apps::QindaCalc::PrintDetail {

using namespace CalcEng;

namespace {

constexpr qreal kInsetPt = 3; // the grid's 4 px

struct PrintedCell {
    CellAddr addr;
    QRectF rect; // points
    bool merged = false;
    qsizetype colSegment = 0;
};

struct PrintText {
    CellAddr addr;
    QRectF clip; // points
    QRectF box;
    QString text;
    QFont font;
    QColor ink;
    int flags = 0;
    SpillSpan span;
};

// Where `col` starts in `segment` (points), measured past its printed
// columns when it is not one of them.
qreal columnStart(const Segment &segment, qint32 col, const ColumnWidth &width)
{
    if (segment.lines.isEmpty()) {
        return 0;
    }
    if (col <= segment.lines.first().index) {
        qreal x = segment.starts.first();
        for (qint32 c = col; c < segment.lines.first().index; ++c) {
            x -= width(c);
        }
        return x;
    }
    qsizetype at = segment.lines.size() - 1;
    while (at > 0 && segment.lines.at(at).index > col) {
        --at;
    }
    if (segment.lines.at(at).index == col) {
        return segment.starts.at(at);
    }
    qreal x = segment.starts.at(at) + segment.lines.at(at).sizePt;
    for (qint32 c = segment.lines.at(at).index + 1; c < col; ++c) {
        x += width(c);
    }
    return x;
}

QString printedText(const PrintContext &ctx, const Cell &cell, const CellStyle *style)
{
    if (isErr(cell.value)) {
        switch (ctx.setup->errors) { // Page Setup ▸ Sheet ▸ Cell errors as
        case 1:
            return {};
        case 2:
            return QStringLiteral("--");
        case 3:
            return QStringLiteral("#N/A");
        default:
            break;
        }
    }
    const QString code = style != nullptr && !style->numberFormat.isEmpty() ? style->numberFormat : QStringLiteral("General");
    return NumberFormat(code).format(cell.value).text;
}

std::optional<PrintText> textOf(const Canvas &canvas, const PrintContext &ctx, const CellAddr &addr, const QRectF &rect,
                                bool merged, const Segment &segment, const ColumnWidth &width)
{
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    const Cell *cell = sheet.cellAt(addr);
    if (cell == nullptr || isBlank(cell->value)) {
        return std::nullopt;
    }
    const qint32 styleId = sheet.styleIdAt(addr);
    const CellStyle *style = styleId != 0 ? ctx.book->style(styleId) : nullptr;
    const CondFormat conditional = sheet.condFormats().isEmpty() ? CondFormat{} : ctx.book->condFormatFor(ctx.sheet, addr);
    if (conditional.hideValue) {
        return std::nullopt;
    }
    const bool colour = !ctx.setup->blackAndWhite;
    PrintText out;
    out.addr = addr;
    QFont font = ctx.base;
    if (style != nullptr && !style->fontFamily.isEmpty()) {
        font.setFamily(style->fontFamily);
    }
    const qreal size = style != nullptr && style->fontSize > 0 ? style->fontSize : ctx.base.pointSizeF();
    font.setPointSizeF(qMax(1.0, size * ctx.scale));
    font.setBold(conditional.bold.value_or(style != nullptr && style->bold));
    font.setItalic(conditional.italic.value_or(style != nullptr && style->italic));
    font.setUnderline(conditional.underline.value_or(style != nullptr && style->underline));
    font.setStrikeOut(conditional.strikeout.value_or(style != nullptr && style->strikeout));
    out.font = font;
    const QString code = style != nullptr && !style->numberFormat.isEmpty() ? style->numberFormat : QStringLiteral("General");
    const NumberFormat format(code);
    const FormattedText formatted = format.format(cell->value);
    out.ink = kInk;
    if (colour && conditional.foreground) {
        out.ink = QColor(conditional.foreground->r, conditional.foreground->g, conditional.foreground->b);
    } else if (colour && formatted.colour) {
        out.ink = QColor(formatted.colour->r, formatted.colour->g, formatted.colour->b);
    } else if (colour && style != nullptr && style->fg) {
        out.ink = QColor(style->fg->r, style->fg->g, style->fg->b);
    }
    const CellHorizontal layout = horizontalLayout(cell->value, style, merged);
    const Qt::Alignment vertical = style != nullptr
        ? (style->vAlign == 0 ? Qt::AlignTop : style->vAlign == 2 ? Qt::AlignBottom : Qt::AlignVCenter)
        : Qt::AlignVCenter;
    out.flags = int(layout.align) | (style != nullptr && style->wrap ? int(Qt::AlignTop) | int(Qt::TextWordWrap) : int(vertical));
    const qreal inset = kInsetPt * ctx.scale;
    const qreal iconRoom = conditional.icon && layout.align != Qt::AlignRight
        ? iconBox(canvas.rect(rect)).width() / canvas.dpp + 1
        : 0;
    const QFontMetricsF metrics(font, canvas.p->device());
    const auto measure = [&](const QString &text) { return metrics.horizontalAdvance(text) / canvas.dpp; };
    const qreal room = rect.width() - 2 * inset - iconRoom;
    QString text = printedText(ctx, *cell, style);
    if (layout.numeric) {
        text = fittedNumber(std::get<double>(cell->value), text, format.isGeneral(), room, measure);
    }
    if (layout.fill) {
        text = filledText(text, room, measure);
    }
    out.text = text;
    const qreal need = measure(text) + 2 * inset + iconRoom;
    out.span = merged ? SpillSpan{addr.col, addr.col}
                      : spillSpan(sheet, addr, layout.spill, need, width, segment.minLine, segment.maxLine);
    // A text that never spills (wrapped, filled, merged, a number) is laid
    // out in its own cell: wrapping needs the cell's width.
    const qreal wide = layout.spill == SpillDirection::None ? rect.width() : qMax(rect.width(), need);
    const qreal left = layout.align == Qt::AlignRight ? rect.right() - wide
        : layout.align == Qt::AlignHCenter            ? rect.center().x() - wide / 2
                                                      : rect.left();
    out.box = QRectF(left + inset + iconRoom, rect.top(), wide - 2 * inset - iconRoom, rect.height());
    if (merged || out.span.first == out.span.last) {
        out.clip = rect;
    } else {
        const qreal x0 = columnStart(segment, out.span.first, width);
        const qreal x1 = columnStart(segment, out.span.last, width) + width(out.span.last);
        out.clip = QRectF(x0, rect.top(), x1 - x0, rect.height());
    }
    // Never past the segment's printed columns (the next page shows the rest).
    const qreal segLeft = segment.starts.first();
    const qreal segRight = segment.starts.last() + segment.lines.last().sizePt;
    out.clip = out.clip.intersected(QRectF(segLeft, out.clip.top(), segRight - segLeft, out.clip.height()));
    return out;
}

quint64 packed(qint32 row, qint32 col)
{
    return (quint64(quint32(col)) << 32) | quint32(row);
}

} // namespace

QList<Line> printedLines(const Sheet &sheet, qint32 first, qint32 last, bool rows, qreal scale)
{
    QList<Line> out;
    const QSet<qint32> filtered = rows ? sheet.filteredOutRows() : QSet<qint32>();
    for (qint32 i = first; i <= last; ++i) {
        const bool hidden = rows ? (sheet.isRowHidden(i) || filtered.contains(i)) : sheet.isColumnHidden(i);
        if (!hidden) {
            out.append({i, (rows ? sheet.rowHeight(i) : sheet.columnWidth(i)) * kPxToPt * scale});
        }
    }
    return out;
}

void paintPageCells(const Canvas &canvas, const PrintContext &ctx, const QList<Segment> &rows, const QList<Segment> &cols)
{
    QPainter &p = *canvas.p;
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    const Sheet::PageSetup &setup = *ctx.setup;
    const bool colour = !setup.blackAndWhite;
    const ColumnWidth width = [&sheet, &ctx](qint32 col) {
        return sheet.isColumnHidden(col) ? 0.0 : sheet.columnWidth(col) * kPxToPt * ctx.scale;
    };

    // The page's cells; a merged range from its first cell, over the lines
    // of its segments it covers.
    QList<PrintedCell> cells;
    for (const Segment &rowSeg : rows) {
        for (qsizetype cs = 0; cs < cols.size(); ++cs) {
            const Segment &colSeg = cols.at(cs);
            for (qsizetype r = 0; r < rowSeg.lines.size(); ++r) {
                for (qsizetype c = 0; c < colSeg.lines.size(); ++c) {
                    const CellAddr addr{rowSeg.lines.at(r).index, colSeg.lines.at(c).index};
                    QRectF rect(colSeg.starts.at(c), rowSeg.starts.at(r), colSeg.lines.at(c).sizePt, rowSeg.lines.at(r).sizePt);
                    const CellRange *merge = sheet.mergeCovering(addr);
                    if (merge != nullptr && merge->topLeft != addr) {
                        continue;
                    }
                    if (merge != nullptr) {
                        for (qsizetype rr = r + 1; rr < rowSeg.lines.size() && rowSeg.lines.at(rr).index <= merge->bottomRight.row; ++rr) {
                            rect.setBottom(rowSeg.starts.at(rr) + rowSeg.lines.at(rr).sizePt);
                        }
                        for (qsizetype cc = c + 1; cc < colSeg.lines.size() && colSeg.lines.at(cc).index <= merge->bottomRight.col; ++cc) {
                            rect.setRight(colSeg.starts.at(cc) + colSeg.lines.at(cc).sizePt);
                        }
                    }
                    cells.append({addr, rect, merge != nullptr, cs});
                }
            }
        }
    }

    // Pass 1: fills, data bars and (M90) sparklines.
    const SparklinePainter sparklines(*ctx.book, ctx.sheet);
    for (const PrintedCell &cell : std::as_const(cells)) {
        const qint32 styleId = sheet.styleIdAt(cell.addr);
        const CellStyle *style = styleId != 0 ? ctx.book->style(styleId) : nullptr;
        const CondFormat conditional = sheet.condFormats().isEmpty() ? CondFormat{} : ctx.book->condFormatFor(ctx.sheet, cell.addr);
        const QRectF device = canvas.rect(cell.rect);
        if (colour && conditional.background) {
            p.fillRect(device, QColor(conditional.background->r, conditional.background->g, conditional.background->b));
        } else if (colour && style != nullptr && style->bg) {
            p.fillRect(device, QColor(style->bg->r, style->bg->g, style->bg->b));
        }
        if (conditional.dataBar) {
            paintDataBar(p, device, *conditional.dataBar);
        }
        sparklines.paint(p, cell.addr, device, device.height() / qMax(qreal(1e-9), qreal(cell.rect.height())));
    }

    // The texts, the page's own and those running in from another page.
    QList<PrintText> texts;
    QSet<quint64> noRightLine;
    const auto keep = [&](const PrintText &text) {
        for (qint32 c = text.span.first; c < text.span.last; ++c) {
            noRightLine.insert(packed(text.addr.row, c));
        }
        texts.append(text);
    };
    for (const PrintedCell &cell : std::as_const(cells)) {
        if (const auto text = textOf(canvas, ctx, cell.addr, cell.rect, cell.merged, cols.at(cell.colSegment), width)) {
            keep(*text);
        }
    }
    const qreal reach = 16384 * kPxToPt * ctx.scale;
    for (const Segment &rowSeg : rows) {
        for (qsizetype r = 0; r < rowSeg.lines.size(); ++r) {
            const qint32 row = rowSeg.lines.at(r).index;
            for (const Segment &colSeg : cols) {
                if (colSeg.lines.isEmpty()) {
                    continue;
                }
                const qint32 firstCol = colSeg.lines.first().index;
                const qint32 lastCol = colSeg.lines.last().index;
                for (const int step : {-1, 1}) {
                    const qint32 from = step < 0 ? firstCol - 1 : lastCol + 1;
                    if (from < colSeg.minLine || from > colSeg.maxLine) {
                        continue;
                    }
                    const qint32 col = nearestStop(sheet, row, from, step, step < 0 ? colSeg.minLine : colSeg.maxLine, width, reach);
                    if (col < 0 || sheet.mergeCovering({row, col}) != nullptr) {
                        continue;
                    }
                    const QRectF rect(columnStart(colSeg, col, width), rowSeg.starts.at(r), width(col), rowSeg.lines.at(r).sizePt);
                    const auto text = textOf(canvas, ctx, {row, col}, rect, false, colSeg, width);
                    if (text && (step < 0 ? text->span.last >= firstCol : text->span.first <= lastCol)) {
                        keep(*text);
                    }
                }
            }
        }
    }

    // Pass 2: grid lines (Page Setup ▸ Sheet ▸ Gridlines), none under text
    // that runs on.
    if (setup.gridLines && !setup.draft) {
        p.setPen(QPen(kPrintGrid, qMax(1.0, 0.5 * canvas.dpp)));
        p.setBrush(Qt::NoBrush);
        for (const PrintedCell &cell : std::as_const(cells)) {
            const QRectF r = canvas.rect(cell.rect);
            const qint32 lastCol = cell.merged ? sheet.mergeCovering(cell.addr)->bottomRight.col : cell.addr.col;
            p.drawLine(r.topLeft(), r.topRight());
            p.drawLine(r.bottomLeft(), r.bottomRight());
            if (!noRightLine.contains(packed(cell.addr.row, cell.addr.col - 1))) {
                p.drawLine(r.topLeft(), r.bottomLeft());
            }
            if (!noRightLine.contains(packed(cell.addr.row, lastCol))) {
                p.drawLine(r.topRight(), r.bottomRight());
            }
        }
    }

    // Pass 3: icons and text.
    for (const PrintedCell &cell : std::as_const(cells)) {
        const CondFormat conditional = sheet.condFormats().isEmpty() ? CondFormat{} : ctx.book->condFormatFor(ctx.sheet, cell.addr);
        if (conditional.icon) {
            paintCfIcon(p, iconBox(canvas.rect(cell.rect)), *conditional.icon);
        }
    }
    for (const PrintText &text : std::as_const(texts)) {
        p.save();
        p.setClipRect(canvas.rect(text.clip), Qt::IntersectClip);
        p.setFont(text.font);
        p.setPen(text.ink);
        p.drawText(canvas.rect(text.box), text.flags, text.text);
        p.restore();
    }

    // Pass 4: borders.
    for (const PrintedCell &cell : std::as_const(cells)) {
        const qint32 styleId = sheet.styleIdAt(cell.addr);
        const CellStyle *style = styleId != 0 ? ctx.book->style(styleId) : nullptr;
        if (style == nullptr || style->borders == 0) {
            continue;
        }
        p.setPen(QPen(kInk, qMax(1.0, 0.75 * canvas.dpp)));
        const QRectF r = canvas.rect(cell.rect);
        if (style->borders & 1) {
            p.drawLine(r.topLeft(), r.bottomLeft());
        }
        if (style->borders & 2) {
            p.drawLine(r.topRight(), r.bottomRight());
        }
        if (style->borders & 4) {
            p.drawLine(r.topLeft(), r.topRight());
        }
        if (style->borders & 8) {
            p.drawLine(r.bottomLeft(), r.bottomRight());
        }
    }
}

} // namespace QindaOffice::Apps::QindaCalc::PrintDetail
