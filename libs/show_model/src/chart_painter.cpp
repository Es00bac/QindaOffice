// SPDX-License-Identifier: GPL-3.0-or-later
// Charts for the renderer, drawn from the values the chart part caches
// (c:numCache / c:strCache): column, bar, line, area (clustered, stacked,
// percent-stacked), pie, doughnut and scatter, with a title, a value axis
// with "nice" gridlines, category labels and a legend; a combo chart's
// plots together on the one value axis (M82: areas, then columns, then
// lines on top). An approximation of what PowerPoint draws — the chart
// part itself travels untouched.

#include "render_parts.h"
#include "text_painter.h"

#include <QHash>
#include <QLocale>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace QindaOffice::Show::Render {

namespace {

const QList<QColor> &officePalette()
{
    static const QList<QColor> palette{QColor(0x44, 0x72, 0xC4), QColor(0xED, 0x7D, 0x31), QColor(0xA5, 0xA5, 0xA5),
                                       QColor(0xFF, 0xC0, 0x00), QColor(0x5B, 0x9B, 0xD5), QColor(0x70, 0xAD, 0x47)};
    return palette;
}

QColor paletteColor(const ChartData &chart, int index)
{
    const QList<QColor> &palette = chart.palette.isEmpty() ? officePalette() : chart.palette;
    QColor color = palette.at(index % palette.size());
    // Beyond the six accents PowerPoint darkens, then lightens, the cycle.
    const int round = index / int(palette.size());
    if (round % 2 == 1) {
        color = color.darker(100 + 40 * ((round + 1) / 2));
    } else if (round > 0) {
        color = color.lighter(100 + 30 * (round / 2));
    }
    return color;
}

QColor seriesColor(const ChartData &chart, int index)
{
    const QColor own = chart.series.at(index).color;
    return own.isValid() ? own : paletteColor(chart, index);
}

void label(QPainter *painter, const QRectF &box, const QString &text, int alignment, qreal size,
           const QColor &color = QColor(89, 89, 89), bool bold = false, TextAnchor anchor = TextAnchor::Middle)
{
    TextRun run;
    run.text = text;
    run.pointSize = size;
    run.color = color;
    run.bold = bold;
    TextParagraph paragraph;
    paragraph.alignment = alignment;
    paragraph.runs = {run};
    TextBodyStyle style;
    style.insetLeft = style.insetRight = style.insetTop = style.insetBottom = 0;
    style.anchor = anchor;
    style.wrap = false;
    paintText(painter, box, {paragraph}, style);
}

QString number(qreal value)
{
    if (std::abs(value) >= 1000 && std::abs(value - std::round(value)) < 1e-9) {
        return QLocale().toString(qint64(std::llround(value)));
    }
    return QString::number(value, 'g', 6);
}

// Axis bounds and step a person would choose for [low, high].
void niceScale(qreal low, qreal high, qreal *min, qreal *max, qreal *step)
{
    if (high <= low) {
        high = low + 1;
    }
    const qreal raw = (high - low) / 5.0;
    const qreal magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const qreal fraction = raw / magnitude;
    const qreal nice = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 2.5 ? 2.5 : fraction <= 5 ? 5 : 10;
    *step = nice * magnitude;
    *min = std::floor(low / *step) * *step;
    *max = std::ceil(high / *step) * *step;
}

int categoryCount(const ChartData &chart)
{
    int count = 0;
    for (const ChartSeries &series : chart.series) {
        count = qMax(count, int(qMax(series.values.size(), series.categories.size())));
    }
    return count;
}

QString categoryName(const ChartData &chart, int index)
{
    for (const ChartSeries &series : chart.series) {
        if (index < series.categories.size() && !series.categories.at(index).isEmpty()) {
            return series.categories.at(index);
        }
    }
    return QString::number(index + 1);
}

qreal valueAt(const ChartSeries &series, int index)
{
    const qreal v = index < series.values.size() ? series.values.at(index) : 0.0;
    return std::isnan(v) ? 0.0 : v;
}

void paintPie(QPainter *painter, const QRectF &area, const ChartData &chart)
{
    const ChartSeries &series = chart.series.first();
    qreal total = 0;
    for (int i = 0; i < series.values.size(); ++i) {
        total += qMax<qreal>(0.0, valueAt(series, i));
    }
    if (total <= 0) {
        return;
    }
    const qreal side = qMin(area.width(), area.height()) * 0.9;
    const QRectF disc(area.center() - QPointF(side / 2, side / 2), QSizeF(side, side));
    qreal angle = 90.0; // PowerPoint starts at 12 o'clock and runs clockwise
    painter->setPen(QPen(Qt::white, 1.0));
    for (int i = 0; i < series.values.size(); ++i) {
        const qreal sweep = 360.0 * qMax<qreal>(0.0, valueAt(series, i)) / total;
        const QColor own = i < series.pointColors.size() ? series.pointColors.at(i) : QColor();
        painter->setBrush(own.isValid() ? own : paletteColor(chart, i));
        painter->drawPie(disc, int(angle * 16), int(-sweep * 16));
        angle -= sweep;
    }
    if (chart.type == u"doughnut") {
        const qreal hole = side * 0.5;
        painter->setBrush(Qt::white);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QRectF(disc.center() - QPointF(hole / 2, hole / 2), QSizeF(hole, hole)));
    }
}

// How one plot of the chart draws (a combo chart has several).
struct PlotLook {
    QString type;
    bool stacked = false;
    bool percent = false;
    [[nodiscard]] bool barLike() const { return type == u"column" || type == u"bar"; }
};

PlotLook lookOf(const ChartData &chart, int plot)
{
    const ChartPlot kind = chart.plotAt(plot);
    PlotLook look;
    look.type = kind.type;
    look.stacked = kind.grouping == u"stacked" || kind.grouping == u"percentStacked";
    look.percent = kind.grouping == u"percentStacked";
    return look;
}

// A series' value at a category as its plot draws it (percent-stacked:
// its share of the category among its plot's series).
qreal drawnValue(const ChartData &chart, int s, int c)
{
    const ChartSeries &series = chart.series.at(s);
    const qreal v = valueAt(series, c);
    if (!lookOf(chart, series.plot).percent) {
        return v;
    }
    qreal sum = 0;
    for (const ChartSeries &other : chart.series) {
        sum += other.plot == series.plot ? std::abs(valueAt(other, c)) : 0.0;
    }
    return sum > 0 ? 100.0 * v / sum : 0;
}

void paintCategories(QPainter *painter, QRectF area, const ChartData &chart)
{
    const bool horizontal = chart.type == u"bar";
    const bool percent = lookOf(chart, 0).percent && chart.plotCount() == 1;
    const int categories = categoryCount(chart);
    if (categories == 0) {
        return;
    }
    // Value range: each plot's stacks, or each series on its own.
    qreal low = 0;
    qreal high = 0;
    for (int c = 0; c < categories; ++c) {
        QHash<int, qreal> positive;
        QHash<int, qreal> negative;
        for (int s = 0; s < chart.series.size(); ++s) {
            const int plot = chart.series.at(s).plot;
            const qreal v = drawnValue(chart, s, c);
            if (lookOf(chart, plot).stacked) {
                (v >= 0 ? positive[plot] : negative[plot]) += v;
                high = qMax(high, positive.value(plot));
                low = qMin(low, negative.value(plot));
            } else {
                high = qMax(high, v);
                low = qMin(low, v);
            }
        }
    }
    if (percent) {
        low = low < 0 ? -100 : 0;
        high = 100;
    }
    qreal min = 0;
    qreal max = 1;
    qreal step = 1;
    niceScale(low, high, &min, &max, &step);
    const qreal labelSize = 9;
    const qreal valueLabelSpace = horizontal ? labelSize * 2 : labelSize * 4;
    const qreal categorySpace = horizontal ? labelSize * 6 : labelSize * 2;
    QRectF plot = horizontal ? area.adjusted(categorySpace, 0, 0, -valueLabelSpace)
                             : area.adjusted(valueLabelSpace, 0, 0, -categorySpace);
    const auto toPos = [&](qreal v) {
        const qreal t = (v - min) / (max - min);
        return horizontal ? plot.left() + t * plot.width() : plot.bottom() - t * plot.height();
    };
    // Gridlines and value labels.
    painter->setPen(QPen(QColor(217, 217, 217), 0.75));
    for (qreal v = min; v <= max + step / 2; v += step) {
        const qreal p = toPos(v);
        const QString text = percent ? QString::number(v) + QLatin1Char('%') : number(v);
        if (horizontal) {
            painter->drawLine(QPointF(p, plot.top()), QPointF(p, plot.bottom()));
            label(painter, QRectF(p - 30, plot.bottom() + 2, 60, labelSize * 1.5), text, int(Qt::AlignHCenter), labelSize);
        } else {
            painter->drawLine(QPointF(plot.left(), p), QPointF(plot.right(), p));
            label(painter, QRectF(area.left(), p - labelSize, valueLabelSpace - 4, labelSize * 2), text,
                  int(Qt::AlignRight), labelSize);
        }
    }
    const qreal band = (horizontal ? plot.height() : plot.width()) / categories;
    // The bars share each band: one slot per clustered bar series, one per
    // stacked bar plot.
    QList<int> slotOf(chart.series.size(), -1);
    QHash<int, int> stackSlot;
    int slotCount = 0;
    for (int s = 0; s < chart.series.size(); ++s) {
        const int plotIndex = chart.series.at(s).plot;
        const PlotLook look = lookOf(chart, plotIndex);
        if (!look.barLike()) {
            continue;
        }
        if (look.stacked) {
            if (!stackSlot.contains(plotIndex)) {
                stackSlot.insert(plotIndex, slotCount++);
            }
            slotOf[s] = stackSlot.value(plotIndex);
        } else {
            slotOf[s] = slotCount++;
        }
    }
    // Areas first, then bars, then lines over them.
    QList<int> order;
    for (const QStringView pass : {QStringView(u"area"), QStringView(u"bar"), QStringView(u"line")}) {
        for (int s = 0; s < chart.series.size(); ++s) {
            const PlotLook look = lookOf(chart, chart.series.at(s).plot);
            const QStringView kind = look.barLike() ? u"bar" : (look.type == u"area" ? u"area" : u"line");
            if (kind == pass) {
                order.append(s);
            }
        }
    }
    QHash<int, QList<qreal>> positiveBase;
    QHash<int, QList<qreal>> negativeBase;
    QHash<int, QList<QPolygonF>> areaTops;
    for (const int s : std::as_const(order)) {
        const ChartSeries &series = chart.series.at(s);
        const PlotLook look = lookOf(chart, series.plot);
        const bool stacked = look.stacked;
        const QColor color = seriesColor(chart, s);
        QList<qreal> &positives = positiveBase[series.plot];
        QList<qreal> &negatives = negativeBase[series.plot];
        positives.resize(categories, 0.0);
        negatives.resize(categories, 0.0);
        QPolygonF points;
        for (int c = 0; c < categories; ++c) {
            const qreal v = drawnValue(chart, s, c);
            qreal from = 0;
            qreal to = v;
            if (stacked) {
                qreal &base = v >= 0 ? positives[c] : negatives[c];
                from = base;
                to = base + v;
                base = to;
            }
            const qreal bandStart = (horizontal ? plot.top() : plot.left()) + c * band;
            if (look.barLike()) {
                const qreal gap = band * 0.6 / 2.5; // PowerPoint's 150 % gap width
                const qreal width = (band - 2 * gap) / qMax(1, slotCount);
                const qreal offset = gap + slotOf.at(s) * width;
                const qreal a = toPos(from);
                const qreal b = toPos(to);
                const QRectF bar = horizontal ? QRectF(QPointF(qMin(a, b), bandStart + offset), QSizeF(std::abs(b - a), width))
                                              : QRectF(QPointF(bandStart + offset, qMin(a, b)), QSizeF(width, std::abs(b - a)));
                painter->fillRect(bar, color);
            } else {
                points.append(horizontal ? QPointF(toPos(to), bandStart + band / 2) : QPointF(bandStart + band / 2, toPos(to)));
            }
        }
        if (!look.barLike() && look.type != u"area") {
            painter->setPen(QPen(color, 2.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(points);
            painter->setBrush(color);
            for (const QPointF &p : points) {
                painter->drawEllipse(p, 2.5, 2.5);
            }
        } else if (look.type == u"area") {
            QList<QPolygonF> &tops = areaTops[series.plot];
            QPolygonF shape = points;
            if (stacked && !tops.isEmpty()) {
                const QPolygonF &below = tops.last();
                for (qsizetype i = below.size() - 1; i >= 0; --i) {
                    shape.append(below.at(i));
                }
            } else if (!points.isEmpty()) {
                shape.append(QPointF(points.last().x(), toPos(qMax(min, 0.0))));
                shape.append(QPointF(points.first().x(), toPos(qMax(min, 0.0))));
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPolygon(shape);
            tops.append(points);
        }
    }
    // Axis line and category labels.
    painter->setPen(QPen(QColor(191, 191, 191), 0.75));
    const qreal zero = toPos(qBound(min, 0.0, max));
    if (horizontal) {
        painter->drawLine(QPointF(zero, plot.top()), QPointF(zero, plot.bottom()));
    } else {
        painter->drawLine(QPointF(plot.left(), zero), QPointF(plot.right(), zero));
    }
    for (int c = 0; c < categories; ++c) {
        const qreal start = (horizontal ? plot.top() : plot.left()) + c * band;
        if (horizontal) {
            label(painter, QRectF(area.left(), start, categorySpace - 4, band), categoryName(chart, c), int(Qt::AlignRight),
                  labelSize);
        } else {
            label(painter, QRectF(start, plot.bottom() + 2, band, categorySpace - 2), categoryName(chart, c),
                  int(Qt::AlignHCenter), labelSize, QColor(89, 89, 89), false, TextAnchor::Top);
        }
    }
}

void paintScatter(QPainter *painter, const QRectF &area, const ChartData &chart)
{
    qreal xLow = 0, xHigh = 0, yLow = 0, yHigh = 0;
    bool first = true;
    for (const ChartSeries &series : chart.series) {
        for (int i = 0; i < series.values.size(); ++i) {
            const qreal x = i < series.xValues.size() ? series.xValues.at(i) : i + 1;
            const qreal y = valueAt(series, i);
            if (std::isnan(x)) {
                continue;
            }
            xLow = first ? x : qMin(xLow, x);
            xHigh = first ? x : qMax(xHigh, x);
            yLow = first ? qMin(0.0, y) : qMin(yLow, y);
            yHigh = first ? y : qMax(yHigh, y);
            first = false;
        }
    }
    qreal x0, x1, xs, y0, y1, ys;
    niceScale(qMin(0.0, xLow), xHigh, &x0, &x1, &xs);
    niceScale(yLow, yHigh, &y0, &y1, &ys);
    const QRectF plot = area.adjusted(36, 0, 0, -18);
    const auto map = [&](qreal x, qreal y) {
        return QPointF(plot.left() + (x - x0) / (x1 - x0) * plot.width(), plot.bottom() - (y - y0) / (y1 - y0) * plot.height());
    };
    painter->setPen(QPen(QColor(217, 217, 217), 0.75));
    for (qreal y = y0; y <= y1 + ys / 2; y += ys) {
        painter->drawLine(map(x0, y), map(x1, y));
        label(painter, QRectF(area.left(), map(x0, y).y() - 9, 32, 18), number(y), int(Qt::AlignRight), 9);
    }
    for (qreal x = x0; x <= x1 + xs / 2; x += xs) {
        label(painter, QRectF(map(x, y0).x() - 30, plot.bottom() + 2, 60, 14), number(x), int(Qt::AlignHCenter), 9);
    }
    for (int s = 0; s < chart.series.size(); ++s) {
        const ChartSeries &series = chart.series.at(s);
        painter->setPen(Qt::NoPen);
        painter->setBrush(seriesColor(chart, s));
        for (int i = 0; i < series.values.size(); ++i) {
            const qreal x = i < series.xValues.size() ? series.xValues.at(i) : i + 1;
            const qreal y = i < series.values.size() ? series.values.at(i) : qQNaN();
            if (std::isfinite(x) && std::isfinite(y)) painter->drawEllipse(map(x, y), 3, 3);
        }
    }
}

} // namespace

void paintChart(QPainter *painter, const QRectF &rect, const ChartData &chart)
{
    if (chart.type.isEmpty() || chart.series.isEmpty()) {
        paintPlaceholderBox(painter, rect, QStringLiteral("Chart"));
        return;
    }
    painter->save();
    QRectF area = rect.adjusted(6, 6, -6, -6);
    if (!chart.title.isEmpty()) {
        const qreal size = qBound<qreal>(10.0, rect.height() / 16.0, 18.0);
        label(painter, QRectF(area.left(), area.top(), area.width(), size * 1.6), chart.title, int(Qt::AlignHCenter), size,
              QColor(64, 64, 64));
        area.setTop(area.top() + size * 1.9);
    }
    const bool pie = chart.type == u"pie" || chart.type == u"doughnut";
    if (chart.legend) {
        QStringList names;
        QList<QColor> colors;
        if (pie) {
            const ChartSeries &series = chart.series.first();
            for (int i = 0; i < series.values.size(); ++i) {
                names.append(categoryName(chart, i));
                const QColor own = i < series.pointColors.size() ? series.pointColors.at(i) : QColor();
                colors.append(own.isValid() ? own : paletteColor(chart, i));
            }
        } else {
            for (int s = 0; s < chart.series.size(); ++s) {
                names.append(chart.series.at(s).name.isEmpty() ? QStringLiteral("Series %1").arg(s + 1)
                                                               : chart.series.at(s).name);
                colors.append(seriesColor(chart, s));
            }
        }
        const qreal row = 14;
        const bool side = chart.legendPosition.isEmpty() || chart.legendPosition == u"r" || chart.legendPosition == u"l"
            || chart.legendPosition == u"tr";
        if (side) {
            const qreal width = qMin(area.width() * 0.3, 110.0);
            const bool left = chart.legendPosition == u"l";
            const QRectF legend(left ? area.left() : area.right() - width, area.center().y() - names.size() * row / 2,
                                width, names.size() * row);
            for (int i = 0; i < names.size(); ++i) {
                const QRectF entry(legend.left(), legend.top() + i * row, legend.width(), row);
                painter->fillRect(QRectF(entry.left(), entry.center().y() - 3.5, 7, 7), colors.at(i));
                label(painter, entry.adjusted(11, 0, 0, 0), names.at(i), int(Qt::AlignLeft), 9);
            }
            area = left ? area.adjusted(width + 6, 0, 0, 0) : area.adjusted(0, 0, -width - 6, 0);
        } else {
            const bool top = chart.legendPosition == u"t";
            const QRectF legend(area.left(), top ? area.top() : area.bottom() - row, area.width(), row);
            const qreal slot = legend.width() / qMax<qsizetype>(1, names.size());
            for (int i = 0; i < names.size(); ++i) {
                const QRectF entry(legend.left() + i * slot, legend.top(), slot, row);
                painter->fillRect(QRectF(entry.left() + 2, entry.center().y() - 3.5, 7, 7), colors.at(i));
                label(painter, entry.adjusted(13, 0, 0, 0), names.at(i), int(Qt::AlignLeft), 9);
            }
            area = top ? area.adjusted(0, row + 4, 0, 0) : area.adjusted(0, 0, 0, -row - 4);
        }
    }
    if (pie) {
        paintPie(painter, area, chart);
    } else if (chart.type == u"scatter") {
        paintScatter(painter, area, chart);
    } else {
        paintCategories(painter, area, chart);
    }
    painter->restore();
}

} // namespace QindaOffice::Show::Render
