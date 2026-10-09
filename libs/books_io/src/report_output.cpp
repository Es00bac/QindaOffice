// SPDX-License-Identifier: GPL-3.0-or-later
#include "report_output.h"

#include "company_file.h"
#include "csv_util.h"
#include "paint_util.h"
#include "printable_document.h"

#include <QPagedPaintDevice>
#include <QPainter>

namespace QindaOffice::Books {

using namespace Paint;

namespace {

constexpr qreal kPad = 6;
constexpr qreal kIndent = 12;

struct Fonts {
    QFont body = font(9);
    QFont semibold = font(9, QFont::DemiBold);
    QFont bold = font(9, QFont::Bold);
    QFont header = font(8, QFont::DemiBold);
    QFont title = font(18, QFont::Bold);
    QFont company = font(10, QFont::DemiBold);
    QFont small = font(8);
    const QFont &forRow(RowKind kind) const
    {
        switch (kind) {
        case RowKind::Section:
        case RowKind::Subtotal: return semibold;
        case RowKind::Total: return bold;
        default: return body;
        }
    }
};

struct Columns {
    QList<qreal> x;
    QList<qreal> w;
    int flex = 0;
    qreal natural = 0; // total natural width before fitting
};

bool isNumeric(ColumnType t)
{
    return t == ColumnType::Money || t == ColumnType::Number || t == ColumnType::Percent || t == ColumnType::Duration;
}

Columns measure(const ReportTable &t, const ReportStyle &style, qreal width, const Fonts &f)
{
    Columns c;
    const int n = int(t.columns.size());
    QList<qreal> natural(n, 0);
    for (int i = 0; i < n; ++i) {
        natural[i] = textWidth(f.header, t.columns[i].title) + kPad * 2;
    }
    for (const ReportRow &r : t.rows) {
        for (int i = 0; i < n && i < r.cells.size(); ++i) {
            const qreal indent = i == 0 ? r.depth * kIndent : 0;
            natural[i] = qMax(natural[i], textWidth(f.forRow(r.kind), r.cells[i].display(t.currency, style.locale))
                                              + indent + kPad * 2);
        }
    }
    // The flexible column: the widest text column (the label in grouped
    // reports; name or memo in listings).
    qreal widest = -1;
    for (int i = 0; i < n; ++i) {
        if (t.columns[i].type == ColumnType::Text && natural[i] > widest) {
            widest = natural[i];
            c.flex = i;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (i != c.flex && t.columns[i].type == ColumnType::Text) {
            natural[i] = qMin<qreal>(natural[i], 150);
        }
        natural[i] = qMax<qreal>(natural[i], isNumeric(t.columns[i].type) ? 64 : 40);
        c.natural += natural[i];
    }
    qreal others = c.natural - natural.value(c.flex);
    qreal flexW = qMax<qreal>(width - others, 90);
    if (others + flexW > width && others > 0) {
        const qreal factor = (width - flexW) / others;
        for (int i = 0; i < n; ++i) {
            if (i != c.flex) {
                natural[i] *= factor;
            }
        }
    }
    natural[c.flex] = flexW;
    qreal x = 0;
    for (int i = 0; i < n; ++i) {
        c.x << x;
        c.w << natural[i];
        x += natural[i];
    }
    return c;
}

qreal rowHeight(const ReportRow &r, const Fonts &f)
{
    if (r.kind == RowKind::Spacer) {
        return 8;
    }
    return lineHeight(f.forRow(r.kind)) + (r.kind == RowKind::Section ? 10 : 6);
}

void drawColumnHeader(QPainter *p, const ReportTable &t, const Columns &c, const QRectF &area, qreal top,
                      const ReportStyle &style, const Fonts &f)
{
    for (int i = 0; i < t.columns.size(); ++i) {
        const bool right = isNumeric(t.columns[i].type);
        const qreal x = area.left() + (right ? c.x[i] + c.w[i] - kPad : c.x[i] + kPad);
        drawText(p, x, top, elide(f.header, t.columns[i].title, c.w[i] - kPad * 2), f.header, kMuted, right);
    }
    const qreal y = top + lineHeight(f.header) + 5;
    p->setPen(QPen(style.accent, 1.0));
    p->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
}

// `grandTotal`: the table's closing Total row, the one that gets the
// accountant's double underline (intermediate totals get a single rule).
void drawRow(QPainter *p, const ReportTable &t, const ReportRow &r, const Columns &c, const QRectF &area, qreal top,
             bool grandTotal, const ReportStyle &style, const Fonts &f)
{
    const QFont &font = f.forRow(r.kind);
    const qreal textTop = top + (r.kind == RowKind::Section ? 7 : 3);
    for (int i = 0; i < t.columns.size() && i < r.cells.size(); ++i) {
        const ReportCell &cell = r.cells[i];
        if (cell.empty) {
            continue;
        }
        const bool right = isNumeric(t.columns[i].type) || isNumeric(cell.type);
        const qreal indent = i == 0 ? r.depth * kIndent : 0;
        const QString text = elide(font, cell.display(t.currency, style.locale), c.w[i] - kPad * 2 - indent);
        const qreal x = area.left() + (right ? c.x[i] + c.w[i] - kPad : c.x[i] + kPad + indent);
        drawText(p, x, textTop, text, font, kInk, right);
        if (right && cell.type == ColumnType::Money && (r.kind == RowKind::Subtotal || r.kind == RowKind::Total)) {
            const qreal x0 = area.left() + c.x[i] + kPad * 2;
            const qreal x1 = area.left() + c.x[i] + c.w[i] - kPad;
            p->setPen(QPen(kInk, r.kind == RowKind::Total ? 0.8 : 0.5));
            p->drawLine(QPointF(x0, top + 1), QPointF(x1, top + 1));
            if (grandTotal) {
                const qreal y = textTop + lineHeight(font) + 1.5;
                p->setPen(QPen(kInk, 0.5));
                p->drawLine(QPointF(x0, y), QPointF(x1, y));
                p->drawLine(QPointF(x0, y + 2), QPointF(x1, y + 2));
            }
        }
    }
}

qreal drawFirstHeader(QPainter *p, const ReportTable &t, const QRectF &area, const Fonts &f)
{
    qreal y = area.top();
    auto centered = [&](const QString &text, const QFont &font, const QColor &color) {
        if (!text.isEmpty()) {
            if (p != nullptr) {
                drawText(p, area.center().x() - textWidth(font, text) / 2, y, text, font, color);
            }
            y += lineHeight(font) + 2;
        }
    };
    centered(t.company, f.company, kMuted);
    centered(t.title, f.title, kInk);
    centered(t.subtitle, f.small, kMuted);
    return y + 16;
}

qreal drawContinuationHeader(QPainter *p, const ReportTable &t, const QRectF &area, const Fonts &f)
{
    if (p != nullptr) {
        drawText(p, area.left(), area.top(), t.title + QStringLiteral(" (continued)"), f.semibold);
        drawText(p, area.right(), area.top(), t.company, f.small, kMuted, true);
    }
    return area.top() + lineHeight(f.semibold) + 14;
}

struct Pagination {
    QList<QPair<int, int>> pages; // [first, last] row per page
};

Pagination paginate(const ReportTable &t, const QRectF &area, const Fonts &f, qreal firstTop, qreal contTop)
{
    Pagination out;
    const qreal header = lineHeight(f.header) + 10;
    const qreal bottom = area.bottom() - lineHeight(f.small) - 14;
    qreal y = firstTop + header;
    int start = 0;
    for (int r = 0; r < t.rows.size(); ++r) {
        const qreal h = rowHeight(t.rows[r], f);
        if (y + h > bottom && r > start) {
            out.pages << qMakePair(start, r - 1);
            start = r;
            y = contTop + header;
        }
        y += h;
    }
    out.pages << qMakePair(start, int(t.rows.size()) - 1);
    return out;
}

} // namespace

void ReportPdf::paint(QPagedPaintDevice *device, QPainter *painter, const ReportTable &table, const ReportStyle &style)
{
    const Fonts f;
    const QRectF area = beginPage(device, painter);
    painter->setRenderHint(QPainter::Antialiasing);
    const Columns columns = measure(table, style, area.width(), f);
    // A null painter measures: pagination uses the very header code.
    const qreal firstTop = drawFirstHeader(nullptr, table, area, f);
    const qreal contTop = drawContinuationHeader(nullptr, table, area, f);
    const Pagination pagination = paginate(table, area, f, firstTop, contTop);
    const int pages = int(pagination.pages.size());
    for (int page = 0; page < pages; ++page) {
        if (page > 0) {
            device->newPage();
            beginPage(device, painter);
        }
        qreal y = page == 0 ? drawFirstHeader(painter, table, area, f) : drawContinuationHeader(painter, table, area, f);
        drawColumnHeader(painter, table, columns, area, y, style, f);
        y += lineHeight(f.header) + 10;
        const auto [first, last] = pagination.pages[page];
        for (int r = first; r <= last; ++r) {
            drawRow(painter, table, table.rows[r], columns, area, y,
                    r == table.rows.size() - 1 && table.rows[r].kind == RowKind::Total, style, f);
            y += rowHeight(table.rows[r], f);
        }
        if (page == pages - 1) {
            y += 12;
            for (const QString &note : table.notes) {
                painter->setFont(f.small);
                painter->setPen(kMuted);
                const qreal h = wrappedHeight(f.small, note, area.width());
                painter->drawText(QRectF(area.left(), y, area.width(), h + 2), Qt::TextWordWrap, note);
                y += h + 4;
            }
        }
        const qreal footer = area.bottom() - lineHeight(f.small);
        drawText(painter, area.left(), footer, table.company + QStringLiteral("  ·  ") + table.title, f.small, kMuted);
        drawText(painter, area.right(), footer, QStringLiteral("Page %1 of %2").arg(page + 1).arg(pages), f.small, kMuted,
                 true);
    }
}

PagePainter ReportPdf::painter(const ReportTable &table, const ReportStyle &style)
{
    return [table, style](QPagedPaintDevice *device, QPainter *qp) { paint(device, qp, table, style); };
}

bool ReportPdf::prefersLandscape(const ReportTable &table, const QPageSize &pageSize, const ReportStyle &style)
{
    const qreal width = pageSize.rect(QPageSize::Point).width() - 2 * kMinMargin;
    const Columns c = measure(table, style, width, Fonts());
    return table.columns.size() > 7 || c.natural > width;
}

ReportStyle ReportPdf::styleFor(CompanyFile &file)
{
    ReportStyle style;
    style.locale = companyLocale(file);
    const QColor accent(file.settings().accentColor);
    if (accent.isValid()) {
        style.accent = accent;
    }
    return style;
}

bool ReportPdf::writeReportPdf(CompanyFile &file, const ReportTable &table, const QString &path, QString *error)
{
    const ReportStyle style = styleFor(file);
    const QPageSize size = defaultPageSize(style.locale);
    const QPageLayout::Orientation orientation =
        prefersLandscape(table, size, style) ? QPageLayout::Landscape : QPageLayout::Portrait;
    return writePdf(path, painter(table, style), size, orientation, error);
}

QString reportCellPlain(const ReportCell &cell, const QString &currency)
{
    if (cell.empty) {
        return QString();
    }
    switch (cell.type) {
    case ColumnType::Text: return cell.text;
    case ColumnType::Money: return Money(cell.value, currency).toPlainString();
    case ColumnType::Number:
    case ColumnType::Percent: return Decimal(cell.value, cell.scale).toString();
    case ColumnType::Date: return cell.date.toString(Qt::ISODate);
    case ColumnType::Duration: return cell.display(currency); // h:mm, as QuickBooks exports time
    }
    return QString();
}

QByteArray reportToCsv(const ReportTable &table)
{
    QList<QStringList> rows;
    QStringList header;
    for (const ReportColumn &c : table.columns) {
        header << (c.title.isEmpty() ? c.id : c.title);
    }
    rows << header;
    for (const ReportRow &r : table.rows) {
        QStringList cells;
        for (int i = 0; i < table.columns.size(); ++i) {
            cells << (i < r.cells.size() ? reportCellPlain(r.cells[i], table.currency) : QString());
        }
        rows << cells;
    }
    return Csv::document(rows);
}

} // namespace QindaOffice::Books
