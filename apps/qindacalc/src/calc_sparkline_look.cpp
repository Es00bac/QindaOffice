// SPDX-License-Identifier: GPL-3.0-or-later
// Sparklines drawn in their cells (calc_sparkline_look.h, M90, D-098).
//
// AGENT-NOTE: Excel's drawing, as measured on its own sparklines: a small
// inset from the cell's edges; lines through evenly spaced points (or by
// date), broken at gaps; columns filling about four fifths of their slot,
// rising from the axis when the values cross zero and from the bottom
// otherwise, the smallest still visible; win/loss bars of one height, wins
// above the middle and losses below, zero left empty. Special points are
// coloured in the order negative, low, high, first, last (a later one
// wins); the axis is drawn only where zero lies inside the vertical range.

#include "calc_sparkline_look.h"

#include "sheet.h"
#include "theme_colors.h"
#include "workbook.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

QColor colorOf(const QVector<Rgb> &palette, const SparklineColor &color, const Rgb &fallback)
{
    const Rgb rgb = color.rgb.has_value() ? *color.rgb : themeColor(palette, color.theme, color.tint, fallback);
    return QColor(rgb.r, rgb.g, rgb.b);
}

struct Points {
    int first = -1;
    int last = -1;
    int high = -1;
    int low = -1;
};

Points specialPoints(const QVector<std::optional<double>> &values)
{
    Points out;
    for (int i = 0; i < values.size(); ++i) {
        if (!values.at(i).has_value()) {
            continue;
        }
        const double v = *values.at(i);
        out.first = out.first < 0 ? i : out.first;
        out.last = i;
        out.high = out.high < 0 || v > *values.at(out.high) ? i : out.high;
        out.low = out.low < 0 || v < *values.at(out.low) ? i : out.low;
    }
    return out;
}

} // namespace

SparklinePainter::SparklinePainter(const Workbook &book, qint32 sheet)
    : m_book(book)
    , m_sheet(sheet)
{
    if (sheet < 0 || sheet >= book.sheetCount()) {
        return;
    }
    const QList<SparklineGroup> &groups = book.sheetAt(sheet).sparklineGroups();
    for (qsizetype g = 0; g < groups.size(); ++g) {
        for (qsizetype i = 0; i < groups.at(g).sparklines.size(); ++i) {
            m_index.insert(groups.at(g).sparklines.at(i).location.key(), {qint32(g), qint32(i)});
        }
    }
    if (!m_index.isEmpty()) {
        m_palette = themePalette(book.source.themeXml);
    }
}

void SparklinePainter::paint(QPainter &p, const CellAddr &addr, const QRectF &cell, qreal unitsPerPoint) const
{
    const auto found = m_index.constFind(addr.key());
    if (found == m_index.constEnd()) {
        return;
    }
    const SparklineGroup &group = m_book.sheetAt(m_sheet).sparklineGroups().at(found->group);
    SparklineSeries series = sparklineSeries(m_book, group, group.sparklines.at(found->index));
    const int n = int(series.values.size());
    if (n == 0) {
        return;
    }
    const SparklineScale scale = sparklineScale(m_book, group, series);
    if (group.rightToLeft) {
        std::reverse(series.values.begin(), series.values.end());
        std::reverse(series.dates.begin(), series.dates.end());
    }
    const qreal inset = qMax(qreal(1.5), 1.5 * unitsPerPoint);
    const QRectF area = cell.adjusted(inset, inset, -inset, -inset);
    if (area.width() <= 1 || area.height() <= 1) {
        return;
    }
    const Rgb black{0, 0, 0};
    const QColor seriesColor = colorOf(m_palette, group.series, Rgb{55, 96, 146});
    const QColor negativeColor = colorOf(m_palette, group.negativeColor, Rgb{192, 80, 77});
    const QColor axisColor = colorOf(m_palette, group.axisColor, black);
    const Points special = specialPoints(series.values);
    const auto pointColor = [&](int i, const QColor &base) {
        QColor out = base;
        const double v = series.values.at(i).value_or(0.0);
        out = group.negative && v < 0 ? negativeColor : out;
        out = group.low && i == special.low ? colorOf(m_palette, group.lowColor, black) : out;
        out = group.high && i == special.high ? colorOf(m_palette, group.highColor, black) : out;
        out = group.first && i == special.first ? colorOf(m_palette, group.firstColor, black) : out;
        out = group.last && i == special.last ? colorOf(m_palette, group.lastColor, black) : out;
        return out;
    };
    const bool flat = scale.max <= scale.min;
    const auto yOf = [&](double v) {
        if (flat) {
            return area.center().y();
        }
        return area.bottom() - (v - scale.min) / (scale.max - scale.min) * area.height();
    };
    p.save();
    p.setClipRect(cell);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal weight = qMax(qreal(1.0), group.lineWeight * unitsPerPoint);
    if (group.type == SparklineGroup::Type::WinLoss) {
        const qreal slot = area.width() / n;
        const qreal gap = qMax(qreal(1.0), slot * 0.2);
        const qreal half = area.height() / 2.0 - qMax(qreal(0.5), unitsPerPoint * 0.5);
        for (int i = 0; i < n; ++i) {
            const double v = series.values.at(i).value_or(0.0);
            if (v == 0.0) {
                continue;
            }
            const QRectF bar(area.left() + i * slot + gap / 2, v > 0 ? area.center().y() - half : area.center().y() + 1,
                             slot - gap, half);
            p.fillRect(bar, pointColor(i, v < 0 ? negativeColor : seriesColor));
        }
        if (group.displayXAxis) {
            p.setPen(QPen(axisColor, qMax(qreal(1.0), 0.75 * unitsPerPoint)));
            p.drawLine(QPointF(area.left(), area.center().y()), QPointF(area.right(), area.center().y()));
        }
        p.restore();
        return;
    }
    if (group.type == SparklineGroup::Type::Column) {
        const double base = scale.min <= 0 && scale.max >= 0 ? 0.0 : (scale.max < 0 ? scale.max : scale.min);
        const qreal slot = area.width() / n;
        const qreal gap = qMax(qreal(1.0), slot * 0.2);
        const qreal floor = qMax(qreal(1.0), unitsPerPoint * 0.75); // the smallest column still shows
        for (int i = 0; i < n; ++i) {
            if (!series.values.at(i).has_value()) {
                continue;
            }
            const double v = qBound(scale.min, *series.values.at(i), qMax(scale.min, scale.max));
            qreal top = yOf(v);
            qreal bottom = yOf(base);
            if (top > bottom) {
                std::swap(top, bottom);
            }
            if (bottom - top < floor) {
                (v >= base ? top : bottom) = v >= base ? bottom - floor : top + floor;
            }
            p.fillRect(QRectF(area.left() + i * slot + gap / 2, top, slot - gap, bottom - top), pointColor(i, seriesColor));
        }
        if (group.displayXAxis && scale.min < 0 && scale.max > 0) {
            p.setPen(QPen(axisColor, qMax(qreal(1.0), 0.75 * unitsPerPoint)));
            p.drawLine(QPointF(area.left(), yOf(0)), QPointF(area.right(), yOf(0)));
        }
        p.restore();
        return;
    }
    // A line, by position or by date.
    const bool byDate = series.dates.size() == n && n > 1;
    const double dateMin = byDate ? *std::min_element(series.dates.begin(), series.dates.end()) : 0;
    const double dateMax = byDate ? *std::max_element(series.dates.begin(), series.dates.end()) : 0;
    const auto xOf = [&](int i) {
        if (byDate && dateMax > dateMin) {
            return area.left() + (series.dates.at(i) - dateMin) / (dateMax - dateMin) * area.width();
        }
        return n == 1 ? area.center().x() : area.left() + i * area.width() / (n - 1);
    };
    if (group.displayXAxis && scale.min < 0 && scale.max > 0) {
        p.setPen(QPen(axisColor, qMax(qreal(1.0), 0.75 * unitsPerPoint)));
        p.drawLine(QPointF(area.left(), yOf(0)), QPointF(area.right(), yOf(0)));
    }
    QPainterPath path;
    bool open = false;
    for (int i = 0; i < n; ++i) {
        if (!series.values.at(i).has_value()) {
            open = false; // a gap breaks the line
            continue;
        }
        const QPointF at(xOf(i), yOf(*series.values.at(i)));
        if (open) {
            path.lineTo(at);
        } else {
            path.moveTo(at);
            open = true;
        }
    }
    p.setPen(QPen(seriesColor, weight, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    const qreal marker = qMax(qreal(2.5), weight * 2.2);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        if (!series.values.at(i).has_value()) {
            continue;
        }
        const double v = *series.values.at(i);
        const bool shown = group.markers || (group.negative && v < 0) || (group.low && i == special.low)
            || (group.high && i == special.high) || (group.first && i == special.first) || (group.last && i == special.last);
        if (!shown) {
            continue;
        }
        p.setBrush(pointColor(i, colorOf(m_palette, group.markersColor, Rgb{55, 96, 146})));
        p.drawEllipse(QPointF(xOf(i), yOf(v)), marker / 2, marker / 2);
    }
    p.restore();
}

} // namespace QindaOffice::Apps::QindaCalc
