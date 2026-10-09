// SPDX-License-Identifier: GPL-3.0-or-later
// CheckRenderer: one voucher check per page (check_print.h).
#include "check_print.h"

#include "paint_util.h"

#include <QFontMetricsF>
#include <QPageLayout>
#include <QPagedPaintDevice>
#include <QPainter>

#include <cmath>

namespace QindaOffice::Books {

using namespace Paint;

namespace {

// AGENT-NOTE (positions): POINTS from the SHEET's top-left corner, for the
// common US voucher check stock (Intuit / Deluxe-compatible: the check is
// the top 3.5 in of a letter sheet, two 3.5 in stubs follow). The check
// third prints values only — the stock pre-prints the labels — so each
// value sits on its pre-printed line: these are BASELINES. Moving one is a
// visible misprint on real stock; tst_books_io_checks pins which band of
// the page each value lands in.
constexpr qreal kInch = 72;
constexpr qreal kCheckHeight = 3.5 * kInch;
constexpr qreal kDateX = 6.55 * kInch;
constexpr qreal kDateBase = 0.78 * kInch;
constexpr qreal kPayeeX = 1.05 * kInch;
constexpr qreal kPayeeBase = 1.30 * kInch;
constexpr qreal kFiguresX = 6.75 * kInch;
constexpr qreal kWordsX = 0.40 * kInch;
constexpr qreal kWordsEnd = 7.25 * kInch; // where the stock's "DOLLARS" starts
constexpr qreal kWordsBase = 1.66 * kInch;
constexpr qreal kAddressX = 0.80 * kInch; // inside a #10 double-window envelope's lower window
constexpr qreal kAddressBase = 1.98 * kInch;
constexpr qreal kAddressPitch = 12;
constexpr int kAddressLines = 5;
constexpr qreal kMemoX = 0.75 * kInch;
constexpr qreal kMemoBase = 2.92 * kInch;
constexpr qreal kMemoEnd = 4.0 * kInch;
// The stubs.
constexpr qreal kStubLeft = 0.5 * kInch;
constexpr qreal kStubRight = 8.0 * kInch;
constexpr qreal kStubPad = 0.32 * kInch;
constexpr qreal kCellPad = 8;

struct Fonts {
    QFont value = font(11);
    QFont figures = font(11, QFont::DemiBold);
    QFont heading = font(10, QFont::DemiBold);
    QFont body = font(9);
    QFont label = font(8, QFont::DemiBold);
    QFont small = font(8);
    Fonts() { label.setLetterSpacing(QFont::PercentageSpacing, 108); }
};

// Scales to points and puts the origin on the paper's corner: stock is
// measured from the sheet's edge, not from the printer's printable area.
void beginSheet(QPagedPaintDevice *device, QPainter *painter)
{
    beginPage(device, painter); // resets the transform, scales to points
    const QPageLayout layout = device->pageLayout();
    const QRectF full = layout.fullRectPoints();
    const QRectF paint = layout.paintRectPoints();
    painter->translate(full.left() - paint.left(), full.top() - paint.top());
}

void onLine(QPainter *p, qreal x, qreal baseline, const QString &text, const QFont &f, const QColor &color = kInk,
            bool alignRight = false)
{
    drawText(p, x, baseline - QFontMetricsF(f).ascent(), text, f, color, alignRight);
}

// The words, shrunk until they fit the line, then starred to its end so
// nothing can be written after them.
void drawWords(QPainter *p, const QString &words)
{
    const qreal width = kWordsEnd - kWordsX;
    int px = 11;
    while (px > 7 && textWidth(font(px), words) > width) {
        --px;
    }
    const QFont f = font(px);
    QString line = elide(f, words, width);
    const qreal star = textWidth(f, QStringLiteral("*"));
    if (star > 0) {
        const int stars = int(std::floor((width - textWidth(f, line + QLatin1Char(' '))) / star));
        if (stars > 0) {
            line += QLatin1Char(' ') + QString(stars, QLatin1Char('*'));
        }
    }
    onLine(p, kWordsX, kWordsBase, line, f);
}

void drawCheck(QPainter *p, const PrintableCheck &c, const Fonts &f)
{
    onLine(p, kDateX, kDateBase, c.date, f.value);
    onLine(p, kPayeeX, kPayeeBase, elide(f.value, c.payee, kFiguresX - kPayeeX - 24), f.value);
    onLine(p, kFiguresX, kPayeeBase, c.amount, f.figures);
    drawWords(p, c.amountWords);
    for (int i = 0; i < qMin(kAddressLines, int(c.address.size())); ++i) {
        onLine(p, kAddressX, kAddressBase + i * kAddressPitch, elide(f.body, c.address.at(i), 3.4 * kInch), f.body);
    }
    if (!c.memo.isEmpty()) {
        onLine(p, kMemoX, kMemoBase, elide(f.body, c.memo, kMemoEnd - kMemoX), f.body);
    }
}

void drawStub(QPainter *p, const PrintableCheck &c, qreal top, const Fonts &f)
{
    const qreal width = kStubRight - kStubLeft;
    qreal y = top + kStubPad;
    QString facts = c.date;
    if (!c.number.isEmpty()) {
        facts += QStringLiteral("    No. ") + c.number;
    }
    drawText(p, kStubRight, y, facts, f.heading, kInk, true);
    drawText(p, kStubLeft, y, elide(f.heading, c.payee, width - textWidth(f.heading, facts) - 24), f.heading);
    y += lineHeight(f.heading) + 2;
    if (!c.memo.isEmpty()) {
        drawText(p, kStubLeft, y, elide(f.small, QStringLiteral("Memo: ") + c.memo, width), f.small, kMuted);
    }
    y += lineHeight(f.small) + 8;
    // The table: weights share the width; numbers align right.
    int weights = 0;
    for (const PrintableColumn &col : c.columns) {
        weights += col.weight;
    }
    QList<qreal> x;
    QList<qreal> w;
    qreal at = kStubLeft;
    for (const PrintableColumn &col : c.columns) {
        x << at;
        w << width * col.weight / qMax(1, weights);
        at += w.last();
    }
    const auto cell = [&](int column, qreal rowTop, const QString &text, const QFont &cellFont, const QColor &color) {
        const PrintableColumn &col = c.columns.at(column);
        const QString fitted = elide(cellFont, text, w[column] - kCellPad);
        drawText(p, col.alignRight ? x[column] + w[column] : x[column], rowTop, fitted, cellFont, color, col.alignRight);
    };
    for (int i = 0; i < c.columns.size(); ++i) {
        cell(i, y, c.columns.at(i).title.toUpper(), f.label, kMuted);
    }
    y += lineHeight(f.label) + 3;
    p->setPen(QPen(kRule, 0.75));
    p->drawLine(QPointF(kStubLeft, y), QPointF(kStubRight, y));
    y += 4;
    const qreal footerTop = top + kCheckHeight - kStubPad - lineHeight(f.heading);
    const qreal rowH = lineHeight(f.body) + 3;
    const int fit = qMax(1, int((footerTop - 8 - y) / rowH));
    const int shown = int(c.rows.size()) <= fit ? int(c.rows.size()) : fit - 1;
    for (int r = 0; r < shown; ++r) {
        for (int i = 0; i < c.columns.size(); ++i) {
            cell(i, y, c.rows.at(r).value(i), f.body, kInk);
        }
        y += rowH;
    }
    if (shown < c.rows.size()) {
        drawText(p, kStubLeft, y, QStringLiteral("+ %1 more (the check amount covers them all)").arg(c.rows.size() - shown),
                 f.body, kMuted);
    }
    p->setPen(QPen(kRule, 0.75));
    p->drawLine(QPointF(kStubLeft, footerTop - 5), QPointF(kStubRight, footerTop - 5));
    const QString amount = QStringLiteral("Check amount   ") + c.total;
    drawText(p, kStubRight, footerTop, amount, f.heading, kInk, true);
    const QString drawn = c.companyName + QStringLiteral("  ·  ") + c.bankAccount;
    drawText(p, kStubLeft, footerTop + 1, elide(f.small, drawn, width - textWidth(f.heading, amount) - 24), f.small,
             kMuted);
}

} // namespace

void CheckRenderer::paint(QPagedPaintDevice *device, QPainter *painter, const QList<PrintableCheck> &checks,
                          const CheckOffset &offset)
{
    const Fonts f;
    painter->setRenderHint(QPainter::Antialiasing);
    for (qsizetype i = 0; i < checks.size(); ++i) {
        if (i > 0) {
            device->newPage();
        }
        beginSheet(device, painter);
        // AGENT-NOTE (M68): the printer's alignment moves the whole sheet,
        // as QuickBooks' Align does; every position below stays nominal.
        painter->translate(offset.dx, offset.dy);
        const PrintableCheck &c = checks.at(i);
        drawCheck(painter, c, f);
        // Both stubs alike: one goes with the check, one stays in the file.
        drawStub(painter, c, kCheckHeight, f);
        drawStub(painter, c, 2 * kCheckHeight, f);
    }
}

namespace {

// A small "+" at a field's anchor (its baseline start) on the test page.
void cross(QPainter *p, qreal x, qreal baseline)
{
    p->setPen(QPen(kMuted, 0.5));
    p->drawLine(QPointF(x - 6, baseline), QPointF(x + 6, baseline));
    p->drawLine(QPointF(x, baseline - 6), QPointF(x, baseline + 6));
}

} // namespace

void CheckRenderer::paintAlignmentTest(QPagedPaintDevice *device, QPainter *painter, const CheckOffset &offset,
                                       const QString &printer, const QString &alignmentText, const PrintableCheck &sample)
{
    const Fonts f;
    painter->setRenderHint(QPainter::Antialiasing);
    beginSheet(device, painter);
    painter->save();
    painter->translate(offset.dx, offset.dy);
    drawCheck(painter, sample, f);
    for (const QPointF &anchor : {QPointF(kDateX, kDateBase), QPointF(kPayeeX, kPayeeBase), QPointF(kFiguresX, kPayeeBase),
                                  QPointF(kWordsX, kWordsBase), QPointF(kAddressX, kAddressBase),
                                  QPointF(kMemoX, kMemoBase)}) {
        cross(painter, anchor.x(), anchor.y());
    }
    // A two-inch scale in tenths under the memo line, to measure a miss.
    const qreal scaleTop = 3.15 * kInch;
    painter->setPen(QPen(kMuted, 0.5));
    painter->drawLine(QPointF(kMemoX, scaleTop), QPointF(kMemoX + 2 * kInch, scaleTop));
    for (int tick = 0; tick <= 20; ++tick) {
        const qreal x = kMemoX + tick * kInch / 10;
        painter->drawLine(QPointF(x, scaleTop), QPointF(x, scaleTop + (tick % 10 == 0 ? 8 : tick % 5 == 0 ? 5 : 3)));
    }
    painter->restore();
    // How to read it, in the lower stub — clear of the check even when the
    // offset moves it a whole inch down.
    qreal y = 2 * kCheckHeight + kStubPad;
    drawText(painter, kStubLeft, y, QStringLiteral("CHECK ALIGNMENT TEST"), f.heading);
    y += lineHeight(f.heading) + 8;
    const QStringList steps{
        QStringLiteral("1. Print this page on plain paper, on the printer you print checks with."),
        QStringLiteral("2. Lay it over a sheet of your check stock and hold both up to the light."),
        QStringLiteral("3. Each value should start at its + and sit on its line."),
        QStringLiteral("4. Too far left? Move it right (a positive horizontal offset). Too high? Move it down (a positive"),
        QStringLiteral("    vertical offset). The scale under the memo line is in tenths of an inch."),
        QStringLiteral("5. Enter the offsets in Expenses > Check Printer Alignment, print this page again, and repeat."),
    };
    for (const QString &line : steps) {
        drawText(painter, kStubLeft, y, line, f.body, kInk);
        y += lineHeight(f.body) + 3;
    }
    y += 8;
    drawText(painter, kStubLeft, y,
             QStringLiteral("Printer: %1    Offset now: %2")
                 .arg(printer.isEmpty() ? QStringLiteral("(a PDF is never shifted)") : printer, alignmentText),
             f.small, kMuted);
}

PagePainter CheckRenderer::alignmentTestPainter(const CheckOffset &offset, const QString &printer,
                                                const QString &alignmentText, const PrintableCheck &sample)
{
    return [offset, printer, alignmentText, sample](QPagedPaintDevice *device, QPainter *painter) {
        paintAlignmentTest(device, painter, offset, printer, alignmentText, sample);
    };
}

PagePainter CheckRenderer::painter(const QList<PrintableCheck> &checks, const CheckOffset &offset)
{
    return [checks, offset](QPagedPaintDevice *device, QPainter *painter) { paint(device, painter, checks, offset); };
}

bool CheckRenderer::writeChecksPdf(CompanyFile &file, const QList<qint64> &docIds, const QString &path, QString *error,
                                   const CheckOffset &offset)
{
    QList<PrintableCheck> checks;
    for (const qint64 id : docIds) {
        PrintableCheck c;
        if (!buildPrintableCheck(file, id, &c, error)) {
            return false;
        }
        checks << c;
    }
    if (checks.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("choose the checks to print");
        }
        return false;
    }
    return writePdf(path, painter(checks, offset), QPageSize(QPageSize::Letter), QPageLayout::Portrait, error);
}

} // namespace QindaOffice::Books
