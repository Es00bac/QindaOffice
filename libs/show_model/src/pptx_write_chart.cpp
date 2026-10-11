// SPDX-License-Identifier: GPL-3.0-or-later
// An edited chart for the pptx writer (pptx_write_shapes.h; M67, D-076;
// M82, D-091). The chart part is never re-derived from the model: the
// SOURCE part is copied and only what the edit touched is rewritten in it —
//   - every plot's series (pptx_write_chart_series.cpp): names, categories
//     — multi-level ones with their outer levels — and values, the series
//     formulas recomputed for the data's new extent where the first
//     series' formulas placed it, the caches PowerPoint draws from refilled;
//     series matched by c:idx, so a combo chart's columns and lines both
//     follow the data and per-point colours and labels stay with their
//     points;
//   - a kind change rebuilds the plot it changes (a combo chart's plots one
//     by one) as the new kind, keeping the series' names, colours, per-point
//     colours and data labels, with the axes that kind needs
//     (pptx_write_chart_kind.cpp);
//   - the embedded workbook (c:externalData) is rewritten through calc_io
//     (chart_workbook.h) so PowerPoint's Edit Data shows the same numbers.
// Titles, legends, text and number formats, chart styles and colours (the
// chartStyle / chartColorStyle parts) stay as they were.

#include "pptx_write_shapes.h"

#include "chart_interchange.h"
#include "pptx_read_shapes.h"
#include "pptx_write_chart_internal.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

// The c:chart reference of a chart's graphic frame.
XmlElement *chartReference(XmlElement &frame)
{
    XmlElement *graphic = frame.child(nsA(), u"graphic");
    XmlElement *data = graphic != nullptr ? graphic->child(nsA(), u"graphicData") : nullptr;
    return data != nullptr ? data->child(nsC(), u"chart") : nullptr;
}

QString sourceChartPart(const PartWriter &out, const QString &part, const QString &relationshipId)
{
    const auto rel = out.session.source ? out.session.source->relationship(part, relationshipId) : std::nullopt;
    return rel && !rel->external ? out.session.source->targetPart(part, *rel) : QString();
}

// Whether two charts hold the same data in the same kinds (colours aside).
bool sameData(const ChartData &a, const ChartData &b)
{
    if (a.type != b.type || a.grouping != b.grouping || a.series.size() != b.series.size() || a.plots != b.plots
        || a.categoryLevels != b.categoryLevels) {
        return false;
    }
    for (qsizetype i = 0; i < a.series.size(); ++i) {
        ChartSeries x = a.series.at(i);
        ChartSeries y = b.series.at(i);
        x.color = y.color = QColor();
        x.pointColors.clear();
        y.pointColors.clear();
        if (!(x == y)) {
            return false;
        }
    }
    return true;
}

// The data a chart part caches (colours resolved against the Office theme:
// only the data is compared).
ChartData cachedChart(const std::shared_ptr<const Ooxml::Package> &package, const QString &part)
{
    static const ThemeContext theme = [] {
        ThemeContext office;
        office.theme = Ooxml::defaultOfficeTheme();
        return office;
    }();
    PartContext ctx;
    ctx.package = package;
    ctx.part = part;
    ctx.theme = &theme;
    return readChart(ctx, part);
}

} // namespace

DetachedChart detachChart(const PartWriter &out, XmlElement &frame, const QString &part)
{
    DetachedChart edit;
    edit.reference = chartReference(frame);
    if (edit.reference != nullptr) {
        edit.sourcePart = sourceChartPart(out, part, edit.reference->attribute(nsR(), u"id"));
        edit.reference->removeAttribute(nsR(), u"id");
    }
    return edit;
}

// Writes the edited chart part and names it in the frame; false when the
// source chart part could not be used.
bool attachChart(PartWriter &out, const DetachedChart &edit, const ChartData &chart, const ChartData &previous)
{
    const QString rel = edit.sourcePart.isEmpty() ? QString() : writeEditedChart(out, edit.sourcePart, chart, previous);
    if (rel.isEmpty()) {
        return false;
    }
    edit.reference->setAttribute(nsR(), QStringLiteral("id"), rel);
    return true;
}

DetachedChart detachEditedCopy(PartWriter &out, XmlElement &frame, const QString &part, const ChartData &chart,
                               ChartData *cached)
{
    XmlElement *reference = frame.name == u"graphicFrame" ? chartReference(frame) : nullptr;
    const QString chartPart = reference != nullptr ? sourceChartPart(out, part, reference->attribute(nsR(), u"id")) : QString();
    if (chartPart.isEmpty()) {
        return {};
    }
    *cached = cachedChart(out.session.source, chartPart);
    return sameData(*cached, chart) ? DetachedChart() : detachChart(out, frame, part);
}

QString writeEditedChart(PartWriter &out, const QString &sourcePart, const ChartData &chart, const ChartData &previous)
{
    const QString part = ChartIO::write(out.session.writer, out.session.source, sourcePart,
                                       chart, previous, QStringLiteral("ppt"));
    return part.isEmpty() ? QString() : out.relate(relType("chart"), part);
}

} // namespace QindaOffice::Show::Pptx
