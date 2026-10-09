// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M82 (D-091): chart parts as PowerPoint writes them past M67's limits, for
// the show_model and QindaShow tests — a combo chart (clustered columns:
// North with a red second point and data labels, one of them its own, and
// South; a line: Total; shared axes) over two-level categories (East ▸ Q1,
// Q2; West ▸ Q1, Q2), and a pie whose slices have their own colours and
// best-fit percentage labels — placed in the fixture deck of
// pptx_fixture_slides.h (slide 4's chart part), with or without the
// workbook PowerPoint embeds. Built in code: no binary file enters the
// repository.

#include "pptx_fixture_slides.h"

#include "ooxml_package.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace PptxComboFixture {

inline const QString kChartPart = QStringLiteral("ppt/charts/chart1.xml");
inline const QString kRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");

inline QByteArray strRef(const char *formula, const char *text)
{
    return QByteArrayLiteral("<c:strRef><c:f>") + formula + QByteArrayLiteral("</c:f><c:strCache><c:ptCount val=\"1\"/>")
        + QByteArrayLiteral("<c:pt idx=\"0\"><c:v>") + text + QByteArrayLiteral("</c:v></c:pt></c:strCache></c:strRef>");
}

inline QByteArray numRef(const char *formula, const QList<int> &values)
{
    QByteArray out = QByteArrayLiteral("<c:numRef><c:f>") + formula
        + QByteArrayLiteral("</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val=\"")
        + QByteArray::number(values.size()) + QByteArrayLiteral("\"/>");
    for (int i = 0; i < values.size(); ++i) {
        out += "<c:pt idx=\"" + QByteArray::number(i) + "\"><c:v>" + QByteArray::number(values.at(i)) + "</c:v></c:pt>";
    }
    return out + QByteArrayLiteral("</c:numCache></c:numRef>");
}

// East ▸ Q1, Q2; West ▸ Q1, Q2 — the leaf level first, as PowerPoint caches it.
inline const QByteArray kCategories = QByteArrayLiteral(
    "<c:cat><c:multiLvlStrRef><c:f>Sheet1!$A$2:$B$5</c:f><c:multiLvlStrCache><c:ptCount val=\"4\"/>"
    "<c:lvl><c:pt idx=\"0\"><c:v>Q1</c:v></c:pt><c:pt idx=\"1\"><c:v>Q2</c:v></c:pt><c:pt idx=\"2\"><c:v>Q1</c:v></c:pt>"
    "<c:pt idx=\"3\"><c:v>Q2</c:v></c:pt></c:lvl>"
    "<c:lvl><c:pt idx=\"0\"><c:v>East</c:v></c:pt><c:pt idx=\"2\"><c:v>West</c:v></c:pt></c:lvl>"
    "</c:multiLvlStrCache></c:multiLvlStrRef></c:cat>");

inline const QByteArray kLabels = QByteArrayLiteral(
    "<c:dLbls><c:dLbl><c:idx val=\"3\"/><c:dLblPos val=\"inEnd\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"1\"/>"
    "<c:showCatName val=\"1\"/><c:showSerName val=\"0\"/><c:showPercent val=\"0\"/><c:showBubbleSize val=\"0\"/></c:dLbl>"
    "<c:dLblPos val=\"outEnd\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"1\"/><c:showCatName val=\"0\"/>"
    "<c:showSerName val=\"0\"/><c:showPercent val=\"0\"/><c:showBubbleSize val=\"0\"/></c:dLbls>");

inline QByteArray comboChart()
{
    QByteArray xml = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<c:chart><c:autoTitleDeleted val=\"1\"/><c:plotArea><c:layout/>"
        "<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>");
    xml += "<c:ser><c:idx val=\"0\"/><c:order val=\"0\"/><c:tx>" + strRef("Sheet1!$C$1", "North")
        + "</c:tx><c:spPr><a:solidFill><a:schemeClr val=\"accent1\"/></a:solidFill></c:spPr><c:invertIfNegative val=\"0\"/>"
          "<c:dPt><c:idx val=\"1\"/><c:invertIfNegative val=\"0\"/><c:bubble3D val=\"0\"/><c:spPr><a:solidFill>"
          "<a:srgbClr val=\"FF0000\"/></a:solidFill></c:spPr></c:dPt>"
        + kLabels + kCategories + "<c:val>" + numRef("Sheet1!$C$2:$C$5", {3, 5, 2, 4}) + "</c:val></c:ser>";
    xml += "<c:ser><c:idx val=\"1\"/><c:order val=\"1\"/><c:tx>" + strRef("Sheet1!$D$1", "South")
        + "</c:tx><c:spPr><a:solidFill><a:schemeClr val=\"accent2\"/></a:solidFill></c:spPr><c:invertIfNegative val=\"0\"/>"
        + kCategories + "<c:val>" + numRef("Sheet1!$D$2:$D$5", {4, 1, 6, 2}) + "</c:val></c:ser>";
    xml += "<c:gapWidth val=\"150\"/><c:axId val=\"10\"/><c:axId val=\"20\"/></c:barChart>"
           "<c:lineChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>";
    xml += "<c:ser><c:idx val=\"2\"/><c:order val=\"2\"/><c:tx>" + strRef("Sheet1!$E$1", "Total")
        + "</c:tx><c:spPr><a:ln w=\"28575\" cap=\"rnd\"><a:solidFill><a:schemeClr val=\"accent3\"/></a:solidFill><a:round/>"
          "</a:ln></c:spPr><c:marker><c:symbol val=\"none\"/></c:marker>"
        + kCategories + "<c:val>" + numRef("Sheet1!$E$2:$E$5", {7, 6, 8, 6}) + "</c:val><c:smooth val=\"0\"/></c:ser>";
    xml += "<c:marker val=\"1\"/><c:axId val=\"10\"/><c:axId val=\"20\"/></c:lineChart>"
           "<c:catAx><c:axId val=\"10\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/>"
           "<c:axPos val=\"b\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:majorTickMark val=\"none\"/>"
           "<c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"20\"/><c:crosses val=\"autoZero\"/>"
           "<c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>"
           "<c:valAx><c:axId val=\"20\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/>"
           "<c:axPos val=\"l\"/><c:majorGridlines/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
           "<c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"10\"/>"
           "<c:crosses val=\"autoZero\"/><c:crossBetween val=\"between\"/></c:valAx></c:plotArea>"
           "<c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/></c:legend><c:plotVisOnly val=\"1\"/></c:chart>"
           "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData></c:chartSpace>";
    return xml;
}

// A pie with each slice's own colour and labels at their best fit.
inline QByteArray pieChart()
{
    QByteArray xml = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<c:chart><c:autoTitleDeleted val=\"1\"/><c:plotArea><c:layout/><c:pieChart><c:varyColors val=\"1\"/>");
    xml += "<c:ser><c:idx val=\"0\"/><c:order val=\"0\"/><c:tx>" + strRef("Sheet1!$B$1", "Share") + "</c:tx>";
    const char *colors[] = {"1F77B4", "FF7F0E", "2CA02C"};
    for (int i = 0; i < 3; ++i) {
        xml += "<c:dPt><c:idx val=\"" + QByteArray::number(i) + "\"/><c:bubble3D val=\"0\"/><c:spPr><a:solidFill><a:srgbClr val=\""
            + colors[i] + "\"/></a:solidFill></c:spPr></c:dPt>";
    }
    xml += "<c:dLbls><c:dLblPos val=\"bestFit\"/><c:showLegendKey val=\"0\"/><c:showVal val=\"0\"/><c:showCatName val=\"0\"/>"
           "<c:showSerName val=\"0\"/><c:showPercent val=\"1\"/><c:showBubbleSize val=\"0\"/><c:showLeaderLines val=\"1\"/></c:dLbls>"
           "<c:cat><c:strRef><c:f>Sheet1!$A$2:$A$4</c:f><c:strCache><c:ptCount val=\"3\"/><c:pt idx=\"0\"><c:v>Rent</c:v></c:pt>"
           "<c:pt idx=\"1\"><c:v>Food</c:v></c:pt><c:pt idx=\"2\"><c:v>Travel</c:v></c:pt></c:strCache></c:strRef></c:cat>"
           "<c:val>" + numRef("Sheet1!$B$2:$B$4", {50, 30, 20}) + "</c:val></c:ser>"
           "<c:firstSliceAng val=\"0\"/></c:pieChart></c:plotArea><c:legend><c:legendPos val=\"r\"/><c:overlay val=\"0\"/>"
           "</c:legend><c:plotVisOnly val=\"1\"/></c:chart>"
           "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData></c:chartSpace>";
    return xml;
}

// The fixture deck with `chart` in slide 4's chart part and `workbook`
// embedded (none: the chart without its c:externalData).
inline QByteArray deckWith(QByteArray chart, const QByteArray &workbook)
{
    namespace Ooxml = QindaOffice::Ooxml;
    if (workbook.isEmpty()) {
        chart.replace("<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData>", "");
    }
    const auto base = Ooxml::Package::fromData(PptxFixture::build());
    Ooxml::PackageWriter w;
    const QStringList parts = base->partNames();
    for (const QString &part : parts) {
        if (part.startsWith(QLatin1Char('[')) || part.endsWith(QLatin1String(".rels"))) {
            continue;
        }
        w.addPart(part, part == kChartPart ? chart : base->data(part), base->contentType(part));
    }
    for (const QString &part : QStringList{QString()} + parts) {
        for (const Ooxml::Relationship &rel : base->relationships(part)) {
            w.addRelationshipWithId(part, rel.id, rel.type, rel.external ? rel.target : base->targetPart(part, rel), rel.external);
        }
    }
    if (!workbook.isEmpty()) {
        const QString embedding = QStringLiteral("ppt/embeddings/Microsoft_Excel_Worksheet.xlsx");
        w.addPart(embedding, workbook, QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"));
        w.addRelationshipWithId(kChartPart, QStringLiteral("rId1"), kRel + QStringLiteral("package"), embedding);
    }
    return w.toZip();
}

} // namespace PptxComboFixture
