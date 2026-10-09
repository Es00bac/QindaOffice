// SPDX-License-Identifier: GPL-3.0-or-later
// A chart part (c:chartSpace) for the pptx reader (pptx_read_shapes.h):
// the first plot's type and grouping, the title, the legend and every
// series' cached names, categories and values. Colours resolve against
// the slide's theme (a chart's own c:clrMapOvr is honoured).

#include "pptx_read_shapes.h"

#include <cmath>
#include <limits>

namespace QindaOffice::Show::Pptx {

namespace {

// The points of a c:strCache / c:numCache (or a reference holding one),
// placed by their idx.
QStringList cachedStrings(const Ooxml::XmlElement *holder)
{
    QStringList values;
    if (holder == nullptr) {
        return values;
    }
    const Ooxml::XmlElement *cache = holder->descendant(nsC(), u"strCache");
    if (cache == nullptr) {
        cache = holder->descendant(nsC(), u"numCache");
    }
    if (cache == nullptr) {
        // c:multiLvlStrCache: the innermost level names the categories.
        if (const Ooxml::XmlElement *multi = holder->descendant(nsC(), u"multiLvlStrCache")) {
            cache = multi->child(nsC(), u"lvl");
        }
    }
    if (cache == nullptr) {
        if (const Ooxml::XmlElement *v = holder->child(nsC(), u"v")) {
            values.append(v->text());
        }
        return values;
    }
    const int count = cache->child(nsC(), u"ptCount") != nullptr
        ? cache->child(nsC(), u"ptCount")->attr(u"val").toInt()
        : 0;
    values = QStringList(qBound(0, count, 100000), QString());
    for (const Ooxml::XmlElement *pt : cache->elements(nsC(), u"pt")) {
        const int idx = pt->attr(u"idx").toInt();
        const Ooxml::XmlElement *v = pt->child(nsC(), u"v");
        if (idx >= 0 && idx < 100000) {
            while (values.size() <= idx) {
                values.append(QString());
            }
            values[idx] = v != nullptr ? v->text() : QString();
        }
    }
    return values;
}

QList<qreal> cachedNumbers(const Ooxml::XmlElement *holder)
{
    QList<qreal> numbers;
    for (const QString &text : cachedStrings(holder)) {
        bool ok = false;
        const double value = text.toDouble(&ok);
        numbers.append(ok ? value : std::numeric_limits<qreal>::quiet_NaN());
    }
    return numbers;
}

QColor seriesFill(const PartContext &ctx, const Ooxml::XmlElement *spPr)
{
    if (spPr == nullptr) {
        return {};
    }
    const ResolvedFill fill = fillFrom(ctx, spPr);
    if (fill.present && fill.plain.alpha() > 0) {
        return fill.plain;
    }
    // Line charts colour their series by the line.
    const ResolvedLine line = lineFrom(ctx, spPr->child(nsA(), u"ln"));
    return line.present && line.color.alpha() > 0 ? line.color : QColor();
}

QString richText(const Ooxml::XmlElement *tx)
{
    if (tx == nullptr) {
        return {};
    }
    QStringList paragraphs;
    if (const Ooxml::XmlElement *rich = tx->child(nsC(), u"rich")) {
        for (const Ooxml::XmlElement *p : rich->elements(nsA(), u"p")) {
            QString line;
            for (const Ooxml::XmlElement *r : p->elements(nsA(), u"r")) {
                line += r->text();
            }
            paragraphs.append(line);
        }
        return paragraphs.join(QLatin1Char(' ')).trimmed();
    }
    return cachedStrings(tx).join(QLatin1Char(' ')).trimmed();
}

} // namespace

ChartData readChart(const PartContext &slideCtx, const QString &chartPart)
{
    ChartData chart;
    const Ooxml::XmlElementPtr root = slideCtx.package ? slideCtx.package->xml(chartPart) : nullptr;
    const Ooxml::XmlElement *body = root ? root->child(nsC(), u"chart") : nullptr;
    if (body == nullptr) {
        return chart;
    }
    PartContext ctx = slideCtx;
    ctx.part = chartPart;
    if (const Ooxml::XmlElement *override = root->child(nsC(), u"clrMapOvr")) {
        for (const Ooxml::XmlAttribute &a : override->attributes) {
            ctx.map.insert(a.name, a.value);
        }
    }
    for (int i = 1; i <= 6; ++i) {
        Ooxml::XmlElement accent(nsA(), QStringLiteral("schemeClr"));
        accent.setAttribute(QString(), QStringLiteral("val"), QStringLiteral("accent%1").arg(i));
        chart.palette.append(color(ctx, &accent));
    }
    const bool titleDeleted = body->child(nsC(), u"autoTitleDeleted") != nullptr
        && body->child(nsC(), u"autoTitleDeleted")->attr(u"val") != u"0";
    if (const Ooxml::XmlElement *title = body->child(nsC(), u"title")) {
        chart.title = richText(title->child(nsC(), u"tx"));
    }
    if (const Ooxml::XmlElement *legend = body->child(nsC(), u"legend")) {
        chart.legend = true;
        if (const Ooxml::XmlElement *pos = legend->child(nsC(), u"legendPos")) {
            chart.legendPosition = pos->attr(u"val", QStringLiteral("r"));
        }
    }
    const Ooxml::XmlElement *plot = body->child(nsC(), u"plotArea");
    if (plot == nullptr) {
        return chart;
    }
    const Ooxml::XmlElement *kind = nullptr;
    for (const Ooxml::XmlElement *e : plot->elements()) {
        if (e->ns == nsC() && e->name.endsWith(QLatin1String("Chart"))) {
            kind = e;
            break;
        }
    }
    if (kind == nullptr) {
        return chart;
    }
    const QString name = kind->name;
    if (name == u"barChart" || name == u"bar3DChart") {
        const Ooxml::XmlElement *dir = kind->child(nsC(), u"barDir");
        chart.type = dir != nullptr && dir->attr(u"val") == u"bar" ? QStringLiteral("bar") : QStringLiteral("column");
    } else if (name == u"lineChart" || name == u"line3DChart" || name == u"radarChart" || name == u"stockChart") {
        chart.type = QStringLiteral("line");
    } else if (name == u"areaChart" || name == u"area3DChart") {
        chart.type = QStringLiteral("area");
    } else if (name == u"pieChart" || name == u"pie3DChart" || name == u"ofPieChart") {
        chart.type = QStringLiteral("pie");
    } else if (name == u"doughnutChart") {
        chart.type = QStringLiteral("doughnut");
    } else if (name == u"scatterChart" || name == u"bubbleChart") {
        chart.type = QStringLiteral("scatter");
    }
    if (const Ooxml::XmlElement *grouping = kind->child(nsC(), u"grouping")) {
        chart.grouping = grouping->attr(u"val");
    }
    for (const Ooxml::XmlElement *ser : kind->elements(nsC(), u"ser")) {
        ChartSeries series;
        series.name = richText(ser->child(nsC(), u"tx"));
        series.color = seriesFill(ctx, ser->child(nsC(), u"spPr"));
        if (chart.type == u"scatter") {
            series.categories = cachedStrings(ser->child(nsC(), u"xVal"));
            series.xValues = cachedNumbers(ser->child(nsC(), u"xVal"));
            series.values = cachedNumbers(ser->child(nsC(), u"yVal"));
        } else {
            series.categories = cachedStrings(ser->child(nsC(), u"cat"));
            series.values = cachedNumbers(ser->child(nsC(), u"val"));
        }
        for (const Ooxml::XmlElement *point : ser->elements(nsC(), u"dPt")) {
            const int idx = point->child(nsC(), u"idx") != nullptr ? point->child(nsC(), u"idx")->attr(u"val").toInt() : -1;
            if (idx < 0 || idx > 100000) {
                continue;
            }
            while (series.pointColors.size() <= idx) {
                series.pointColors.append(QColor());
            }
            series.pointColors[idx] = seriesFill(ctx, point->child(nsC(), u"spPr"));
        }
        chart.series.append(series);
    }
    // A single series titles its chart unless the title was deleted.
    if (chart.title.isEmpty() && !titleDeleted && body->child(nsC(), u"title") != nullptr && chart.series.size() == 1) {
        chart.title = chart.series.first().name;
    }
    return chart;
}

} // namespace QindaOffice::Show::Pptx
