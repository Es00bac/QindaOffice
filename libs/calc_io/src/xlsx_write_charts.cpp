// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_write_charts.h"

#include <QXmlStreamWriter>

namespace QindaOffice::Calc::IO {

namespace {

// Axis ids are arbitrary integers shared between the plot-type element and
// its axes; two are enough for every kind here.
constexpr qint64 kCatAxId = 100000001;
constexpr qint64 kValAxId = 100000002;

void writeAxisIds(QXmlStreamWriter &writer)
{
    writer.writeEmptyElement(QStringLiteral("c:axId"));
    writer.writeAttribute(QStringLiteral("val"), QString::number(kCatAxId));
    writer.writeEmptyElement(QStringLiteral("c:axId"));
    writer.writeAttribute(QStringLiteral("val"), QString::number(kValAxId));
}

void writeAxes(QXmlStreamWriter &writer, bool scatter)
{
    // catAx for category kinds, a second valAx for scatter (both numeric).
    writer.writeStartElement(scatter ? QStringLiteral("c:valAx") : QStringLiteral("c:catAx"));
    writer.writeEmptyElement(QStringLiteral("c:axId"));
    writer.writeAttribute(QStringLiteral("val"), QString::number(kCatAxId));
    writer.writeStartElement(QStringLiteral("c:scaling"));
    writer.writeEmptyElement(QStringLiteral("c:orientation"));
    writer.writeAttribute(QStringLiteral("val"), QStringLiteral("minMax"));
    writer.writeEndElement(); // scaling
    writer.writeEmptyElement(QStringLiteral("c:delete"));
    writer.writeAttribute(QStringLiteral("val"), QStringLiteral("0"));
    writer.writeEmptyElement(QStringLiteral("c:axPos"));
    writer.writeAttribute(QStringLiteral("val"), QStringLiteral("b"));
    writer.writeEmptyElement(QStringLiteral("c:crossAx"));
    writer.writeAttribute(QStringLiteral("val"), QString::number(kValAxId));
    writer.writeEndElement(); // catAx/valAx

    writer.writeStartElement(QStringLiteral("c:valAx"));
    writer.writeEmptyElement(QStringLiteral("c:axId"));
    writer.writeAttribute(QStringLiteral("val"), QString::number(kValAxId));
    writer.writeStartElement(QStringLiteral("c:scaling"));
    writer.writeEmptyElement(QStringLiteral("c:orientation"));
    writer.writeAttribute(QStringLiteral("val"), QStringLiteral("minMax"));
    writer.writeEndElement(); // scaling
    writer.writeEmptyElement(QStringLiteral("c:delete"));
    writer.writeAttribute(QStringLiteral("val"), QStringLiteral("0"));
    writer.writeEmptyElement(QStringLiteral("c:axPos"));
    writer.writeAttribute(QStringLiteral("val"), QStringLiteral("l"));
    writer.writeEmptyElement(QStringLiteral("c:majorGridlines"));
    writer.writeEmptyElement(QStringLiteral("c:crossAx"));
    writer.writeAttribute(QStringLiteral("val"), QString::number(kCatAxId));
    writer.writeEndElement(); // valAx
}

void writeSeries(QXmlStreamWriter &writer, const ChartSpec &spec)
{
    const bool scatter = spec.kind == ChartSpec::Kind::Scatter;
    for (int i = 0; i < spec.series.size(); ++i) {
        const ChartSeriesSpec &ser = spec.series.at(i);
        writer.writeStartElement(QStringLiteral("c:ser"));
        writer.writeEmptyElement(QStringLiteral("c:idx"));
        writer.writeAttribute(QStringLiteral("val"), QString::number(i));
        writer.writeEmptyElement(QStringLiteral("c:order"));
        writer.writeAttribute(QStringLiteral("val"), QString::number(i));
        if (!ser.name.isEmpty()) {
            writer.writeStartElement(QStringLiteral("c:tx"));
            writer.writeTextElement(QStringLiteral("c:v"), ser.name);
            writer.writeEndElement(); // tx
        }
        if (!ser.catRange.isEmpty()) {
            writer.writeStartElement(scatter ? QStringLiteral("c:xVal") : QStringLiteral("c:cat"));
            writer.writeStartElement(QStringLiteral("c:strRef"));
            writer.writeTextElement(QStringLiteral("c:f"), ser.catRange);
            writer.writeEndElement(); // strRef
            writer.writeEndElement(); // cat/xVal
        }
        writer.writeStartElement(scatter ? QStringLiteral("c:yVal") : QStringLiteral("c:val"));
        writer.writeStartElement(QStringLiteral("c:numRef"));
        writer.writeTextElement(QStringLiteral("c:f"), ser.valRange);
        writer.writeEndElement(); // numRef
        writer.writeEndElement(); // val/yVal
        writer.writeEndElement(); // ser
    }
}

} // namespace

QByteArray chartPartXml(const ChartSpec &spec)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("c:chartSpace"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), QStringLiteral("c"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
    // AGENT-GUARD: the rich-text title uses a: elements — declare the
    // prefix or the part is namespace-invalid and every reader errors out
    // at the title (QXmlStreamReader stops with "prefix not declared").
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
    writer.writeStartElement(QStringLiteral("c:chart"));
    if (!spec.title.isEmpty()) {
        writer.writeStartElement(QStringLiteral("c:title"));
        writer.writeStartElement(QStringLiteral("c:tx"));
        writer.writeStartElement(QStringLiteral("c:rich"));
        writer.writeEmptyElement(QStringLiteral("a:bodyPr"));
        writer.writeStartElement(QStringLiteral("a:p"));
        writer.writeStartElement(QStringLiteral("a:r"));
        writer.writeTextElement(QStringLiteral("a:t"), spec.title);
        writer.writeEndElement(); // a:r
        writer.writeEndElement(); // a:p
        writer.writeEndElement(); // c:rich
        writer.writeEndElement(); // c:tx
        writer.writeEndElement(); // c:title
    }
    writer.writeStartElement(QStringLiteral("c:plotArea"));
    switch (spec.kind) {
    case ChartSpec::Kind::Bar:
    case ChartSpec::Kind::StackedBar:
        writer.writeStartElement(QStringLiteral("c:barChart"));
        writer.writeEmptyElement(QStringLiteral("c:barDir"));
        writer.writeAttribute(QStringLiteral("val"), QStringLiteral("col"));
        writer.writeEmptyElement(QStringLiteral("c:grouping"));
        writer.writeAttribute(QStringLiteral("val"), spec.kind == ChartSpec::Kind::StackedBar
                                           ? QStringLiteral("stacked")
                                           : QStringLiteral("clustered"));
        writeSeries(writer, spec);
        writeAxisIds(writer);
        writer.writeEndElement(); // c:barChart
        break;
    case ChartSpec::Kind::Line:
        writer.writeStartElement(QStringLiteral("c:lineChart"));
        writer.writeEmptyElement(QStringLiteral("c:grouping"));
        writer.writeAttribute(QStringLiteral("val"), QStringLiteral("standard"));
        writeSeries(writer, spec);
        writeAxisIds(writer);
        writer.writeEndElement(); // c:lineChart
        break;
    case ChartSpec::Kind::Area:
        writer.writeStartElement(QStringLiteral("c:areaChart"));
        writer.writeEmptyElement(QStringLiteral("c:grouping"));
        writer.writeAttribute(QStringLiteral("val"), QStringLiteral("standard"));
        writeSeries(writer, spec);
        writeAxisIds(writer);
        writer.writeEndElement(); // c:areaChart
        break;
    case ChartSpec::Kind::Pie:
        writer.writeStartElement(QStringLiteral("c:pieChart"));
        writer.writeEmptyElement(QStringLiteral("c:varyColors"));
        writer.writeAttribute(QStringLiteral("val"), QStringLiteral("1"));
        writeSeries(writer, spec);
        writer.writeEmptyElement(QStringLiteral("c:firstSliceAng"));
        writer.writeAttribute(QStringLiteral("val"), QStringLiteral("0"));
        writer.writeEndElement(); // c:pieChart
        break;
    case ChartSpec::Kind::Scatter:
        writer.writeStartElement(QStringLiteral("c:scatterChart"));
        writer.writeEmptyElement(QStringLiteral("c:scatterStyle"));
        writer.writeAttribute(QStringLiteral("val"), QStringLiteral("marker"));
        writeSeries(writer, spec);
        writeAxisIds(writer);
        writer.writeEndElement(); // c:scatterChart
        break;
    }
    if (spec.kind != ChartSpec::Kind::Pie) {
        writeAxes(writer, spec.kind == ChartSpec::Kind::Scatter);
    }
    writer.writeEndElement(); // c:plotArea
    writer.writeEndElement(); // c:chart
    writer.writeEndElement(); // c:chartSpace
    writer.writeEndDocument();
    return xml;
}

QByteArray graphicFrameAnchorXml(const QString &rid, int cNvPrId,
                                 int fromCol, int fromRow, int toCol, int toRow)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("xdr:twoCellAnchor"));
    const auto cell = [&writer](const QString &tag, int col, int row) {
        writer.writeStartElement(tag);
        writer.writeTextElement(QStringLiteral("xdr:col"), QString::number(col));
        writer.writeTextElement(QStringLiteral("xdr:colOff"), QStringLiteral("0"));
        writer.writeTextElement(QStringLiteral("xdr:row"), QString::number(row));
        writer.writeTextElement(QStringLiteral("xdr:rowOff"), QStringLiteral("0"));
        writer.writeEndElement();
    };
    cell(QStringLiteral("xdr:from"), fromCol, fromRow);
    cell(QStringLiteral("xdr:to"), toCol, toRow);
    writer.writeStartElement(QStringLiteral("xdr:graphicFrame"));
    writer.writeAttribute(QStringLiteral("macro"), QString());
    writer.writeStartElement(QStringLiteral("xdr:nvGraphicFramePr"));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvPr"));
    writer.writeAttribute(QStringLiteral("id"), QString::number(cNvPrId));
    writer.writeAttribute(QStringLiteral("name"), QStringLiteral("Chart %1").arg(cNvPrId));
    writer.writeEmptyElement(QStringLiteral("xdr:cNvGraphicFramePr"));
    writer.writeEndElement(); // nvGraphicFramePr
    writer.writeStartElement(QStringLiteral("xdr:xfrm"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"),
                          QStringLiteral("a"));
    writer.writeEmptyElement(QStringLiteral("a:off"));
    writer.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
    writer.writeEmptyElement(QStringLiteral("a:ext"));
    writer.writeAttribute(QStringLiteral("cx"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("cy"), QStringLiteral("0"));
    writer.writeEndElement(); // xdr:xfrm
    writer.writeStartElement(QStringLiteral("a:graphic"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"),
                          QStringLiteral("a"));
    writer.writeStartElement(QStringLiteral("a:graphicData"));
    writer.writeAttribute(QStringLiteral("uri"),
                          QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"));
    writer.writeEmptyElement(QStringLiteral("c:chart"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"),
                          QStringLiteral("c"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
                          QStringLiteral("r"));
    writer.writeAttribute(QStringLiteral("r:id"), rid);
    writer.writeEndElement(); // a:graphicData
    writer.writeEndElement(); // a:graphic
    writer.writeEndElement(); // graphicFrame
    writer.writeEmptyElement(QStringLiteral("xdr:clientData"));
    writer.writeEndElement(); // twoCellAnchor
    return xml;
}

} // namespace QindaOffice::Calc::IO
