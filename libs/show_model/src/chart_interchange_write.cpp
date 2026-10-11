// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_interchange.h"
#include "chart_workbook.h"
#include "pptx_write_shapes.h"
#include "pptx_write_chart_internal.h"
#include "ooxml_namespaces.h"
namespace QindaOffice::Show::ChartIO {
using namespace Pptx;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using ChartXml::make;
using ChartXml::valued;
namespace {
const QString kChartType = QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
const QString kXlsxType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
QString formulaOf(const XmlElement *holder)
{
    const auto *f = holder ? holder->descendant(nsC(), u"f") : nullptr;
    return f ? f->text().trimmed() : QString();
}
std::shared_ptr<const Ooxml::Package> seed()
{
    // A valid, ordinary column chart is the editing seed. The same patcher
    // handles insertion and imports, including axes and embedded workbooks.
    const QByteArray xml = R"(<c:chartSpace xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart"><c:chart><c:autoTitleDeleted val="1"/><c:plotArea><c:layout/><c:barChart><c:barDir val="col"/><c:grouping val="clustered"/><c:ser><c:idx val="0"/><c:order val="0"/><c:tx><c:v>Series 1</c:v></c:tx><c:cat><c:strRef><c:f>Sheet1!$A$2</c:f><c:strCache><c:ptCount val="1"/><c:pt idx="0"><c:v>A</c:v></c:pt></c:strCache></c:strRef></c:cat><c:val><c:numRef><c:f>Sheet1!$B$2</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val="1"/><c:pt idx="0"><c:v>1</c:v></c:pt></c:numCache></c:numRef></c:val></c:ser><c:axId val="1"/><c:axId val="2"/></c:barChart><c:catAx><c:axId val="1"/><c:scaling><c:orientation val="minMax"/></c:scaling><c:axPos val="b"/><c:crossAx val="2"/><c:crosses val="autoZero"/></c:catAx><c:valAx><c:axId val="2"/><c:scaling><c:orientation val="minMax"/></c:scaling><c:axPos val="l"/><c:crossAx val="1"/><c:crosses val="autoZero"/></c:valAx></c:plotArea><c:plotVisOnly val="1"/></c:chart></c:chartSpace>)";
    Ooxml::PackageWriter writer;
    writer.addPart(QStringLiteral("chart.xml"), xml, kChartType);
    return Ooxml::Package::fromEntries(writer.entries());
}
} // namespace
QString write(Ooxml::PackageWriter &writer,
              const std::shared_ptr<const Ooxml::Package> &originalSource,
              const QString &originalPart, const ChartData &requestedChart,
              const ChartData &oldChart, const QString &directory, QString *error)
{
    ChartData chart = requestedChart;
    if (!originalSource) {
        // A new Word document need not carry a theme part. Spell the
        // renderer's palette explicitly so other consumers draw its bars.
        const QStringList palette{QStringLiteral("#4472c4"), QStringLiteral("#ed7d31"), QStringLiteral("#a5a5a5"),
                                  QStringLiteral("#ffc000"), QStringLiteral("#5b9bd5"), QStringLiteral("#70ad47")};
        for (int i = 0; i < chart.series.size(); ++i) {
            if (!chart.series[i].color.isValid()) chart.series[i].color = QColor(palette.at(i % palette.size()));
            chart.series[i].sourceIdx = -1;
        }
    }
    const auto source = originalSource ? originalSource : seed();
    const QString sourcePart = originalSource ? originalPart : QStringLiteral("chart.xml");
    Ooxml::RelationshipCopier copier(source, &writer);
    const ChartData previous = originalSource ? oldChart : read(source, sourcePart, Ooxml::defaultOfficeTheme());

    if (!source || chart.series.isEmpty()) {
        return {};
    }
    const XmlElementPtr original = source->xml(sourcePart);
    if (!original) {
        return {};
    }
    XmlElementPtr chartSpace = original->clone();
    XmlElement *body = chartSpace->child(nsC(), u"chart");
    XmlElement *plotArea = body != nullptr ? body->child(nsC(), u"plotArea") : nullptr;
    QList<XmlElement *> plots = plotArea != nullptr ? ChartXml::plotsOf(*plotArea) : QList<XmlElement *>();
    const XmlElement *first = !plots.isEmpty() ? plots.first()->child(nsC(), u"ser") : nullptr;
    if (first == nullptr) {
        return {};
    }
    // Where the data sits, from the first series as the file has it.
    ChartXml::DataShape shape;
    const bool scatterLike = first->child(nsC(), u"xVal") != nullptr || first->child(nsC(), u"yVal") != nullptr;
    const XmlElement *sourceCat = first->child(nsC(), scatterLike ? u"xVal" : u"cat");
    const XmlElement *sourceVal = first->child(nsC(), scatterLike ? u"yVal" : u"val");
    shape.layout = ChartWorkbook::layoutFrom(formulaOf(first->child(nsC(), u"tx")), formulaOf(sourceCat), formulaOf(sourceVal));
    if (const XmlElement *numbers = sourceCat != nullptr ? sourceCat->descendant(nsC(), u"numCache") : nullptr) {
        const XmlElement *format = numbers->child(nsC(), u"formatCode");
        shape.categoryFormat = format != nullptr ? format->text() : QStringLiteral("General");
        shape.layout.categoriesNumeric = true;
    }
    if (const XmlElement *numbers = sourceVal != nullptr ? sourceVal->descendant(nsC(), u"numCache") : nullptr) {
        if (const XmlElement *format = numbers->child(nsC(), u"formatCode")) {
            shape.valueFormat = format->text();
        }
    }
    // Each plot whose kind changed is rebuilt (a combo chart's one by one).
    bool converted = false;
    for (int p = 0; p < plots.size(); ++p) {
        const ChartPlot now = chart.plotAt(p);
        if (now == previous.plotAt(p)) {
            continue;
        }
        ChartData kind;
        kind.type = now.type;
        kind.grouping = now.grouping;
        plots[p] = convertPlot(*plotArea, plots.at(p), kind);
        if (plots.at(p) == nullptr) {
            return {};
        }
        converted = true;
        if (p > 0) {
            continue;
        }
        if (plots.at(p)->name == u"scatterChart") {
            shape.categoryFormat = shape.categoryFormat.isNull() ? QStringLiteral("General") : shape.categoryFormat;
        } else if (!previous.type.isEmpty() && previous.type == u"scatter") {
            shape.categoryFormat = QString(); // labels again
            shape.layout.categoriesNumeric = false;
        }
    }
    if (converted) {
        // The kinds QindaShow draws are flat: a 3-D view has nothing left to show.
        for (const char16_t *name : {u"view3D", u"floor", u"sideWall", u"backWall"}) {
            body->removeChildren(nsC(), name);
        }
    }
    // M82: multi-level categories keep their levels where the workbook has
    // room for them (the outer ones in the columns, or rows, before the
    // series' own labels).
    const int levels = chart.type == u"scatter" ? 1 : 1 + int(chart.categoryLevels.size());
    const int room = shape.layout.seriesInRows ? shape.layout.headerRow : shape.layout.categoryColumn;
    shape.layout.levels = levels - 1 <= room ? levels : 1;
    if (!ChartXml::patchPlots(plots, chart, shape)) {
        return {};
    }

    // The new part and its relationships: the source's, but the workbook.
    const QString part = writer.uniquePartName(directory + QStringLiteral("/charts/chart"), QStringLiteral(".xml"));
    XmlElement *external = chartSpace->child(nsC(), u"externalData");
    const QString externalId = external != nullptr ? external->attribute(nsR(), u"id") : QString();
    QByteArray workbook;
    for (const Ooxml::Relationship &rel : source->relationships(sourcePart)) {
        if (rel.id == externalId && !rel.external) {
            const QString target = source->targetPart(sourcePart, rel);
            if (source->contentType(target) == kXlsxType) {
                workbook = source->data(target);
            }
            continue;
        }
        if (rel.external) {
            writer.addRelationshipWithId(part, rel.id, rel.type, rel.target, true);
        } else if (const QString target = source->targetPart(sourcePart, rel); !target.isEmpty()) {
            writer.addRelationshipWithId(part, rel.id, rel.type, copier.copyPart(target));
        }
    }
    const bool linked = !externalId.isEmpty() && [&] {
        const auto rel = source->relationship(sourcePart, externalId);
        return rel && rel->external;
    }();
    if (!linked) {
        // AGENT-NOTE: a chart linked to an outside workbook keeps its link
        // (PowerPoint refreshes from that file); an embedded one gets the
        // edited data written into its workbook, or a new workbook.
        QString workbookError;
        const QByteArray bytes = ChartWorkbook::write(workbook, chart, previous, shape.layout, &workbookError);
        if (bytes.isEmpty()) {
            if (error) *error = workbookError;
            return {};
        } else {
            const QString embedding = writer.uniquePartName(directory + QStringLiteral("/embeddings/Microsoft_Excel_Worksheet"),
                                                                    QStringLiteral(".xlsx"));
            writer.addPart(embedding, bytes, kXlsxType);
            QString id = externalId;
            if (id.isEmpty()) {
                id = writer.addRelationship(part, relType("package"), embedding);
                XmlElementPtr data = make(QStringLiteral("externalData"));
                data->setAttribute(nsR(), QStringLiteral("id"), id);
                data->appendElement(valued(QStringLiteral("autoUpdate"), QStringLiteral("0")));
                chartSpace->insertElementBefore(data, {u"printSettings", u"userShapes", u"extLst"});
            } else {
                writer.addRelationshipWithId(part, id, relType("package"), embedding);
            }
        }
    }
    writer.addPart(part, Ooxml::serializeDocument(*chartSpace), kChartType);
    return part;
}
} // namespace QindaOffice::Show::ChartIO
