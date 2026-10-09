// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem: the cells of one pane (M82, D-091), painted in passes so
// a text running on over its neighbours is never painted over by them —
// fills (and data bars) first, then the grid lines (none under text that
// runs on, as in Excel), then icons and text, then comment markers, filter
// buttons, PivotTable rules and borders. A merged range is one block,
// painted even while its first cell is scrolled away.
//
// Text follows calc_text_overflow.h — the printer's rules too: left and
// General text runs right over empty cells, right-aligned text left,
// centred text both ways; numbers too wide show #### (General ones give up
// digits first). A text whose cell is scrolled out of the pane still shows
// the part that reaches in (nearestStop finds it).
//
// AGENT-GUARD: everything here is in GRID coordinates inside the pane's
// clip (paintGrid); cellRect()/colLeft() measure in the pane being painted.

#include "spreadsheet_item.h"

#include "calc_cell_look.h"
#include "calc_text_overflow.h"
#include "spreadsheet_document.h"

#include "number_format.h"
#include "sheet.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegion>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

enum BorderBit { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

// Document colours: the sheet is paper (D-004).
const QColor kPaper(Qt::white);
const QColor kInk(0x20, 0x20, 0x28);
const QColor kGridLine(0xd8, 0xd8, 0xdc);
const QColor kCommentMarker(0xc0, 0x39, 0x2b);
// Excel's hyperlink colour (the Office theme's hlink): document ink.
const QColor kLinkInk(0x05, 0x63, 0xc1);
constexpr qreal kInset = 4;
// How far beyond a pane's edge a text is looked for whose overflow reaches
// in (px at 100 %): about 2,700 characters of the default font.
constexpr qreal kSpillReach = 16384;

// The stock Excel table styles a real workbook names. headerText contrasts
// with the header fill; bandFill is the stripe colour. The engine stores
// only styleName (it is QtCore-only), so the palette lives here.
struct TableStyleColors {
    QColor headerFill;
    QColor headerText;
    QColor bandFill;
};

TableStyleColors tableStyleColors(const QString &styleName)
{
    static const QHash<QString, TableStyleColors> styles = {
        {QStringLiteral("TableStyleMedium1"), {QColor(0x2F, 0x5B, 0x95), QColor(Qt::white), QColor(0xD3, 0xDE, 0xEE)}},
        {QStringLiteral("TableStyleMedium2"), {QColor(0x44, 0x72, 0xC4), QColor(Qt::white), QColor(0xD9, 0xE1, 0xF2)}},
        {QStringLiteral("TableStyleMedium3"), {QColor(0xC6, 0x59, 0x11), QColor(Qt::white), QColor(0xF8, 0xCB, 0xAD)}},
        {QStringLiteral("TableStyleMedium4"), {QColor(0x7B, 0x7B, 0x7B), QColor(Qt::white), QColor(0xE2, 0xEF, 0xDA)}},
        {QStringLiteral("TableStyleMedium9"), {QColor(0x1F, 0x8A, 0x70), QColor(Qt::white), QColor(0xD1, 0xE4, 0xDF)}},
    };
    return styles.value(styleName, styles.value(QStringLiteral("TableStyleMedium2")));
}

QColor rgb(const Rgb &c)
{
    return QColor(c.r, c.g, c.b);
}

// One visible cell (or merged block) as this paint sees it.
struct CellLook {
    CellAddr addr;
    QRectF rect;
    const CellStyle *style = nullptr;
    CondFormat conditional;
    std::optional<PivotCellLook> pivot;
    bool selected = false;
    bool cursor = false;
    bool merged = false;
    bool skip = false; // covered by a merged block (painted with it)
};

// A text to draw: laid out in `box`, shown inside `clip` (the columns it
// spans, its row).
struct TextPiece {
    CellAddr addr;
    QRectF cell;
    QRectF clip;
    QRectF box;
    QString text;
    QFont font;
    QColor ink;
    int flags = 0;
    bool onCursor = false; // its own cell is the cursor's solid fill
    SpillSpan span;
};

quint64 packed(qint32 row, qint32 col)
{
    return (quint64(quint32(col)) << 32) | quint32(row);
}

} // namespace

void SpreadsheetItem::paintPaneCells(QPainter &p, const QVector<Band> &rows, const QVector<Band> &cols,
                                     const AxisPane &colPane)
{
    const Sheet *sheetPtr = activeSheet();
    if (sheetPtr == nullptr || rows.isEmpty() || cols.isEmpty()) {
        return;
    }
    const Sheet &sheet = *sheetPtr;
    const Workbook &wb = *m_document->workbook();
    const qint32 shownSheet = sheetIndex();
    const CellRange selection = selectionRange();
    const QColor selectionFill = m_selectionColor.lighter(160);
    // The cell font scales with the view, or zooming would only move the
    // grid lines and leave the text at its original size.
    const QFont baseFont = scaledFont();
    const ColumnWidth width = [this](qint32 col) { return colWidthPx(col); };
    // A frozen pane's text spills only over its own columns; the scrolling
    // pane's over the columns from its first line on.
    const qint32 minCol = colPane.base;
    const qint32 maxCol = colPane.limit >= 0 ? colPane.limit - 1 : Sheet::kMaxCols - 1;
    const qint32 firstRow = rows.first().index;
    const qint32 lastRow = rows.last().index;
    const qint32 firstCol = cols.first().index;
    const qint32 lastCol = cols.last().index;

    const auto lookOf = [&](const CellAddr &addr, const QRectF &rect, bool merged) {
        CellLook look;
        look.addr = addr;
        look.rect = rect;
        look.merged = merged;
        const qint32 styleId = sheet.styleIdAt(addr);
        // Styles live on EMPTY cells too (fill, borders).
        look.style = styleId != 0 ? wb.style(styleId) : nullptr;
        // Conditional formatting layers OVER the cell's own style and under
        // the selection; evaluated through the workbook so Expression and
        // DataBar rules resolve against live values.
        look.conditional = sheet.condFormats().isEmpty() ? CondFormat{} : wb.condFormatFor(shownSheet, addr);
        // A PivotTable's style (M74): drawn from its name, like a table's.
        look.pivot = sheet.pivots().isEmpty() ? std::nullopt : pivotLookAt(sheet, addr);
        look.selected = selection.contains(addr);
        look.cursor = addr == m_cursor;
        return look;
    };

    // ---- the cells in view, merged blocks as one ----
    QList<CellRange> merges;
    for (const CellRange &merge : sheet.merges()) {
        if (merge.topLeft.row <= lastRow && merge.bottomRight.row >= firstRow && merge.topLeft.col <= lastCol
            && merge.bottomRight.col >= firstCol) {
            merges.append(merge);
        }
    }
    const auto coveredBy = [&merges](const CellAddr &addr) -> const CellRange * {
        for (const CellRange &merge : merges) {
            if (merge.contains(addr)) {
                return &merge;
            }
        }
        return nullptr;
    };
    QList<CellLook> looks;
    looks.reserve(rows.size() * cols.size() + merges.size());
    for (const Band &rowBand : rows) {
        for (const Band &colBand : cols) {
            const CellAddr addr{rowBand.index, colBand.index};
            if (coveredBy(addr) != nullptr) {
                continue;
            }
            looks.append(lookOf(addr, QRectF(colBand.pos, rowBand.pos, colBand.size, rowBand.size), false));
        }
    }
    for (const CellRange &merge : std::as_const(merges)) {
        // Measured from its own corners, so a block whose first cell is
        // scrolled away still paints where it is.
        const QRectF first = cellRect(merge.topLeft);
        const QRectF last = cellRect(merge.bottomRight);
        looks.append(lookOf(merge.topLeft, QRectF(first.topLeft(), last.bottomRight()), true));
    }

    // ---- pass 1: fills, under everything ----
    for (const CellLook &look : std::as_const(looks)) {
        if (look.selected) {
            p.fillRect(look.rect, look.cursor ? m_selectionColor : selectionFill);
            continue;
        }
        if (look.pivot && look.pivot->fill) {
            p.fillRect(look.rect, rgb(*look.pivot->fill));
        }
        // A structured table's style is the LOWEST layer: direct fills and
        // conditional formats paint over it (Excel's precedence).
        if (const TableSpec *table = sheet.tableCovering(look.addr)) {
            const TableStyleColors colors = tableStyleColors(table->styleName);
            const bool header = table->headerRow && look.addr.row == table->range.topLeft.row;
            const bool totals = table->totalsRow && look.addr.row == table->range.bottomRight.row;
            const int bodyIndex = look.addr.row - table->range.topLeft.row - (table->headerRow ? 1 : 0);
            if (header) {
                p.fillRect(look.rect, colors.headerFill);
            } else if (totals) {
                // The totals row under its double rule (M69).
                p.fillRect(QRectF(look.rect.left(), look.rect.top(), look.rect.width(), 1), colors.headerFill);
                p.fillRect(QRectF(look.rect.left(), look.rect.top() + 2, look.rect.width(), 1), colors.headerFill);
            } else if (table->rowStripes && bodyIndex >= 0 && bodyIndex % 2 == 1) {
                p.fillRect(look.rect, colors.bandFill);
            }
        }
        if (look.conditional.background) {
            p.fillRect(look.rect, rgb(*look.conditional.background));
        } else if (look.style != nullptr && look.style->bg.has_value()) {
            p.fillRect(look.rect, rgb(*look.style->bg));
        }
        if (look.conditional.dataBar.has_value()) {
            paintDataBar(p, look.rect, *look.conditional.dataBar); // under the text (M78)
        }
    }

    // ---- the texts, and the grid lines they hide ----
    const auto pieceOf = [&](const CellAddr &addr, const QRectF &rect, const CellLook *look) -> std::optional<TextPiece> {
        const Cell *cellData = sheet.cellAt(addr);
        if (cellData == nullptr || isBlank(cellData->value)) {
            return std::nullopt;
        }
        const CellLook own = look != nullptr ? *look : lookOf(addr, rect, false);
        if (own.conditional.hideValue) {
            return std::nullopt;
        }
        const CellStyle *style = own.style;
        const QString code = style != nullptr && !style->numberFormat.isEmpty() ? style->numberFormat : QStringLiteral("General");
        const NumberFormat format(code);
        const FormattedText formatted = format.format(cellData->value);
        TextPiece piece;
        piece.addr = addr;
        piece.cell = rect;
        QFont font = baseFont;
        if (style != nullptr) {
            if (!style->fontFamily.isEmpty()) {
                font.setFamily(style->fontFamily);
            }
            if (style->fontSize > 0) {
                font.setPointSizeF(style->fontSize * m_zoom);
            }
            font.setBold(style->bold);
            font.setItalic(style->italic);
            font.setUnderline(style->underline);
            font.setStrikeOut(style->strikeout);
        }
        // A link reads as one (M66): underlined, in the link colour unless
        // the cell has a colour of its own.
        const bool link = hasLink(addr);
        font.setUnderline(font.underline() || link);
        if (own.pivot && own.pivot->bold && (style == nullptr || !style->bold)) {
            font.setBold(true);
        }
        const CondFormat &conditional = own.conditional;
        font.setBold(conditional.bold.value_or(font.bold()));
        font.setItalic(conditional.italic.value_or(font.italic()));
        font.setUnderline(conditional.underline.value_or(font.underline()));
        font.setStrikeOut(conditional.strikeout.value_or(font.strikeOut()));
        piece.font = font;
        if (conditional.foreground) {
            piece.ink = rgb(*conditional.foreground);
        } else if (formatted.colour.has_value()) {
            // A [Red]-style section colour wins over the font colour.
            piece.ink = rgb(*formatted.colour);
        } else if (style != nullptr && style->fg.has_value()) {
            piece.ink = rgb(*style->fg);
        } else if (own.pivot && own.pivot->text) {
            piece.ink = rgb(*own.pivot->text);
        } else if (link) {
            piece.ink = kLinkInk;
        } else if (const TableSpec *table = sheet.tableCovering(addr);
                   table != nullptr && table->headerRow && addr.row == table->range.topLeft.row) {
            piece.ink = tableStyleColors(table->styleName).headerText;
        } else {
            piece.ink = kInk;
        }
        piece.onCursor = own.selected && own.cursor;
        const CellHorizontal layout = horizontalLayout(cellData->value, style, own.merged);
        Qt::Alignment vertical = style != nullptr
            ? (style->vAlign == 0 ? Qt::AlignTop : style->vAlign == 2 ? Qt::AlignBottom : Qt::AlignVCenter)
            : Qt::AlignVCenter;
        int flags = int(layout.align);
        if (style != nullptr && style->wrap) {
            flags |= int(Qt::AlignTop) | int(Qt::TextWordWrap);
        } else {
            flags |= int(vertical);
        }
        piece.flags = flags;
        // A compact form's label sits one indent per level (M74); an icon
        // set's icon keeps the text clear of it (M78).
        const qreal indent = (own.pivot ? own.pivot->indent * 12.0 * m_zoom : 0.0)
            + (conditional.icon.has_value() && layout.align != Qt::AlignRight ? iconBox(rect).width() + 2 : 0.0);
        const QFontMetricsF metrics(font);
        const auto measure = [&metrics](const QString &text) { return metrics.horizontalAdvance(text); };
        const qreal room = rect.width() - 2 * kInset - indent;
        QString text = formatted.text;
        if (layout.numeric) {
            text = fittedNumber(std::get<double>(cellData->value), text, format.isGeneral(), room, measure);
        }
        if (layout.fill) {
            text = filledText(text, room, measure);
        }
        piece.text = text;
        const qreal need = measure(text) + 2 * kInset + indent;
        piece.span = own.merged ? SpillSpan{addr.col, addr.col}
                                : spillSpan(sheet, addr, layout.spill, need, width, minCol, maxCol);
        // A text that never spills (wrapped, filled, merged, a number) is laid
    // out in its own cell: wrapping needs the cell's width.
    const qreal wide = layout.spill == SpillDirection::None ? rect.width() : qMax(rect.width(), need);
        const qreal left = layout.align == Qt::AlignRight ? rect.right() - wide
            : layout.align == Qt::AlignHCenter            ? rect.center().x() - wide / 2
                                                          : rect.left();
        piece.box = QRectF(left + indent, rect.top(), wide - indent, rect.height());
        if (own.merged || piece.span.first == piece.span.last) {
            piece.clip = rect;
        } else {
            const qreal x0 = colLeft(piece.span.first);
            const qreal x1 = colLeft(piece.span.last) + colWidthPx(piece.span.last);
            piece.clip = QRectF(x0, rect.top(), x1 - x0, rect.height());
        }
        return piece;
    };

    QList<TextPiece> pieces;
    QSet<quint64> noRightLine; // (row, col) whose right grid line text crosses
    const auto keep = [&](const TextPiece &piece) {
        for (qint32 c = piece.span.first; c < piece.span.last; ++c) {
            noRightLine.insert(packed(piece.addr.row, c));
        }
        pieces.append(piece);
    };
    for (const CellLook &look : std::as_const(looks)) {
        if (const std::optional<TextPiece> piece = pieceOf(look.addr, look.rect, &look)) {
            keep(*piece);
        }
    }
    // Text whose cell is outside the pane but whose overflow reaches in.
    const qreal reach = kSpillReach * m_zoom;
    for (const Band &rowBand : rows) {
        const qint32 row = rowBand.index;
        for (const int step : {-1, 1}) {
            const qint32 from = step < 0 ? firstCol - 1 : lastCol + 1;
            if (from < minCol || from > maxCol) {
                continue;
            }
            const qint32 col = nearestStop(sheet, row, from, step, step < 0 ? minCol : maxCol, width, reach);
            if (col < 0 || sheet.mergeCovering({row, col}) != nullptr) {
                continue;
            }
            const CellAddr addr{row, col};
            const QRectF rect(colLeft(col), rowBand.pos, colWidthPx(col), rowBand.size);
            const std::optional<TextPiece> piece = pieceOf(addr, rect, nullptr);
            if (piece && (step < 0 ? piece->span.last >= firstCol : piece->span.first <= lastCol)) {
                keep(*piece);
            }
        }
    }

    // ---- pass 2: grid lines (not under text that runs on) ----
    p.setPen(kGridLine);
    for (const CellLook &look : std::as_const(looks)) {
        const qint32 rightCol = look.merged ? coveredBy(look.addr)->bottomRight.col : look.addr.col;
        p.drawLine(look.rect.bottomLeft(), look.rect.bottomRight());
        if (!noRightLine.contains(packed(look.addr.row, rightCol))) {
            p.drawLine(look.rect.topRight(), look.rect.bottomRight());
        }
    }

    // ---- pass 3: icons and text ----
    for (const CellLook &look : std::as_const(looks)) {
        // An icon set's icon at the cell's left edge (M78, D-087).
        if (look.conditional.icon.has_value()) {
            paintCfIcon(p, iconBox(look.rect), *look.conditional.icon);
        }
    }
    for (const TextPiece &piece : std::as_const(pieces)) {
        p.save();
        p.setFont(piece.font);
        const QRectF box = piece.box.adjusted(kInset, 1, -kInset, -1);
        if (piece.onCursor) {
            // The cursor's own cell is a solid fill: its text is paper
            // there and ink where it runs on.
            p.setClipRegion(QRegion(piece.clip.toAlignedRect()).subtracted(QRegion(piece.cell.toAlignedRect())),
                            Qt::IntersectClip);
            p.setPen(piece.ink);
            p.drawText(box, piece.flags, piece.text);
            p.restore();
            p.save();
            p.setFont(piece.font);
            p.setClipRect(piece.cell, Qt::IntersectClip);
            p.setPen(kPaper);
        } else {
            p.setClipRect(piece.clip, Qt::IntersectClip);
            p.setPen(piece.ink);
        }
        p.drawText(box, piece.flags, piece.text);
        p.restore();
    }

    // ---- pass 4: markers, buttons, rules and borders over the text ----
    const AutoFilter &filter = sheet.autoFilter();
    for (const CellLook &look : std::as_const(looks)) {
        const QRectF &cell = look.rect;
        const CellAddr &addr = look.addr;
        // A comment marker: the small corner triangle every spreadsheet
        // uses. The text itself is the hover tooltip.
        if (sheet.commentAt(addr) != nullptr) {
            const QPolygonF marker({cell.topRight() + QPointF(-7, 1), cell.topRight() + QPointF(-1, 1),
                                    cell.topRight() + QPointF(-1, 7)});
            p.setPen(Qt::NoPen);
            p.setBrush(kCommentMarker);
            p.drawPolygon(marker);
            p.setBrush(Qt::NoBrush);
        }
        // Filter dropdown on the header row of an active filter.
        if (filter.active && filter.range.isValid() && addr.row == filter.range.topLeft.row
            && addr.col >= filter.range.topLeft.col && addr.col <= filter.range.bottomRight.col) {
            const QRectF button(cell.right() - kFilterButtonPx, cell.top() + 2, kFilterButtonPx - 2, cell.height() - 4);
            p.setPen(m_headerLineColor);
            p.setBrush(filter.allowed.contains(addr.col) ? m_selectionColor : m_headerColor);
            p.drawRect(button);
            p.setPen(m_headerTextColor);
            const QPointF centre = button.center();
            p.drawPolygon(QPolygonF({centre + QPointF(-3, -1.5), centre + QPointF(3, -1.5), centre + QPointF(0, 2.5)}));
            p.setBrush(Qt::NoBrush);
        }
        if (look.pivot) {
            paintPivotRules(p, cell, *look.pivot);
        }
        if (look.style != nullptr && look.style->borders != 0) {
            p.setPen(QPen(kInk, 1));
            if (look.style->borders & kLeft) {
                p.drawLine(cell.topLeft(), cell.bottomLeft());
            }
            if (look.style->borders & kRight) {
                p.drawLine(cell.topRight(), cell.bottomRight());
            }
            if (look.style->borders & kTop) {
                p.drawLine(cell.topLeft(), cell.topRight());
            }
            if (look.style->borders & kBottom) {
                p.drawLine(cell.bottomLeft(), cell.bottomRight());
            }
        }
    }
}

void SpreadsheetItem::paintSplitBars(QPainter &p)
{
    if (!isSplit() || frozen().row > 0 || frozen().col > 0) {
        return;
    }
    // In ITEM coordinates, across the headers as well: the bars are chrome.
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(m_headerColor);
    const qreal left = headerWidth();
    const qreal top = headerHeight();
    QList<QRectF> bars;
    if (m_split.x > 0) {
        bars.append(QRectF(left + m_split.x, colGutterPx(), kSplitBar, height() - colGutterPx()));
    }
    if (m_split.y > 0) {
        bars.append(QRectF(rowGutterPx(), top + m_split.y, width() - rowGutterPx(), kSplitBar));
    }
    for (const QRectF &bar : std::as_const(bars)) {
        p.drawRect(bar);
    }
    p.setPen(m_headerLineColor);
    p.setBrush(Qt::NoBrush);
    for (const QRectF &bar : std::as_const(bars)) {
        p.drawRect(bar.adjusted(0, 0, -1, -1));
    }
    p.restore();
}

} // namespace QindaOffice::Apps::QindaCalc
