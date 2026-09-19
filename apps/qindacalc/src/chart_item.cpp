// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_item.h"

#include <QPainter>

#include <cmath>

namespace QindaOffice::Apps::QindaCalc {

namespace {

// The Office series palette — the colours an authored workbook expects.
const QColor kSeriesColors[] = {
    QColor(0x44, 0x72, 0xC4), QColor(0xED, 0x7C, 0x31), QColor(0xA5, 0xA5, 0xA5),
    QColor(0xFF, 0xC0, 0x00), QColor(0x5B, 0x9B, 0xD5), QColor(0x70, 0xAD, 0x47),
    QColor(0x26, 0x44, 0x78), QColor(0x9E, 0x48, 0x0E), QColor(0x63, 0x63, 0x63),
    QColor(0x99, 0x73, 0x00),
};

QColor seriesColor(int index)
{
    return kSeriesColors[index % 10];
}

const int kTitleH = 26;
const int kLegendH = 22;
const int kAxisLabelW = 58; // wide enough for a 7-digit tick
const int kCatLabelH = 18;
const int kPad = 8;

// A readable axis top: 4 ticks from 0 (or the negative min) to a rounded max.
double axisCeil(double maxValue)
{
    if (maxValue <= 0) {
        return 1;
    }
    const double magnitude = std::pow(10.0, std::floor(std::log10(maxValue)));
    const double unit = maxValue / magnitude;
    const double nice = unit <= 1 ? 1 : unit <= 2 ? 2 : unit <= 2.5 ? 2.5
                        : unit <= 5 ? 5 : 10;
    return nice * magnitude;
}

double axisFloor(double minValue)
{
    if (minValue >= 0) {
        return 0;
    }
    return -axisCeil(-minValue);
}

QString tickText(double value)
{
    return value == std::floor(value) && std::abs(value) < 1e15
        ? QString::number(qint64(value))
        : QString::number(value, 'g', 4);
}

} // namespace

ChartItem::ChartItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void ChartItem::setChartData(const QVariantMap &data)
{
    m_data = data;
    m_title = data.value(QStringLiteral("title")).toString();
    m_kind = data.value(QStringLiteral("kind")).toInt();
    const QVariantList categories = data.value(QStringLiteral("categories")).toList();
    m_categories.clear();
    m_categories.reserve(categories.size());
    for (const QVariant &category : categories) {
        m_categories.append(category.toString());
    }
    m_seriesNames.clear();
    m_seriesValues.clear();
    m_minValue = 0;
    m_maxValue = 0;
    const QVariantList series = data.value(QStringLiteral("series")).toList();
    for (const QVariant &entry : series) {
        const QVariantMap map = entry.toMap();
        m_seriesNames.append(map.value(QStringLiteral("name")).toString());
        QVector<double> values;
        const QVariantList raw = map.value(QStringLiteral("values")).toList();
        values.reserve(raw.size());
        for (const QVariant &v : raw) {
            const double number = v.toDouble();
            m_minValue = qMin(m_minValue, number);
            m_maxValue = qMax(m_maxValue, number);
            values.append(number);
        }
        m_seriesValues.append(values);
    }
    emit chartDataChanged();
    update();
}

void ChartItem::paint(QPainter *painter)
{
    if (m_seriesValues.isEmpty()) {
        return;
    }
    QRectF area = boundingRect().adjusted(kPad, kPad, -kPad, -kPad);
    if (area.isEmpty()) {
        return;
    }
    painter->setPen(m_ink);
    painter->setBrush(Qt::NoBrush);
    // Title and legend take their bands; the plot gets the rest.
    if (!m_title.isEmpty()) {
        const QRectF titleRect(area.left(), area.top(), area.width(), kTitleH);
        QFont titleFont = painter->font();
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, m_title);
        area.adjust(0, kTitleH, 0, 0);
    }
    const QRectF legendRect(area.left(), area.bottom() - kLegendH, area.width(), kLegendH);
    area.adjust(0, 0, 0, -kLegendH);

    const bool pie = m_kind == 4;
    if (!pie) {
        const QRectF plot = area.adjusted(kAxisLabelW, 0, 0, -kCatLabelH);
        // Gridlines + value ticks at 4 even steps over [floor, ceil].
        const double floor = axisFloor(m_minValue);
        const double ceil = axisCeil(m_maxValue);
        const double span = ceil - floor;
        const auto yFor = [&](double v) {
            return plot.bottom() - (v - floor) / span * plot.height();
        };
        painter->setPen(QColor(0x99, 0x99, 0x99, 120));
        QFontMetrics metrics(painter->font());
        for (int t = 0; t <= 4; ++t) {
            const double value = floor + span * t / 4.0;
            const qreal y = yFor(value);
            painter->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter->setPen(m_ink);
            painter->drawText(QRectF(plot.left() - kAxisLabelW, y - metrics.height() / 2.0,
                                     kAxisLabelW - 4, metrics.height()),
                              Qt::AlignRight | Qt::AlignVCenter, tickText(value));
            painter->setPen(QColor(0x99, 0x99, 0x99, 120));
        }
        if (m_kind == 0 || m_kind == 1) {
            paintBar(*painter, plot, m_kind == 1);
        } else {
            // Kind: Line=2, Area=3, Pie=4 (above), Scatter=5.
            paintLines(*painter, plot, m_kind == 3, m_kind == 5);
        }
        // Category labels along the bottom, sparse when crowded.
        painter->setPen(m_ink);
        const int count = m_categories.size();
        const int step = qMax(1, (count + 11) / 12);
        for (int i = 0; i < count; i += step) {
            const qreal x = plot.left() + (i + 0.5) / count * plot.width();
            painter->drawText(QRectF(x - 40, plot.bottom() + 2, 80, kCatLabelH - 2),
                              Qt::AlignHCenter | Qt::AlignTop,
                              metrics.elidedText(m_categories.at(i), Qt::ElideRight, 78));
        }
    } else {
        // Pie: one slice per VALUE of the first series.
        const QVector<double> &values = m_seriesValues.first();
        double total = 0;
        for (const double v : values) {
            total += qMax(0.0, v);
        }
        if (total > 0) {
            const QRectF pieRect = area.adjusted(qMax(0.0, (area.width() - area.height()) / 2),
                                                 0,
                                                 qMin(0.0, (area.height() - area.width()) / 2),
                                                 0);
            int startAngle = 0;
            for (int i = 0; i < values.size(); ++i) {
                const int spanAngle = int(5760.0 * qMax(0.0, values.at(i)) / total);
                painter->setPen(Qt::NoPen);
                painter->setBrush(seriesColor(i));
                painter->drawPie(pieRect, startAngle, spanAngle);
                startAngle += spanAngle;
            }
        }
    }
    // Legend: a swatch + name per series (pie: per category).
    painter->setPen(m_ink);
    qreal legendX = legendRect.left();
    const QStringList names = pie ? m_categories : m_seriesNames;
    const int limit = qMin(names.size(), pie ? m_seriesValues.first().size()
                                                       : m_seriesNames.size());
    for (int i = 0; i < limit && legendX < legendRect.right() - 60; ++i) {
        painter->fillRect(QRectF(legendX, legendRect.center().y() - 5, 10, 10), seriesColor(i));
        legendX += 14;
        QFontMetrics metrics(painter->font());
        const QString label = names.at(i).isEmpty()
            ? QStringLiteral("Series %1").arg(i + 1)
            : names.at(i);
        painter->drawText(QRectF(legendX, legendRect.top(), 120, legendRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(label, Qt::ElideRight, 116));
        legendX += metrics.horizontalAdvance(label) + 18;
    }
}

void ChartItem::paintBar(QPainter &painter, const QRectF &plot, bool stacked)
{
    const double floor = axisFloor(m_minValue);
    const double ceil = axisCeil(m_maxValue);
    const double span = ceil - floor;
    const auto yFor = [&](double v) {
        return plot.bottom() - (v - floor) / span * plot.height();
    };
    const int categories = qMax(1, int(m_seriesValues.first().size()));
    const qreal slot = plot.width() / categories;
    const qreal zeroY = yFor(0);
    for (int c = 0; c < categories; ++c) {
        qreal x = plot.left() + c * slot;
        if (stacked) {
            // Cumulative offsets in VALUE space: positives grow up from zero,
            // negatives down.
            double positive = 0;
            double negative = 0;
            for (int s = 0; s < m_seriesValues.size(); ++s) {
                const double value = m_seriesValues.at(s).value(c);
                painter.setPen(Qt::NoPen);
                painter.setBrush(seriesColor(s));
                if (value >= 0) {
                    const qreal bottom = yFor(positive);
                    positive += value;
                    const qreal top = yFor(positive);
                    painter.drawRect(QRectF(x + slot * 0.15, top, slot * 0.7, bottom - top));
                } else {
                    const qreal top = yFor(negative);
                    negative += value;
                    const qreal bottom = yFor(negative);
                    painter.drawRect(QRectF(x + slot * 0.15, top, slot * 0.7, bottom - top));
                }
            }
        } else {
            const qreal barWidth = slot * 0.7 / qMax(1, int(m_seriesValues.size()));
            for (int s = 0; s < m_seriesValues.size(); ++s) {
                const double value = m_seriesValues.at(s).value(c);
                painter.setPen(Qt::NoPen);
                painter.setBrush(seriesColor(s));
                const qreal y = yFor(value);
                painter.drawRect(QRectF(x, qMin(y, zeroY), barWidth, qAbs(zeroY - y)));
                x += barWidth;
            }
        }
    }
}

void ChartItem::paintLines(QPainter &painter, const QRectF &plot, bool fill, bool pointsOnly)
{
    const double floor = axisFloor(m_minValue);
    const double ceil = axisCeil(m_maxValue);
    const double span = ceil - floor;
    const auto yFor = [&](double v) {
        return plot.bottom() - (v - floor) / span * plot.height();
    };
    for (int s = 0; s < m_seriesValues.size(); ++s) {
        const QVector<double> &values = m_seriesValues.at(s);
        const int count = values.size();
        if (count == 0) {
            continue;
        }
        QPolygonF line;
        line.reserve(count);
        for (int i = 0; i < count; ++i) {
            line.append(QPointF(plot.left() + (i + 0.5) / count * plot.width(), yFor(values.at(i))));
        }
        if (fill) {
            QPolygonF area = line;
            area.append(QPointF(line.last().x(), yFor(0)));
            area.append(QPointF(line.first().x(), yFor(0)));
            QColor fillColor = seriesColor(s);
            fillColor.setAlpha(90);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.drawPolygon(area);
        }
        if (!pointsOnly) {
            painter.setPen(QPen(seriesColor(s), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(line);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(seriesColor(s));
        for (const QPointF &point : line) {
            painter.drawEllipse(point, 2.5, 2.5);
        }
    }
}

} // namespace QindaOffice::Apps::QindaCalc
