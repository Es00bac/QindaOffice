// SPDX-License-Identifier: GPL-3.0-or-later
#include "report_print.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <memory>

namespace QindaOffice::Plan {

namespace {

struct Fonts {
    QFont title = paperFont(18, QFont::Bold);
    QFont subtitle = paperFont(9);
    QFont heading = paperFont(11, QFont::Bold);
    QFont header = paperFont(8, QFont::DemiBold);
    QFont body = paperFont(8);
    QFont kpiLabel = paperFont(8);
    QFont kpiValue = paperFont(16, QFont::Bold);
    QFont kpiDetail = paperFont(7);
    QFont footer = paperFont(7);
};

constexpr qreal kTitleHeight = 46;
constexpr qreal kKpiHeight = 60;
constexpr qreal kKpiGap = 8;
constexpr qreal kHeadingHeight = 24;
constexpr qreal kHeaderHeight = 17;
constexpr qreal kRowHeight = 15;
constexpr qreal kFooterHeight = 16;
constexpr qreal kCellPad = 5;

enum class Kind { Title, KpiRow, Heading, Header, Row, Empty, Space };

struct Item {
    Kind kind;
    int table = -1; // index into report.tables
    int row = -1;   // row index, or first KPI of a KpiRow
    bool continued = false;
    qreal height = 0;
};

struct Layout {
    PlanReport report;
    QRectF area;
    Fonts fonts;
    PlanPaperStyle style;
    int kpiColumns = 4;
    QList<QList<qreal>> widths; // per table
    QList<QList<Item>> pages;
};

QList<qreal> measureColumns(const ReportTable &table, const Fonts &f, qreal available)
{
    QList<qreal> w;
    for (int c = 0; c < table.columns.size(); ++c) {
        qreal best = textWidth(f.header, table.columns.at(c)) + 2 * kCellPad;
        for (const QStringList &row : table.rows) {
            best = std::max(best, textWidth(f.body, row.value(c)) + 2 * kCellPad);
        }
        w.append(std::min<qreal>(best, 240));
    }
    qreal total = 0;
    for (qreal x : std::as_const(w)) {
        total += x;
    }
    // The widest text column absorbs the difference (the task name, mostly).
    int flex = 0;
    for (int c = 0; c < w.size(); ++c) {
        if (!table.numericColumns.contains(c) && (table.numericColumns.contains(flex) || w.at(c) > w.at(flex))) {
            flex = c;
        }
    }
    if (!w.isEmpty()) {
        w[flex] = std::max<qreal>(50, w.at(flex) + available - total);
        total = 0;
        for (qreal x : std::as_const(w)) {
            total += x;
        }
        if (total > available) {
            for (qreal &x : w) {
                x *= available / total;
            }
        }
    }
    return w;
}

void paginate(Layout &l)
{
    QList<Item> items;
    items.append({Kind::Title, -1, -1, false, kTitleHeight});
    for (int k = 0; k < l.report.kpis.size(); k += l.kpiColumns) {
        items.append({Kind::KpiRow, -1, k, false, kKpiHeight + kKpiGap});
    }
    for (int t = 0; t < l.report.tables.size(); ++t) {
        items.append({Kind::Space, t, -1, false, 8});
        items.append({Kind::Heading, t, -1, false, kHeadingHeight});
        items.append({Kind::Header, t, -1, false, kHeaderHeight});
        const ReportTable &table = l.report.tables.at(t);
        if (table.rows.isEmpty()) {
            items.append({Kind::Empty, t, -1, false, kRowHeight + 4});
        }
        for (int r = 0; r < table.rows.size(); ++r) {
            items.append({Kind::Row, t, r, false, kRowHeight});
        }
    }
    const qreal capacity = l.area.height() - kFooterHeight;
    QList<Item> page;
    qreal used = 0;
    for (int i = 0; i < items.size(); ++i) {
        Item item = items.at(i);
        qreal need = item.height;
        if (item.kind == Kind::Heading) { // keep with its header and first row
            need += kHeaderHeight + kRowHeight + 4;
        }
        if (item.kind == Kind::Space && used == 0) {
            continue;
        }
        if (used + need > capacity && !page.isEmpty()) {
            l.pages.append(page);
            page.clear();
            used = 0;
            if (item.kind == Kind::Row) {
                page.append({Kind::Heading, item.table, -1, true, kHeadingHeight});
                page.append({Kind::Header, item.table, -1, true, kHeaderHeight});
                used = kHeadingHeight + kHeaderHeight;
            }
            if (item.kind == Kind::Space) {
                continue;
            }
        }
        page.append(item);
        used += item.height;
    }
    if (!page.isEmpty() || l.pages.isEmpty()) {
        l.pages.append(page);
    }
}

void paintKpi(QPainter *p, const Layout &l, const ReportKpi &k, const QRectF &box)
{
    const PlanPaperStyle &s = l.style;
    const Fonts &f = l.fonts;
    const QColor tone = k.tone.isEmpty() ? s.ink : s.tone(k.tone);
    p->setPen(QPen(s.rule, 0.8));
    p->setBrush(s.paper);
    p->drawRoundedRect(box, 3, 3);
    if (!k.tone.isEmpty()) {
        p->fillRect(QRectF(box.left(), box.top() + 3, 2.5, box.height() - 6), tone);
    }
    const qreal x = box.left() + 10;
    drawTextAt(p, x, box.top() + 7, elideText(f.kpiLabel, k.label, box.width() - 16), f.kpiLabel, s.muted);
    drawTextAt(p, x, box.top() + 19, elideText(f.kpiValue, k.value, box.width() - 16), f.kpiValue,
               k.tone == QLatin1String("danger") ? tone : s.ink);
    if (k.percent >= 0) {
        const QRectF track(x, box.bottom() - 13, box.width() - 20, 4);
        p->setPen(Qt::NoPen);
        p->setBrush(s.hairline);
        p->drawRoundedRect(track, 2, 2);
        p->setBrush(s.accent);
        p->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * std::clamp(k.percent, 0, 100) / 100.0, 4), 2, 2);
    } else if (!k.detail.isEmpty()) {
        drawTextAt(p, x, box.bottom() - 15, elideText(f.kpiDetail, k.detail, box.width() - 16), f.kpiDetail,
                   k.tone.isEmpty() ? s.muted : tone);
    }
    if (k.percent >= 0 && !k.detail.isEmpty()) {
        drawTextAt(p, box.right() - 8, box.top() + 7, elideText(f.kpiDetail, k.detail, box.width() * 0.5), f.kpiDetail,
                   s.muted, true);
    }
}

void paintItem(QPainter *p, const Layout &l, const Item &item, qreal y)
{
    const PlanPaperStyle &s = l.style;
    const Fonts &f = l.fonts;
    const QRectF &a = l.area;
    switch (item.kind) {
    case Kind::Title:
        drawTextAt(p, a.left(), y, l.report.title, f.title, s.ink);
        drawTextAt(p, a.left(), y + 25, elideText(f.subtitle, l.report.subtitle, a.width()), f.subtitle, s.muted);
        p->fillRect(QRectF(a.left(), y + kTitleHeight - 6, a.width(), 1.2), s.accent);
        return;
    case Kind::KpiRow: {
        const qreal w = (a.width() - (l.kpiColumns - 1) * kKpiGap) / l.kpiColumns;
        for (int c = 0; c < l.kpiColumns && item.row + c < l.report.kpis.size(); ++c) {
            paintKpi(p, l, l.report.kpis.at(item.row + c), QRectF(a.left() + c * (w + kKpiGap), y + 4, w, kKpiHeight - 4));
        }
        return;
    }
    case Kind::Space:
        return;
    case Kind::Heading: {
        const ReportTable &t = l.report.tables.at(item.table);
        const QString text = item.continued ? QStringLiteral("%1 (continued)").arg(t.title) : t.title;
        drawTextAt(p, a.left(), y + 6, text, f.heading, s.ink);
        if (!item.continued && !t.rows.isEmpty()) {
            drawTextAt(p, a.right(), y + 8, QStringLiteral("%1 rows").arg(t.rows.size()), f.footer, s.muted, true);
        }
        return;
    }
    case Kind::Header:
    case Kind::Row:
    case Kind::Empty: {
        const ReportTable &t = l.report.tables.at(item.table);
        const QList<qreal> &widths = l.widths.at(item.table);
        const qreal h = item.height;
        if (item.kind == Kind::Header) {
            p->fillRect(QRectF(a.left(), y, a.width(), h), s.shade);
        } else if (item.kind == Kind::Row && item.row % 2 == 1) {
            p->fillRect(QRectF(a.left(), y, a.width(), h), s.zebra);
        }
        if (item.kind == Kind::Empty) {
            drawTextAt(p, a.left() + kCellPad, y + 4, t.emptyText, f.body, s.muted);
            return;
        }
        if (item.kind == Kind::Row) {
            const QString tone = t.rowTones.value(item.row);
            if (!tone.isEmpty()) {
                p->fillRect(QRectF(a.left(), y + 1, 2.5, h - 2), s.tone(tone));
            }
        }
        const QFont &font = item.kind == Kind::Header ? f.header : f.body;
        const QStringList cells = item.kind == Kind::Header ? t.columns : t.rows.at(item.row);
        const qreal textTop = y + (h - QFontMetricsF(font).height()) / 2;
        qreal x = a.left();
        for (int c = 0; c < widths.size(); ++c) {
            const bool right = t.numericColumns.contains(c);
            const QString text = elideText(font, cells.value(c), widths.at(c) - 2 * kCellPad);
            drawTextAt(p, right ? x + widths.at(c) - kCellPad : x + kCellPad, textTop, text, font,
                       item.kind == Kind::Header ? s.muted : s.ink, right);
            x += widths.at(c);
        }
        p->setPen(QPen(item.kind == Kind::Header ? s.rule : s.hairline, 0.5));
        p->drawLine(QPointF(a.left(), y + h), QPointF(a.right(), y + h));
        return;
    }
    }
}

} // namespace

PageLayout reportLayout(const PlanReport &report, const QLocale &locale)
{
    Q_UNUSED(locale)
    return [report](const QRectF &area) {
        auto l = std::make_shared<Layout>();
        l->report = report;
        l->area = area;
        l->kpiColumns = std::clamp(int(area.width() / 130), 2, 4);
        for (const ReportTable &t : report.tables) {
            l->widths.append(measureColumns(t, l->fonts, area.width()));
        }
        paginate(*l);
        PageSet set;
        set.count = int(l->pages.size());
        set.paint = [l](QPainter *p, int page) {
            qreal y = l->area.top();
            for (const Item &item : l->pages.at(page)) {
                paintItem(p, *l, item, y);
                y += item.height;
            }
            const qreal fy = l->area.bottom() - kFooterHeight + 5;
            p->setPen(QPen(l->style.hairline, 0.5));
            p->drawLine(QPointF(l->area.left(), fy - 3), QPointF(l->area.right(), fy - 3));
            drawTextAt(p, l->area.left(), fy, elideText(l->fonts.footer, l->report.title + QStringLiteral("  ·  ") + l->report.subtitle,
                                                          l->area.width() * 0.75),
                       l->fonts.footer, l->style.muted);
            drawTextAt(p, l->area.right(), fy, QStringLiteral("Page %1 of %2").arg(page + 1).arg(l->pages.size()),
                       l->fonts.footer, l->style.muted, true);
        };
        return set;
    };
}

PagePainter reportPainter(const PlanReport &report, const QLocale &locale)
{
    return pagedPainter(reportLayout(report, locale));
}

} // namespace QindaOffice::Plan
