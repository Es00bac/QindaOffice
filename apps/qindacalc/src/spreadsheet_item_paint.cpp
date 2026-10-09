// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem painting: the header bands, the cell grid and the
// paint() that drives them. Moved from the Widgets view nearly verbatim:
// palette roles became the chrome colour properties, the document
// colours are fixed (D-004: paper stays paper).
//
// AGENT-GUARD: the grid is painted inside p.translate(headerWidth(),
// headerHeight()), so cellRect() is ALREADY correct in that block. Adding the
// header offset again there draws a full header down and to the right of the
// cell it belongs to — use cursorItemRect() only OUTSIDE the transform.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "number_format.h"
#include "sheet.h"

#include <QPainter>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

enum BorderBit { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

// Document colours: the sheet is paper.
const QColor kPaper(Qt::white);
const QColor kInk(0x20, 0x20, 0x28);
const QColor kGridLine(0xd8, 0xd8, 0xdc);
const QColor kCommentMarker(0xc0, 0x39, 0x2b);
// Excel's hyperlink colour (the Office theme's hlink): document ink, like
// a conditional format's colour, not chrome (D-004).
const QColor kLinkInk(0x05, 0x63, 0xc1);

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

} // namespace

void SpreadsheetItem::paintHeaderBands(QPainter &p)
{
    const CellRange selection = selectionRange();
    const QColor selectedHeader = m_selectionColor.lighter(170);
    // The headers sit inside the outline gutter (M66): left of the row
    // headers, above the column headers; both are zero wide when the sheet
    // is not grouped.
    const qreal left = rowGutterPx();
    const qreal top = colGutterPx();

    const QRectF corner(left, top, kHeaderWidth, kHeaderHeight);
    p.fillRect(corner, m_headerColor);
    p.setPen(m_headerLineColor);
    // The corner box selects the whole sheet: the little triangle every
    // spreadsheet uses.
    const QPolygonF marker({QPointF(left + kHeaderWidth - 6, top + 5),
                            QPointF(left + kHeaderWidth - 6, top + kHeaderHeight - 5),
                            QPointF(left + 6, top + kHeaderHeight - 5)});
    p.setBrush(m_headerTextColor);
    p.drawPolygon(marker);
    p.setBrush(Qt::NoBrush);

    p.save();
    p.translate(headerWidth(), top);
    for (const Band &band : visibleColumnBands()) {
        const QRectF r(band.pos, 0, band.size, kHeaderHeight);
        const bool selected =
            band.index >= selection.topLeft.col && band.index <= selection.bottomRight.col;
        p.fillRect(r, selected ? selectedHeader : m_headerColor);
        p.setPen(m_headerTextColor);
        p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, colName(band.index));
        p.setPen(m_headerLineColor);
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomRight());
    }
    p.restore();

    p.save();
    p.translate(left, headerHeight());
    for (const Band &band : visibleRowBands()) {
        const QRectF r(0, band.pos, kHeaderWidth, band.size);
        const bool selected =
            band.index >= selection.topLeft.row && band.index <= selection.bottomRight.row;
        p.fillRect(r, selected ? selectedHeader : m_headerColor);
        p.setPen(m_headerTextColor);
        p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, QString::number(band.index + 1));
        p.setPen(m_headerLineColor);
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.bottomRight(), r.topRight());
    }
    p.restore();
    paintOutline(p);
}

void SpreadsheetItem::paintGrid(QPainter &p)
{
    const Sheet *sheetPtr = activeSheet();
    if (sheetPtr == nullptr) {
        return;
    }
    const Workbook &wb = *m_document->workbook();
    const Sheet &sheet = *sheetPtr;
    const QVector<Band> rows = visibleRowBands();
    const QVector<Band> cols = visibleColumnBands();
    const CellRange selection = selectionRange();
    const QColor selectionFill = m_selectionColor.lighter(160);

    p.save();
    p.translate(headerWidth(), headerHeight());
    p.setClipRect(QRectF(0, 0, viewportWidth(), viewportHeight()));

    // The cell font scales with the view, or zooming would only move the
    // grid lines and leave the text at its original size.
    const QFont baseFont = scaledFont();

    // Merged blocks are painted once, from their top-left cell, over the
    // whole span; the covered cells are skipped entirely.
    const auto bandFor = [](const QVector<Band> &bands, qint32 index) -> const Band * {
        for (const Band &band : bands) {
            if (band.index == index) {
                return &band;
            }
        }
        return nullptr;
    };

    for (const Band &rowBand : rows) {
        for (const Band &colBand : cols) {
            const CellAddr addr{rowBand.index, colBand.index};
            QRectF cell(colBand.pos, rowBand.pos, colBand.size, rowBand.size);
            const CellRange *merge = sheet.mergeCovering(addr);
            if (merge != nullptr) {
                if (merge->topLeft != addr) {
                    continue; // painted by the anchor cell
                }
                const Band *lastRow = bandFor(rows, merge->bottomRight.row);
                const Band *lastCol = bandFor(cols, merge->bottomRight.col);
                if (lastRow != nullptr) {
                    cell.setBottom(lastRow->pos + lastRow->size);
                }
                if (lastCol != nullptr) {
                    cell.setRight(lastCol->pos + lastCol->size);
                }
            }
            const bool inSelection = selection.contains(addr);
            const bool isCursor = addr == m_cursor;
            if (inSelection) {
                p.fillRect(cell, isCursor ? m_selectionColor : selectionFill);
            }
            const Cell *cellData = sheet.cellAt(addr);
            const qint32 styleId = sheet.styleIdAt(addr);
            // Styles live on EMPTY cells too (fill, borders).
            const CellStyle *style = styleId != 0 ? wb.style(styleId) : nullptr;
            // Conditional formatting layers OVER the cell's own style and
            // under the selection: a rule that hid the selection would make
            // the cursor invisible on a formatted block. Evaluation goes
            // through the workbook so Expression and DataBar rules resolve
            // against live values.
            const CondFormat conditional = sheet.condFormats().isEmpty()
                ? CondFormat{}
                : wb.condFormatFor(m_document->activeSheet(), addr);
            // A PivotTable's style (M74): drawn from its name, like a table's.
            const std::optional<PivotCellLook> pivotLook =
                sheet.pivots().isEmpty() ? std::nullopt : pivotLookAt(sheet, addr);
            if (!inSelection && pivotLook && pivotLook->fill) {
                p.fillRect(cell, QColor(pivotLook->fill->r, pivotLook->fill->g, pivotLook->fill->b));
            }
            if (!inSelection) {
                // A structured table's style is the LOWEST layer: direct
                // cell fills and conditional formats paint over it (Excel's
                // precedence), the header row paints its style's fill.
                if (const TableSpec *table = sheet.tableCovering(addr)) {
                    const TableStyleColors colors = tableStyleColors(table->styleName);
                    const bool header = table->headerRow && addr.row == table->range.topLeft.row;
                    const bool totals = table->totalsRow && addr.row == table->range.bottomRight.row;
                    const int bodyIndex = addr.row - table->range.topLeft.row
                        - (table->headerRow ? 1 : 0);
                    if (header) {
                        p.fillRect(cell, colors.headerFill);
                    } else if (totals) {
                        // The totals row under its double rule, as the
                        // table styles draw it (M69).
                        p.fillRect(QRectF(cell.left(), cell.top(), cell.width(), 1), colors.headerFill);
                        p.fillRect(QRectF(cell.left(), cell.top() + 2, cell.width(), 1), colors.headerFill);
                    } else if (table->rowStripes && bodyIndex >= 0 && bodyIndex % 2 == 1) {
                        p.fillRect(cell, colors.bandFill);
                    }
                }
                if (conditional.background) {
                    p.fillRect(cell, QColor(conditional.background->r, conditional.background->g,
                                            conditional.background->b));
                } else if (style != nullptr && style->bg.has_value()) {
                    p.fillRect(cell, QColor((*style->bg).r, (*style->bg).g, (*style->bg).b));
                }
                // A data bar under the text, translucent the way Excel's
                // bars sit over the cell's own fill.
                if (conditional.dataBar.has_value()) {
                    const DataBarFill &bar = *conditional.dataBar;
                    QRectF barRect = cell;
                    barRect.setWidth(cell.width() * bar.fraction);
                    p.fillRect(barRect,
                               QColor(bar.color.r, bar.color.g, bar.color.b, 115));
                }
            }
            if (cellData != nullptr && !isBlank(cellData->value)) {
                const FormattedText formatted =
                    NumberFormat(style != nullptr && !style->numberFormat.isEmpty()
                                     ? style->numberFormat
                                     : QStringLiteral("General"))
                        .format(cellData->value);
                QFont cellFont = baseFont;
                if (style != nullptr) {
                    if (!style->fontFamily.isEmpty()) {
                        cellFont.setFamily(style->fontFamily);
                    }
                    if (style->fontSize > 0) {
                        cellFont.setPointSizeF(style->fontSize * m_zoom);
                    }
                    cellFont.setBold(style->bold);
                    cellFont.setItalic(style->italic);
                    cellFont.setUnderline(style->underline);
                    cellFont.setStrikeOut(style->strikeout);
                }
                // A link reads as one (M66): underlined, in the link colour
                // unless the cell has a colour of its own.
                const bool link = hasLink(addr);
                if (link) {
                    cellFont.setUnderline(true);
                }
                if (pivotLook && pivotLook->bold && (style == nullptr || !style->bold)) {
                    cellFont.setBold(true);
                }
                if (conditional.bold) {
                    cellFont.setBold(*conditional.bold);
                }
                if (conditional.italic) {
                    cellFont.setItalic(*conditional.italic);
                }
                p.setFont(cellFont);
                if (inSelection && isCursor) {
                    p.setPen(kPaper);
                } else if (conditional.foreground) {
                    p.setPen(QColor(conditional.foreground->r, conditional.foreground->g,
                                    conditional.foreground->b));
                } else if (formatted.colour.has_value()) {
                    // A [Red]-style section colour in the number format wins
                    // over the font colour (Excel's order).
                    p.setPen(QColor((*formatted.colour).r, (*formatted.colour).g,
                                    (*formatted.colour).b));
                } else if (style != nullptr && style->fg.has_value()) {
                    p.setPen(QColor((*style->fg).r, (*style->fg).g, (*style->fg).b));
                } else if (pivotLook && pivotLook->text) {
                    p.setPen(QColor(pivotLook->text->r, pivotLook->text->g, pivotLook->text->b));
                } else if (link) {
                    p.setPen(kLinkInk);
                } else if (const TableSpec *table = sheet.tableCovering(addr);
                           table != nullptr && table->headerRow
                           && addr.row == table->range.topLeft.row) {
                    p.setPen(tableStyleColors(table->styleName).headerText);
                } else {
                    p.setPen(kInk);
                }
                // Explicit alignment wins; otherwise numbers right, text left.
                Qt::Alignment alignment = style != nullptr
                    ? (style->vAlign == 0 ? Qt::AlignTop
                                          : style->vAlign == 2 ? Qt::AlignBottom : Qt::AlignVCenter)
                    : Qt::AlignVCenter;
                if (style != nullptr && style->alignment != 0) {
                    alignment |= style->alignment == 1 ? Qt::AlignHCenter : Qt::AlignRight;
                } else {
                    alignment |= isNumber(cellData->value) || isBool(cellData->value)
                        ? Qt::AlignRight
                        : Qt::AlignLeft;
                }
                int flags = int(alignment);
                if (style != nullptr && style->wrap) {
                    flags = (flags & ~int(Qt::AlignVertical_Mask)) | int(Qt::AlignTop)
                        | int(Qt::TextWordWrap);
                }
                // A compact form's label sits one indent per level (M74).
                const qreal indent = pivotLook ? pivotLook->indent * 12.0 * m_zoom : 0.0;
                p.drawText(cell.adjusted(4 + indent, 1, -4, -1), flags, formatted.text);
            }
            // A comment marker: the small corner triangle every spreadsheet
            // uses. The text itself is the hover tooltip.
            if (sheet.commentAt(addr) != nullptr) {
                const QPolygonF marker({cell.topRight() + QPointF(-7, 1),
                                        cell.topRight() + QPointF(-1, 1),
                                        cell.topRight() + QPointF(-1, 7)});
                p.setPen(Qt::NoPen);
                p.setBrush(kCommentMarker);
                p.drawPolygon(marker);
                p.setBrush(Qt::NoBrush);
            }
            // Filter dropdown on the header row of an active filter.
            const AutoFilter &filter = sheet.autoFilter();
            if (filter.active && filter.range.isValid()
                && addr.row == filter.range.topLeft.row
                && addr.col >= filter.range.topLeft.col
                && addr.col <= filter.range.bottomRight.col) {
                const QRectF button(cell.right() - kFilterButtonPx, cell.top() + 2,
                                    kFilterButtonPx - 2, cell.height() - 4);
                p.setPen(m_headerLineColor);
                p.setBrush(filter.allowed.contains(addr.col) ? m_selectionColor : m_headerColor);
                p.drawRect(button);
                p.setPen(m_headerTextColor);
                const QPointF centre = button.center();
                p.drawPolygon(QPolygonF({centre + QPointF(-3, -1.5), centre + QPointF(3, -1.5),
                                         centre + QPointF(0, 2.5)}));
                p.setBrush(Qt::NoBrush);
            }
            p.setPen(kGridLine);
            p.drawLine(cell.bottomLeft(), cell.bottomRight());
            p.drawLine(cell.topRight(), cell.bottomRight());
            if (pivotLook) {
                paintPivotRules(p, cell, *pivotLook);
            }
            if (style != nullptr && style->borders != 0) {
                p.setPen(QPen(kInk, 1));
                if (style->borders & kLeft) {
                    p.drawLine(cell.topLeft(), cell.bottomLeft());
                }
                if (style->borders & kRight) {
                    p.drawLine(cell.topRight(), cell.bottomRight());
                }
                if (style->borders & kTop) {
                    p.drawLine(cell.topLeft(), cell.topRight());
                }
                if (style->borders & kBottom) {
                    p.drawLine(cell.bottomLeft(), cell.bottomRight());
                }
            }
        }
    }

    paintPivotPlaceholders(p); // an empty PivotTable (M74)
    paintImages(p);

    paintSpillBorder(p);

    // Cursor outline last, in GRID coordinates (see the file comment).
    p.setPen(QPen(m_cursorColor, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cellRect(m_cursor));

    // The frozen split, so the user can see where the sheet stops scrolling.
    if (frozen().row > 0 || frozen().col > 0) {
        p.setPen(QPen(m_headerLineColor.darker(150), 2));
        if (frozen().row > 0) {
            const qreal y = frozenHeightPx();
            p.drawLine(QPointF(0, y), QPointF(viewportWidth(), y));
        }
        if (frozen().col > 0) {
            const qreal x = frozenWidthPx();
            p.drawLine(QPointF(x, 0), QPointF(x, viewportHeight()));
        }
    }
    p.restore();
}

// Floating pictures (M39): drawn over the cells they cover and under the
// cursor, in GRID coordinates like everything else in paintGrid, so they
// scroll and freeze with the sheet. A spec with no size falls back to the
// image's own pixels.
void SpreadsheetItem::paintImages(QPainter &p)
{
    if (m_document == nullptr) {
        return;
    }
    const CalcEng::Workbook *book = m_document->workbook();
    const qint32 active = m_document->activeSheet();
    for (const CalcEng::ImageSpec &spec : book->images) {
        if (spec.sheet != active || !spec.isValid()) {
            continue;
        }
        const auto key = quintptr(spec.bytes.data());
        auto it = m_imageCache.constFind(key);
        if (it == m_imageCache.constEnd()) {
            it = m_imageCache.insert(key, {spec.bytes, QImage::fromData(*spec.bytes)});
        }
        const QImage &image = it.value().image;
        if (image.isNull()) {
            continue; // a format this build cannot decode: leave the cells alone
        }
        const QRectF anchor = cellRect({spec.anchorRow, spec.anchorCol});
        const QSizeF size(spec.widthPx > 0 ? spec.widthPx : image.width(),
                          spec.heightPx > 0 ? spec.heightPx : image.height());
        p.drawImage(QRectF(anchor.topLeft(), size), image);
    }
}

void SpreadsheetItem::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), kPaper);
    paintGrid(*painter);
    paintHeaderBands(*painter);
}

} // namespace QindaOffice::Apps::QindaCalc
