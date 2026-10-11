// SPDX-License-Identifier: GPL-3.0-or-later
// The sheet printer (calc_print.h, M78, D-087). Geometry is worked out in
// points (1/72 inch) from page_layout.h and turned into the printer's
// device pixels per drawing call: a world transform would scale the fonts
// a second time (QFont point sizes already follow the device's DPI). The
// cells of a page are calc_print_cells.cpp's (M82: Excel's overflow, as the
// grid draws it); this file draws the headings, pictures, headers and
// footers around them.

#include "calc_print.h"

#include "calc_print_internal.h"
#include "spreadsheet_document.h"

#include "header_footer.h"
#include "xlsx_print_assets.h"
#include "calc_print_notes.h"
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
using namespace PrintDetail;

namespace {

// The header or footer of a page: three sections on one band.
void paintHeaderFooter(const Canvas &canvas, const QString &code, bool header, const Sheet::PageSetup &setup,
                       const PrintLayout &layout, const HeaderFooterContext &context, const QFont &base, const IO::LegacyPrintAssets &assets, const QString &variant)
{
    if (code.isEmpty()) {
        return;
    }
    const HeaderFooterText text = parseHeaderFooter(code);
    const qreal scale = setup.scaleWithDoc ? layout.scale : 1.0;
    const qreal left = setup.alignWithMargins ? setup.marginLeft * 72 : 18;
    const qreal right = layout.paperPt.width() - (setup.alignWithMargins ? setup.marginRight * 72 : 18);
    QPainter &p = *canvas.p;
    const auto paintSection = [&](const HeaderFooterSection &section, Qt::Alignment align, const QString &position) {
        if (section.isEmpty()) {
            return;
        }
        // The runs as lines of (text, font, colour); a run's line breaks
        // start new lines.
        struct Piece {
            QString text;
            QFont font;
            QColor colour;
            QImage image;
            QSizeF size;
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
            if (run.field == HeaderFooterRun::Field::Picture) {
                const auto picture = assets.pictures.value(position + variant, assets.pictures.value(position));
                QImage image = QImage::fromData(picture.bytes);
                if (!image.isNull()) {
                    const auto crop = picture.crop;
                    if (crop.left() + crop.right() >= 1 || crop.top() + crop.bottom() >= 1) continue;
                    if (crop.left() + crop.right() < 1 && crop.top() + crop.bottom() < 1 && !crop.isNull())
                        image = image.copy(QRectF(image.width() * crop.left(), image.height() * crop.top(),
                                                 image.width() * (1 - crop.left() - crop.right()),
                                                 image.height() * (1 - crop.top() - crop.bottom())).toRect());
                    if (setup.blackAndWhite) image = image.convertToFormat(QImage::Format_Grayscale8);
                    const QSizeF size = picture.points.isEmpty() ? QSizeF(image.size()) * 0.75 : picture.points;
                    lines.last().append(Piece{{}, font, colour, image, size * scale});
                }
                continue;
            }
            const QStringList parts = headerFooterRunText(run, context).split(QLatin1Char('\n'));
            for (qsizetype i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    lines.append(QList<Piece>());
                }
                lines.last().append(Piece{parts.at(i), font, colour, {}, {}});
            }
        }
        qreal y = header ? setup.marginHeader * 72 : 0;
        qreal total = 0;
        QList<qreal> heights;
        for (const QList<Piece> &line : std::as_const(lines)) {
            qreal height = base.pointSizeF() * scale * 1.2;
            for (const Piece &piece : line) {
                height = qMax(height, piece.image.isNull() ? QFontMetricsF(piece.font, p.device()).height() / canvas.dpp : piece.size.height());
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
                width += piece.image.isNull() ? QFontMetricsF(piece.font, p.device()).horizontalAdvance(piece.text) / canvas.dpp : piece.size.width();
            }
            qreal x = align == Qt::AlignLeft ? left : align == Qt::AlignRight ? right - width
                                                                              : (layout.paperPt.width() - width) / 2;
            for (const Piece &piece : lines.at(l)) {
                if (!piece.image.isNull()) {
                    p.drawImage(canvas.rect(QRectF(QPointF(x, y), piece.size)), piece.image);
                    x += piece.size.width();
                    continue;
                }
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
    paintSection(text.left, Qt::AlignLeft, header ? QStringLiteral("LH") : QStringLiteral("LF"));
    paintSection(text.center, Qt::AlignHCenter, header ? QStringLiteral("CH") : QStringLiteral("CF"));
    paintSection(text.right, Qt::AlignRight, header ? QStringLiteral("RH") : QStringLiteral("RF"));
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

void paintPage(const Canvas &canvas, const PrintContext &ctx, const PrintLayout &layout, const PrintPage &page, const IO::LegacyPrintAssets &assets)
{
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    const Sheet::PageSetup &setup = *ctx.setup;
    QList<Line> rows;
    QList<Line> cols;
    if (page.titleRows) {
        rows += printedLines(sheet, page.titleRows->topLeft.row, page.titleRows->bottomRight.row, true, ctx.scale);
    }
    const qsizetype titleRowCount = rows.size();
    rows += printedLines(sheet, page.body.topLeft.row, page.body.bottomRight.row, true, ctx.scale);
    if (page.titleCols) {
        cols += printedLines(sheet, page.titleCols->topLeft.col, page.titleCols->bottomRight.col, false, ctx.scale);
    }
    const qsizetype titleColCount = cols.size();
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
    // The cells (calc_print_cells.cpp): titles and body as separate
    // segments, so a title's text runs on only over the titles and the
    // body's over its print area's columns, as on screen (M82).
    const CellRange area = page.area >= 0 && page.area < layout.areas.size() ? layout.areas.at(page.area) : layout.area;
    const auto segments = [](const QList<Line> &lines, const QList<qreal> &starts, qsizetype titleCount,
                             const std::optional<CellRange> &title, bool rowsAxis, const CellRange &bounds) {
        QList<Segment> out;
        if (titleCount > 0) {
            Segment segment{lines.mid(0, titleCount), starts.mid(0, titleCount), 0, 0};
            segment.minLine = rowsAxis ? title->topLeft.row : title->topLeft.col;
            segment.maxLine = rowsAxis ? title->bottomRight.row : title->bottomRight.col;
            out.append(segment);
        }
        if (lines.size() > titleCount) {
            Segment segment{lines.mid(titleCount), starts.mid(titleCount), 0, 0};
            segment.minLine = rowsAxis ? bounds.topLeft.row : bounds.topLeft.col;
            segment.maxLine = rowsAxis ? bounds.bottomRight.row : bounds.bottomRight.col;
            out.append(segment);
        }
        return out;
    };
    paintPageCells(canvas, ctx, segments(rows, ys, titleRowCount, page.titleRows, true, area),
                   segments(cols, xs, titleColCount, page.titleCols, false, area));
    if (setup.cellComments == 2) paintDisplayedNotes(canvas, ctx, rows, cols, xs, ys, assets);
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
        const auto assets = IO::legacyPrintAssets(book, sheet);
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
        PrintContext ctx;
        ctx.book = &book;
        ctx.sheet = sheet;
        ctx.setup = &setup;
        ctx.base = context.baseFont;
        ctx.scale = layout.scale;
        HeaderFooterContext fields;
        const auto notePages = endNotePages(canvas, ctx, layout);
        fields.pages = int(layout.pages.size() + notePages.size());
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
            paintPage(canvas, ctx, layout, page, assets);
            fields.page = page.number;
            const auto [header, footer] = headerFooterFor(setup, int(i), page.number);
            const QString variant = setup.differentFirst && i == 0 ? QStringLiteral("F")
                : setup.differentOddEven && page.number % 2 == 0 ? QStringLiteral("E") : QString();
            paintHeaderFooter(canvas, header, true, setup, layout, fields, context.baseFont, assets, variant);
            paintHeaderFooter(canvas, footer, false, setup, layout, fields, context.baseFont, assets, variant);
        }
        for (qsizetype n = 0; n < notePages.size(); ++n) {
            printer->newPage();
            paintEndNotes(canvas, ctx, layout, notePages.at(n));
            fields.page = layout.pages.last().number + int(n) + 1;
            const auto [header, footer] = headerFooterFor(setup, int(layout.pages.size() + n), fields.page);
            const QString variant = setup.differentOddEven && fields.page % 2 == 0 ? QStringLiteral("E") : QString();
            paintHeaderFooter(canvas, header, true, setup, layout, fields, context.baseFont, assets, variant);
            paintHeaderFooter(canvas, footer, false, setup, layout, fields, context.baseFont, assets, variant);
        }
    };
}

} // namespace QindaOffice::Apps::QindaCalc
