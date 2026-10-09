// SPDX-License-Identifier: GPL-3.0-or-later
// The sheet printer (calc_print.h, M78, D-087). Geometry is worked out in
// points (1/72 inch) from page_layout.h and turned into the printer's
// device pixels per drawing call: a world transform would scale the fonts
// a second time (QFont point sizes already follow the device's DPI).

#include "calc_print.h"

#include "calc_cell_look.h"
#include "spreadsheet_document.h"

#include "header_footer.h"
#include "number_format.h"
#include "page_layout.h"
#include "sheet.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>

#include <algorithm>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

constexpr qreal kPxToPt = 0.75; // the engine's sizes are pixels at 96 dpi
const QColor kInk(0x20, 0x20, 0x28);
const QColor kPrintGrid(0xb8, 0xb8, 0xbc);
const QColor kHeadingFill(0xf0, 0xf0, 0xf2);

// Points → the printer's device pixels, from the paper's corner.
struct Canvas {
    QPainter *p = nullptr;
    qreal dpp = 1;   // device pixels per point
    QPointF origin;  // the paper's corner in device pixels
    [[nodiscard]] QRectF rect(const QRectF &pt) const
    {
        return {origin.x() + pt.x() * dpp, origin.y() + pt.y() * dpp, pt.width() * dpp, pt.height() * dpp};
    }
    [[nodiscard]] QPointF point(qreal x, qreal y) const { return {origin.x() + x * dpp, origin.y() + y * dpp}; }
};

struct Line {
    qint32 index;
    qreal sizePt;
};

// The printed rows (or columns) of [first, last]: hidden ones take no space.
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

struct CellPaint {
    const Workbook *book = nullptr;
    qint32 sheet = 0;
    const Sheet::PageSetup *setup = nullptr;
    QFont base;
    qreal scale = 1;
};

QString printedText(const CellPaint &ctx, const Cell &cell, const CellStyle *style)
{
    if (const auto *err = std::get_if<Err>(&cell.value)) {
        Q_UNUSED(err);
        switch (ctx.setup->errors) {
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

// One cell's look on paper: fill, conditional format (bar, icon, colours),
// text, borders. `rect` is in points; `textRect` may reach over empty
// neighbours (Excel lets a left-aligned label overflow).
void paintCell(const Canvas &canvas, const CellPaint &ctx, const CellAddr &addr, const QRectF &rect, const QRectF &textRect)
{
    QPainter &p = *canvas.p;
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    const qint32 styleId = sheet.styleIdAt(addr);
    const CellStyle *style = styleId != 0 ? ctx.book->style(styleId) : nullptr;
    const CondFormat conditional = sheet.condFormats().isEmpty() ? CondFormat{} : ctx.book->condFormatFor(ctx.sheet, addr);
    const bool colour = !ctx.setup->blackAndWhite;
    const QRectF device = canvas.rect(rect);
    if (colour && conditional.background) {
        p.fillRect(device, QColor(conditional.background->r, conditional.background->g, conditional.background->b));
    } else if (colour && style != nullptr && style->bg) {
        p.fillRect(device, QColor(style->bg->r, style->bg->g, style->bg->b));
    }
    if (conditional.dataBar) {
        paintDataBar(p, device, *conditional.dataBar);
    }
    if (conditional.icon) {
        paintCfIcon(p, iconBox(device), *conditional.icon);
    }
    const Cell *cell = sheet.cellAt(addr);
    if (cell != nullptr && !isBlank(cell->value) && !conditional.hideValue) {
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
        p.setFont(font);
        const QString code = style != nullptr && !style->numberFormat.isEmpty() ? style->numberFormat : QStringLiteral("General");
        const FormattedText formatted = NumberFormat(code).format(cell->value);
        QColor ink = kInk;
        if (colour && conditional.foreground) {
            ink = QColor(conditional.foreground->r, conditional.foreground->g, conditional.foreground->b);
        } else if (colour && formatted.colour) {
            ink = QColor(formatted.colour->r, formatted.colour->g, formatted.colour->b);
        } else if (colour && style != nullptr && style->fg) {
            ink = QColor(style->fg->r, style->fg->g, style->fg->b);
        }
        p.setPen(ink);
        Qt::Alignment alignment = style != nullptr ? (style->vAlign == 0 ? Qt::AlignTop : style->vAlign == 2 ? Qt::AlignBottom : Qt::AlignVCenter)
                                                   : Qt::AlignVCenter;
        if (style != nullptr && style->alignment != 0) {
            alignment |= style->alignment == 1 ? Qt::AlignHCenter : Qt::AlignRight;
        } else {
            alignment |= isNumber(cell->value) || isBool(cell->value) || isErr(cell->value) ? Qt::AlignRight : Qt::AlignLeft;
        }
        int flags = int(alignment);
        if (style != nullptr && style->wrap) {
            flags |= int(Qt::TextWordWrap);
        }
        const qreal inset = 2 * ctx.scale;
        const qreal iconRoom = conditional.icon && !(alignment & Qt::AlignRight) ? iconBox(device).width() / canvas.dpp + 1 : 0;
        const QRectF target = (style != nullptr && style->wrap) || (alignment & Qt::AlignRight) ? rect : textRect;
        p.save();
        p.setClipRect(canvas.rect(target));
        p.drawText(canvas.rect(target.adjusted(inset + iconRoom, 0, -inset, 0)), flags, printedText(ctx, *cell, style));
        p.restore();
    }
    if (style != nullptr && style->borders != 0) {
        p.setPen(QPen(kInk, qMax(1.0, 0.75 * canvas.dpp)));
        const QRectF r = device;
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

// The header or footer of a page: three sections on one band.
void paintHeaderFooter(const Canvas &canvas, const QString &code, bool header, const Sheet::PageSetup &setup,
                       const PrintLayout &layout, const HeaderFooterContext &context, const QFont &base)
{
    if (code.isEmpty()) {
        return;
    }
    const HeaderFooterText text = parseHeaderFooter(code);
    const qreal scale = setup.scaleWithDoc ? layout.scale : 1.0;
    const qreal left = setup.alignWithMargins ? setup.marginLeft * 72 : 18;
    const qreal right = layout.paperPt.width() - (setup.alignWithMargins ? setup.marginRight * 72 : 18);
    QPainter &p = *canvas.p;
    const auto paintSection = [&](const HeaderFooterSection &section, Qt::Alignment align) {
        if (section.isEmpty()) {
            return;
        }
        // The runs as lines of (text, font, colour); a run's line breaks
        // start new lines.
        struct Piece {
            QString text;
            QFont font;
            QColor colour;
        };
        QList<QList<Piece>> lines{QList<Piece>()};
        for (const HeaderFooterRun &run : section.runs) {
            QFont font = base;
            if (!run.font.isEmpty() && run.font != QLatin1String("-")) {
                font.setFamily(run.font);
            }
            font.setPointSizeF(qMax(1.0, (run.size > 0 ? run.size : 11.0) * scale));
            font.setBold(run.bold);
            font.setItalic(run.italic);
            font.setUnderline(run.underline || run.doubleUnderline);
            font.setStrikeOut(run.strikeout);
            const QColor colour = run.color && !setup.blackAndWhite ? QColor(run.color->r, run.color->g, run.color->b) : kInk;
            const QStringList parts = headerFooterRunText(run, context).split(QLatin1Char('\n'));
            for (qsizetype i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    lines.append(QList<Piece>());
                }
                lines.last().append(Piece{parts.at(i), font, colour});
            }
        }
        qreal y = header ? setup.marginHeader * 72 : 0;
        qreal total = 0;
        QList<qreal> heights;
        for (const QList<Piece> &line : std::as_const(lines)) {
            qreal height = base.pointSizeF() * scale * 1.2;
            for (const Piece &piece : line) {
                height = qMax(height, QFontMetricsF(piece.font, p.device()).height() / canvas.dpp);
            }
            heights.append(height);
            total += height;
        }
        if (!header) {
            y = layout.paperPt.height() - setup.marginFooter * 72 - total;
        }
        for (qsizetype l = 0; l < lines.size(); ++l) {
            qreal width = 0;
            for (const Piece &piece : lines.at(l)) {
                width += QFontMetricsF(piece.font, p.device()).horizontalAdvance(piece.text) / canvas.dpp;
            }
            qreal x = align == Qt::AlignLeft ? left : align == Qt::AlignRight ? right - width
                                                                              : (layout.paperPt.width() - width) / 2;
            for (const Piece &piece : lines.at(l)) {
                p.setFont(piece.font);
                p.setPen(piece.colour);
                const qreal advance = QFontMetricsF(piece.font, p.device()).horizontalAdvance(piece.text) / canvas.dpp;
                p.drawText(canvas.rect(QRectF(x, y, advance + 1, heights.at(l))), int(Qt::AlignLeft | Qt::AlignBottom),
                           piece.text);
                x += advance;
            }
            y += heights.at(l);
        }
    };
    paintSection(text.left, Qt::AlignLeft);
    paintSection(text.center, Qt::AlignHCenter);
    paintSection(text.right, Qt::AlignRight);
}

// Which header and footer a page takes (Different First Page, Different
// Odd & Even Pages).
QPair<QString, QString> headerFooterFor(const Sheet::PageSetup &setup, int index, int number)
{
    if (setup.differentFirst && index == 0) {
        return {setup.firstHeader, setup.firstFooter};
    }
    if (setup.differentOddEven && number % 2 == 0) {
        return {setup.evenHeader, setup.evenFooter};
    }
    return {setup.oddHeader, setup.oddFooter};
}

void paintPage(const Canvas &canvas, const CellPaint &ctx, const PrintLayout &layout, const PrintPage &page)
{
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    const Sheet::PageSetup &setup = *ctx.setup;
    QList<Line> rows;
    QList<Line> cols;
    if (page.titleRows) {
        rows += printedLines(sheet, page.titleRows->topLeft.row, page.titleRows->bottomRight.row, true, ctx.scale);
    }
    rows += printedLines(sheet, page.body.topLeft.row, page.body.bottomRight.row, true, ctx.scale);
    if (page.titleCols) {
        cols += printedLines(sheet, page.titleCols->topLeft.col, page.titleCols->bottomRight.col, false, ctx.scale);
    }
    cols += printedLines(sheet, page.body.topLeft.col, page.body.bottomRight.col, false, ctx.scale);
    // The headings' bands and the grid's size on paper are the layout's
    // (page_layout.h's renderer contract), so centring matches pagination.
    const qreal headingW = layout.headingWidthPt;
    const qreal headingH = layout.headingHeightPt;
    const qreal x0 = layout.contentLeftPt
        + (setup.centerHorizontally ? qMax(0.0, (layout.contentWidthPt - page.printedSizePt.width()) / 2) : 0.0);
    const qreal y0 = layout.contentTopPt
        + (setup.centerVertically ? qMax(0.0, (layout.contentHeightPt - page.printedSizePt.height()) / 2) : 0.0);
    QPainter &p = *canvas.p;
    p.save();
    p.setClipRect(canvas.rect(QRectF(layout.contentLeftPt, layout.contentTopPt, layout.contentWidthPt,
                                     layout.contentHeightPt)));
    QList<qreal> xs;
    QList<qreal> ys;
    for (qreal x = x0 + headingW; const Line &col : std::as_const(cols)) {
        xs.append(x);
        x += col.sizePt;
    }
    for (qreal y = y0 + headingH; const Line &row : std::as_const(rows)) {
        ys.append(y);
        y += row.sizePt;
    }
    if (setup.headings) {
        QFont font = ctx.base;
        font.setPointSizeF(qMax(1.0, 9 * ctx.scale));
        p.setFont(font);
        for (qsizetype c = 0; c < cols.size(); ++c) {
            const QRectF box = canvas.rect(QRectF(xs.at(c), y0, cols.at(c).sizePt, headingH));
            p.fillRect(box, kHeadingFill);
            p.setPen(kPrintGrid);
            p.drawRect(box);
            p.setPen(kInk);
            p.drawText(box, Qt::AlignCenter, colName(cols.at(c).index));
        }
        for (qsizetype r = 0; r < rows.size(); ++r) {
            const QRectF box = canvas.rect(QRectF(x0, ys.at(r), headingW, rows.at(r).sizePt));
            p.fillRect(box, kHeadingFill);
            p.setPen(kPrintGrid);
            p.drawRect(box);
            p.setPen(kInk);
            p.drawText(box, Qt::AlignCenter, QString::number(rows.at(r).index + 1));
        }
    }
    for (qsizetype r = 0; r < rows.size(); ++r) {
        for (qsizetype c = 0; c < cols.size(); ++c) {
            const CellAddr addr{rows.at(r).index, cols.at(c).index};
            QRectF rect(xs.at(c), ys.at(r), cols.at(c).sizePt, rows.at(r).sizePt);
            const CellRange *merge = sheet.mergeCovering(addr);
            if (merge != nullptr && merge->topLeft != addr) {
                continue;
            }
            if (merge != nullptr) {
                for (qsizetype rr = r + 1; rr < rows.size() && rows.at(rr).index <= merge->bottomRight.row; ++rr) {
                    rect.setBottom(ys.at(rr) + rows.at(rr).sizePt);
                }
                for (qsizetype cc = c + 1; cc < cols.size() && cols.at(cc).index <= merge->bottomRight.col; ++cc) {
                    rect.setRight(xs.at(cc) + cols.at(cc).sizePt);
                }
            }
            // A label runs on over empty cells to its right (Excel's overflow).
            QRectF textRect = rect;
            for (qsizetype cc = c + 1; cc < cols.size(); ++cc) {
                const Cell *next = sheet.cellAt({addr.row, cols.at(cc).index});
                if ((next != nullptr && !isBlank(next->value)) || sheet.mergeCovering({addr.row, cols.at(cc).index})) {
                    break;
                }
                textRect.setRight(xs.at(cc) + cols.at(cc).sizePt);
            }
            if (setup.gridLines && !setup.draft) {
                p.setPen(QPen(kPrintGrid, qMax(1.0, 0.5 * canvas.dpp)));
                p.setBrush(Qt::NoBrush);
                p.drawRect(canvas.rect(rect));
            }
            paintCell(canvas, ctx, addr, rect, textRect);
        }
    }
    // Pictures anchored on the page's cells (not in draft quality).
    for (const ImageSpec &image : ctx.book->images) {
        if (setup.draft || image.sheet != ctx.sheet || !image.isValid()) {
            continue;
        }
        const qsizetype r = std::find_if(rows.cbegin(), rows.cend(), [&](const Line &l) { return l.index == image.anchorRow; }) - rows.cbegin();
        const qsizetype c = std::find_if(cols.cbegin(), cols.cend(), [&](const Line &l) { return l.index == image.anchorCol; }) - cols.cbegin();
        if (r >= rows.size() || c >= cols.size()) {
            continue;
        }
        const QImage picture = QImage::fromData(*image.bytes);
        const QSizeF size(image.widthPx > 0 ? image.widthPx : picture.width(), image.heightPx > 0 ? image.heightPx : picture.height());
        p.drawImage(canvas.rect(QRectF(xs.at(c), ys.at(r), size.width() * kPxToPt * ctx.scale, size.height() * kPxToPt * ctx.scale)),
                    picture);
    }
    p.restore();
}

} // namespace

QPageLayout sheetPageLayout(const SpreadsheetDocument *document, qint32 sheet)
{
    const Sheet::PageSetup &setup = document->workbook()->sheetAt(sheet).pageSetup();
    const QSizeF mm = paperSizeMm(setup.paperSize);
    return QPageLayout(QPageSize(mm, QPageSize::Millimeter, QString(), QPageSize::ExactMatch),
                       setup.orientation == 1 ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
}

Quick::PrintRenderCallback sheetPrintRender(const SpreadsheetDocument *document, qint32 sheet,
                                            const SheetPrintContext &context)
{
    return [document, sheet, context](QPrinter *printer, QPainter *painter) {
        if (sheet < 0 || sheet >= document->workbook()->sheetCount()) {
            return;
        }
        const Workbook &book = *document->workbook();
        const PrintLayout layout = layoutPrintPages(book, sheet);
        if (layout.pages.isEmpty()) {
            return;
        }
        const Sheet::PageSetup &setup = book.sheetAt(sheet).pageSetup();
        Canvas canvas;
        canvas.p = painter;
        canvas.dpp = printer->resolution() / 72.0;
        // Device coordinates start at the printable area; the layout's at
        // the paper's corner.
        canvas.origin = -printer->pageRect(QPrinter::DevicePixel).topLeft();
        CellPaint ctx;
        ctx.book = &book;
        ctx.sheet = sheet;
        ctx.setup = &setup;
        ctx.base = context.baseFont;
        ctx.scale = layout.scale;
        HeaderFooterContext fields;
        fields.pages = int(layout.pages.size());
        fields.date = QDate::currentDate();
        fields.time = QTime::currentTime();
        fields.fileName = context.fileName;
        fields.filePath = context.filePath;
        fields.sheetName = book.sheetName(sheet);
        for (qsizetype i = 0; i < layout.pages.size(); ++i) {
            if (i > 0) {
                printer->newPage();
            }
            const PrintPage &page = layout.pages.at(i);
            paintPage(canvas, ctx, layout, page);
            fields.page = page.number;
            const auto [header, footer] = headerFooterFor(setup, int(i), page.number);
            paintHeaderFooter(canvas, header, true, setup, layout, fields, context.baseFont);
            paintHeaderFooter(canvas, footer, false, setup, layout, fields, context.baseFont);
        }
    };
}

} // namespace QindaOffice::Apps::QindaCalc
