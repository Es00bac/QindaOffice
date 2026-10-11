// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel-shaped PivotTables for the M74 gates (D-083), built by hand the way
// Excel 365 writes them: the cache (shared items for axis and text fields,
// inline dates and numbers otherwise, an x14 pivotCacheId), the records,
// the pivotTableDefinition with its items, rowItems / colItems, location
// and style, and the rendered cells — six sales records summarized
// (Region, Product, Date, Sales, Qty; tests/calc_engine/pivot_fixture.h
// works the numbers). Nothing comes from the owner's documents.

#include "carry_fixtures.h"

namespace QindaOffice::Test {

namespace PivotXml {

inline const QByteArray kX14 = QByteArrayLiteral("http://schemas.microsoft.com/office/spreadsheetml/2009/9/main");

// The shared strings every sales book uses, in this order.
inline QList<QByteArray> strings()
{
    QList<QByteArray> out;
    for (const char *s : {"Region", "Product", "Date", "Sales", "Qty", "East", "West", "North", "Apple", "Pear",
                          "Sum of Sales", "Column Labels", "Row Labels", "Grand Total", "Sum of Qty", "East Total",
                          "North Total", "West Total", "(All)", "2024", "2025", "Jan", "Feb", "Apr", "Jul", "Nov", "Mar"}) {
        out.append(QByteArrayLiteral("<t>") + s + QByteArrayLiteral("</t>"));
    }
    return out;
}

inline int sst(const char *text)
{
    const QList<QByteArray> all = strings();
    return int(all.indexOf(QByteArrayLiteral("<t>") + text + QByteArrayLiteral("</t>")));
}

// One row of cells: "s:East" a shared string, "n:10" a number, "d:45296"
// a date (xf 1), "" nothing.
inline QByteArray row(int r, const QStringList &cells)
{
    QByteArray xml = QByteArrayLiteral("<row r=\"") + QByteArray::number(r) + "\">";
    for (int c = 0; c < cells.size(); ++c) {
        const QString &cell = cells.at(c);
        if (cell.isEmpty()) {
            continue;
        }
        const QByteArray ref = QByteArray(1, char('A' + c)) + QByteArray::number(r);
        const QByteArray value = cell.mid(2).toUtf8();
        if (cell.startsWith(QLatin1String("s:"))) {
            xml += "<c r=\"" + ref + "\" t=\"s\"><v>" + QByteArray::number(sst(value.constData())) + "</v></c>";
        } else if (cell.startsWith(QLatin1String("d:"))) {
            xml += "<c r=\"" + ref + "\" s=\"1\"><v>" + value + "</v></c>";
        } else {
            xml += "<c r=\"" + ref + "\"><v>" + value + "</v></c>";
        }
    }
    return xml + "</row>";
}

inline ExcelSheet dataSheet()
{
    ExcelSheet data;
    data.name = QStringLiteral("Data");
    data.sheetData = row(1, {"s:Region", "s:Product", "s:Date", "s:Sales", "s:Qty"})
        + row(2, {"s:East", "s:Apple", "d:45296", "n:10", "n:1"}) + row(3, {"s:West", "s:Pear", "d:45332", "n:20", "n:2"})
        + row(4, {"s:East", "s:Pear", "d:45402", "n:30", "n:3"}) + row(5, {"s:North", "s:Apple", "d:45474", "n:5", "n:4"})
        + row(6, {"s:West", "s:Apple", "d:45611", "n:7", "n:5"}) + row(7, {"s:East", "s:Apple", "d:45719", "n:8", "n:6"});
    return data;
}

inline const char *kDates[] = {"2024-01-05T00:00:00", "2024-02-10T00:00:00", "2024-04-20T00:00:00",
                               "2024-07-01T00:00:00", "2024-11-15T00:00:00", "2025-03-03T00:00:00"};

// The cache: Region and Product listed; Date listed when `datesListed`
// (a page or row field), inline otherwise; Sales and Qty inline.
// `extraFields` follow the five source columns (made by grouping).
inline QByteArray cacheXml(const QByteArray &source, bool datesListed, const QByteArray &dateGroup = {},
                           const QByteArray &extraFields = {}, int extraCount = 0)
{
    QByteArray dates = QByteArrayLiteral("<sharedItems containsSemiMixedTypes=\"0\" containsNonDate=\"0\" containsDate=\"1\""
                                         " containsString=\"0\" minDate=\"2024-01-05T00:00:00\" maxDate=\"2025-03-03T00:00:00\"");
    if (datesListed) {
        dates += " count=\"6\">";
        for (const char *d : kDates) {
            dates += QByteArrayLiteral("<d v=\"") + d + "\"/>";
        }
        dates += "</sharedItems>";
    } else {
        dates += "/>";
    }
    return xmlHead() + QByteArrayLiteral("<pivotCacheDefinition ") + smlRoot()
        + QByteArrayLiteral(" r:id=\"rId1\" refreshedBy=\"Owner\" refreshedDate=\"45567.5\" createdVersion=\"8\""
                            " refreshedVersion=\"8\" minRefreshableVersion=\"3\" recordCount=\"6\"><cacheSource type=\"worksheet\">")
        + source + "</cacheSource><cacheFields count=\"" + QByteArray::number(5 + extraCount) + "\">"
        + "<cacheField name=\"Region\" numFmtId=\"0\"><sharedItems count=\"3\"><s v=\"East\"/><s v=\"West\"/><s v=\"North\"/>"
          "</sharedItems></cacheField><cacheField name=\"Product\" numFmtId=\"0\"><sharedItems count=\"2\"><s v=\"Apple\"/>"
          "<s v=\"Pear\"/></sharedItems></cacheField><cacheField name=\"Date\" numFmtId=\"14\">"
        + dates + dateGroup
        + "</cacheField><cacheField name=\"Sales\" numFmtId=\"0\"><sharedItems containsSemiMixedTypes=\"0\" containsString=\"0\""
          " containsNumber=\"1\" containsInteger=\"1\" minValue=\"5\" maxValue=\"30\"/></cacheField><cacheField name=\"Qty\""
          " numFmtId=\"0\"><sharedItems containsSemiMixedTypes=\"0\" containsString=\"0\" containsNumber=\"1\" containsInteger=\"1\""
          " minValue=\"1\" maxValue=\"6\"/></cacheField>"
        + extraFields + "</cacheFields><extLst><ext uri=\"{725AE2AE-9491-48be-B2B4-4EB974FC3084}\" xmlns:x14=\"" + kX14
        + "\"><x14:pivotCacheDefinition pivotCacheId=\"1\"/></ext></extLst></pivotCacheDefinition>";
}

inline QByteArray recordsXml(bool datesListed)
{
    const int region[] = {0, 1, 0, 2, 1, 0};
    const int product[] = {0, 1, 1, 0, 0, 0};
    const int sales[] = {10, 20, 30, 5, 7, 8};
    QByteArray xml = xmlHead() + QByteArrayLiteral("<pivotCacheRecords ") + smlRoot() + " count=\"6\">";
    for (int r = 0; r < 6; ++r) {
        xml += "<r><x v=\"" + QByteArray::number(region[r]) + "\"/><x v=\"" + QByteArray::number(product[r]) + "\"/>"
            + (datesListed ? "<x v=\"" + QByteArray::number(r) + "\"/>" : QByteArrayLiteral("<d v=\"") + kDates[r] + "\"/>")
            + "<n v=\"" + QByteArray::number(sales[r]) + "\"/><n v=\"" + QByteArray::number(r + 1) + "\"/></r>";
    }
    return xml + "</pivotCacheRecords>";
}

inline QByteArray tableHead(const QByteArray &attributes)
{
    return xmlHead() + QByteArrayLiteral("<pivotTableDefinition xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
                                         " name=\"PivotTable1\" cacheId=\"1\" applyNumberFormats=\"0\" applyBorderFormats=\"0\""
                                         " applyFontFormats=\"0\" applyPatternFormats=\"0\" applyAlignmentFormats=\"0\""
                                         " applyWidthHeightFormats=\"1\" dataCaption=\"Values\" updatedVersion=\"8\""
                                         " minRefreshableVersion=\"3\" useAutoFormatting=\"1\" itemPrintTitles=\"1\""
                                         " createdVersion=\"8\" indent=\"0\"")
        + attributes + QByteArrayLiteral(" multipleFieldFilters=\"0\">");
}

inline QByteArray tableTail()
{
    return QByteArrayLiteral("<pivotTableStyleInfo name=\"PivotStyleLight16\" showRowHeaders=\"1\" showColHeaders=\"1\""
                             " showRowStripes=\"0\" showColStripes=\"0\" showLastColumn=\"1\"/><extLst>"
                             "<ext uri=\"{962EF5D1-5CA2-4c93-8EF4-DBF5C05439D2}\" xmlns:x14=\"")
        + kX14 + QByteArrayLiteral("\"><x14:pivotTableDefinition hideValuesRow=\"1\"/></ext></extLst></pivotTableDefinition>");
}

// A book with the Data sheet, a Pivot sheet holding `cells` and the three parts.
inline ExcelWorkbook book(const QByteArray &cells, const QByteArray &table, const QByteArray &cache,
                          const QByteArray &records)
{
    ExcelWorkbook wb;
    wb.sharedStrings = strings();
    ExcelSheet pivot;
    pivot.name = QStringLiteral("Pivot");
    pivot.sheetData = cells;
    pivot.rels = {{QStringLiteral("rId1"), officeRel("pivotTable"), QStringLiteral("xl/pivotTables/pivotTable1.xml")}};
    wb.parts.append({QStringLiteral("xl/pivotTables/pivotTable1.xml"), table, smlType("pivotTable+xml"),
                     {{QStringLiteral("rId1"), officeRel("pivotCacheDefinition"),
                       QStringLiteral("xl/pivotCache/pivotCacheDefinition1.xml")}}});
    wb.parts.append({QStringLiteral("xl/pivotCache/pivotCacheDefinition1.xml"), cache, smlType("pivotCacheDefinition+xml"),
                     {{QStringLiteral("rId1"), officeRel("pivotCacheRecords"),
                       QStringLiteral("xl/pivotCache/pivotCacheRecords1.xml")}}});
    wb.parts.append({QStringLiteral("xl/pivotCache/pivotCacheRecords1.xml"), records, smlType("pivotCacheRecords+xml"), {}});
    wb.workbookRels.append({QStringLiteral("rIdPivot1"), officeRel("pivotCacheDefinition"),
                            QStringLiteral("xl/pivotCache/pivotCacheDefinition1.xml")});
    wb.afterCalcPr = QByteArrayLiteral("<pivotCaches><pivotCache cacheId=\"1\" r:id=\"rIdPivot1\"/></pivotCaches>");
    wb.sheets = {dataSheet(), pivot};
    return wb;
}

} // namespace PivotXml

// A: compact, Region on Rows, Product on Columns, Sum of Sales (A3:D8).
inline ExcelWorkbook regionsByProductBook(const QByteArray &source = QByteArrayLiteral(
                                              "<worksheetSource ref=\"A1:E7\" sheet=\"Data\"/>"))
{
    using namespace PivotXml;
    const QByteArray cells = row(3, {"s:Sum of Sales", "s:Column Labels"})
        + row(4, {"s:Row Labels", "s:Apple", "s:Pear", "s:Grand Total"}) + row(5, {"s:East", "n:18", "n:30", "n:48"})
        + row(6, {"s:North", "n:5", "", "n:5"}) + row(7, {"s:West", "n:7", "n:20", "n:27"})
        + row(8, {"s:Grand Total", "n:30", "n:50", "n:80"});
    const QByteArray table = tableHead(QByteArrayLiteral(" outline=\"1\" outlineData=\"1\""))
        + QByteArrayLiteral("<location ref=\"A3:D8\" firstHeaderRow=\"1\" firstDataRow=\"2\" firstDataCol=\"1\"/>"
                            "<pivotFields count=\"5\"><pivotField axis=\"axisRow\" showAll=\"0\"><items count=\"4\"><item x=\"0\"/>"
                            "<item x=\"2\"/><item x=\"1\"/><item t=\"default\"/></items></pivotField><pivotField axis=\"axisCol\""
                            " showAll=\"0\"><items count=\"3\"><item x=\"0\"/><item x=\"1\"/><item t=\"default\"/></items>"
                            "</pivotField><pivotField numFmtId=\"14\" showAll=\"0\"/><pivotField dataField=\"1\" showAll=\"0\"/>"
                            "<pivotField showAll=\"0\"/></pivotFields><rowFields count=\"1\"><field x=\"0\"/></rowFields>"
                            "<rowItems count=\"4\"><i><x/></i><i><x v=\"1\"/></i><i><x v=\"2\"/></i><i t=\"grand\"><x/></i>"
                            "</rowItems><colFields count=\"1\"><field x=\"1\"/></colFields><colItems count=\"3\"><i><x/></i>"
                            "<i><x v=\"1\"/></i><i t=\"grand\"><x/></i></colItems><dataFields count=\"1\"><dataField"
                            " name=\"Sum of Sales\" fld=\"3\" baseField=\"0\" baseItem=\"0\"/></dataFields>")
        + tableTail();
    return book(cells, table, cacheXml(source, false), recordsXml(false));
}

// B: tabular, Region and Product on Rows, Sum of Sales and Sum of Qty
// (Σ Values on Columns), Date on Filters (All) — A1:B1 and A3:D12.
inline ExcelWorkbook tabularBook()
{
    using namespace PivotXml;
    const QByteArray cells = row(1, {"s:Date", "s:(All)"}) + row(3, {"s:Region", "s:Product", "s:Sum of Sales", "s:Sum of Qty"})
        + row(4, {"s:East", "s:Apple", "n:18", "n:7"}) + row(5, {"", "s:Pear", "n:30", "n:3"})
        + row(6, {"s:East Total", "", "n:48", "n:10"}) + row(7, {"s:North", "s:Apple", "n:5", "n:4"})
        + row(8, {"s:North Total", "", "n:5", "n:4"}) + row(9, {"s:West", "s:Apple", "n:7", "n:5"})
        + row(10, {"", "s:Pear", "n:20", "n:2"}) + row(11, {"s:West Total", "", "n:27", "n:7"})
        + row(12, {"s:Grand Total", "", "n:80", "n:21"});
    QByteArray dateItems = QByteArrayLiteral("<items count=\"7\">");
    for (int d = 0; d < 6; ++d) {
        dateItems += "<item x=\"" + QByteArray::number(d) + "\"/>";
    }
    dateItems += "<item t=\"default\"/></items>";
    const QByteArray table = tableHead(QByteArrayLiteral(" compact=\"0\" compactData=\"0\""))
        + QByteArrayLiteral("<location ref=\"A3:D12\" firstHeaderRow=\"0\" firstDataRow=\"1\" firstDataCol=\"2\""
                            " rowPageCount=\"1\" colPageCount=\"1\"/><pivotFields count=\"5\"><pivotField axis=\"axisRow\""
                            " compact=\"0\" outline=\"0\" showAll=\"0\"><items count=\"4\"><item x=\"0\"/><item x=\"2\"/>"
                            "<item x=\"1\"/><item t=\"default\"/></items></pivotField><pivotField axis=\"axisRow\" compact=\"0\""
                            " outline=\"0\" showAll=\"0\"><items count=\"3\"><item x=\"0\"/><item x=\"1\"/><item t=\"default\"/>"
                            "</items></pivotField><pivotField axis=\"axisPage\" compact=\"0\" numFmtId=\"14\" outline=\"0\""
                            " showAll=\"0\">")
        + dateItems
        + QByteArrayLiteral("</pivotField><pivotField dataField=\"1\" compact=\"0\" outline=\"0\" showAll=\"0\"/>"
                            "<pivotField dataField=\"1\" compact=\"0\" outline=\"0\" showAll=\"0\"/></pivotFields>"
                            "<rowFields count=\"2\"><field x=\"0\"/><field x=\"1\"/></rowFields><rowItems count=\"9\">"
                            "<i><x/><x/></i><i r=\"1\"><x v=\"1\"/></i><i t=\"default\"><x/></i><i><x v=\"1\"/><x/></i>"
                            "<i t=\"default\"><x v=\"1\"/></i><i><x v=\"2\"/><x/></i><i r=\"1\"><x v=\"1\"/></i>"
                            "<i t=\"default\"><x v=\"2\"/></i><i t=\"grand\"><x/></i></rowItems><colFields count=\"1\">"
                            "<field x=\"-2\"/></colFields><colItems count=\"2\"><i><x/></i><i i=\"1\"><x v=\"1\"/></i></colItems>"
                            "<pageFields count=\"1\"><pageField fld=\"2\" hier=\"-1\"/></pageFields><dataFields count=\"2\">"
                            "<dataField name=\"Sum of Sales\" fld=\"3\" baseField=\"0\" baseItem=\"0\"/><dataField"
                            " name=\"Sum of Qty\" fld=\"4\" baseField=\"0\" baseItem=\"0\"/></dataFields>")
        + tableTail();
    return book(cells, table, cacheXml(QByteArrayLiteral("<worksheetSource ref=\"A1:E7\" sheet=\"Data\"/>"), true),
                recordsXml(true));
}

// C: the Date field grouped by Months (in place) and Years (a made field),
// compact, Years and Date on Rows, Sum of Sales (A3:B12).
inline ExcelWorkbook groupedDatesBook()
{
    using namespace PivotXml;
    const QByteArray cells = row(3, {"s:Row Labels", "s:Sum of Sales"}) + row(4, {"s:2024", "n:72"})
        + row(5, {"s:Jan", "n:10"}) + row(6, {"s:Feb", "n:20"}) + row(7, {"s:Apr", "n:30"}) + row(8, {"s:Jul", "n:5"})
        + row(9, {"s:Nov", "n:7"}) + row(10, {"s:2025", "n:8"}) + row(11, {"s:Mar", "n:8"})
        + row(12, {"s:Grand Total", "n:80"});
    QByteArray months = QByteArrayLiteral("<fieldGroup par=\"5\" base=\"2\"><rangePr groupBy=\"months\""
                                          " startDate=\"2024-01-05T00:00:00\" endDate=\"2025-03-04T00:00:00\"/>"
                                          "<groupItems count=\"14\"><s v=\"&lt;1/5/2024\"/>");
    for (const char *m : {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}) {
        months += QByteArrayLiteral("<s v=\"") + m + "\"/>";
    }
    months += "<s v=\"&gt;3/4/2025\"/></groupItems></fieldGroup>";
    const QByteArray years = QByteArrayLiteral(
        "<cacheField name=\"Years\" numFmtId=\"0\" databaseField=\"0\"><fieldGroup base=\"2\"><rangePr groupBy=\"years\""
        " startDate=\"2024-01-05T00:00:00\" endDate=\"2025-03-04T00:00:00\"/><groupItems count=\"4\"><s v=\"&lt;1/5/2024\"/>"
        "<s v=\"2024\"/><s v=\"2025\"/><s v=\"&gt;3/4/2025\"/></groupItems></fieldGroup></cacheField>");
    QByteArray dateItems = QByteArrayLiteral("<items count=\"15\">");
    for (int m = 0; m < 14; ++m) {
        dateItems += "<item x=\"" + QByteArray::number(m) + "\"/>";
    }
    dateItems += "<item t=\"default\"/></items>";
    const QByteArray table = tableHead(QByteArrayLiteral(" outline=\"1\" outlineData=\"1\""))
        + QByteArrayLiteral("<location ref=\"A3:B12\" firstHeaderRow=\"1\" firstDataRow=\"1\" firstDataCol=\"1\"/>"
                            "<pivotFields count=\"6\"><pivotField showAll=\"0\"/><pivotField showAll=\"0\"/>"
                            "<pivotField axis=\"axisRow\" numFmtId=\"14\" showAll=\"0\">")
        + dateItems
        + QByteArrayLiteral("</pivotField><pivotField dataField=\"1\" showAll=\"0\"/><pivotField showAll=\"0\"/>"
                            "<pivotField axis=\"axisRow\" showAll=\"0\"><items count=\"5\"><item x=\"0\"/><item x=\"1\"/>"
                            "<item x=\"2\"/><item x=\"3\"/><item t=\"default\"/></items></pivotField></pivotFields>"
                            "<rowFields count=\"2\"><field x=\"5\"/><field x=\"2\"/></rowFields><rowItems count=\"9\">"
                            "<i><x v=\"1\"/></i><i r=\"1\"><x v=\"1\"/></i><i r=\"1\"><x v=\"2\"/></i><i r=\"1\"><x v=\"4\"/></i>"
                            "<i r=\"1\"><x v=\"7\"/></i><i r=\"1\"><x v=\"11\"/></i><i><x v=\"2\"/></i><i r=\"1\"><x v=\"3\"/></i>"
                            "<i t=\"grand\"><x/></i></rowItems><colItems count=\"1\"><i/></colItems><dataFields count=\"1\">"
                            "<dataField name=\"Sum of Sales\" fld=\"3\" baseField=\"0\" baseItem=\"0\"/></dataFields>")
        + tableTail();
    return book(cells, table,
                cacheXml(QByteArrayLiteral("<worksheetSource ref=\"A1:E7\" sheet=\"Data\"/>"), true, months, years, 1),
                recordsXml(true));
}

// D: A's pivot over the Excel table "Sales" that covers the Data sheet.
inline ExcelWorkbook tableSourceBook()
{
    ExcelWorkbook wb = regionsByProductBook(QByteArrayLiteral("<worksheetSource name=\"Sales\"/>"));
    ExcelSheet &data = wb.sheets[0];
    data.afterData = QByteArrayLiteral("<tableParts count=\"1\"><tablePart r:id=\"rId1\"/></tableParts>");
    data.rels = {{QStringLiteral("rId1"), officeRel("table"), QStringLiteral("xl/tables/table1.xml")}};
    QByteArray columns;
    int id = 0;
    for (const char *name : {"Region", "Product", "Date", "Sales", "Qty"}) {
        columns += "<tableColumn id=\"" + QByteArray::number(++id) + "\" name=\"" + name + "\"/>";
    }
    wb.parts.append({QStringLiteral("xl/tables/table1.xml"),
                     xmlHead() + QByteArrayLiteral("<table ") + smlRoot()
                         + QByteArrayLiteral(" id=\"1\" name=\"Sales\" displayName=\"Sales\" ref=\"A1:E7\" totalsRowShown=\"0\">"
                                             "<autoFilter ref=\"A1:E7\"/><tableColumns count=\"5\">")
                         + columns
                         + QByteArrayLiteral("</tableColumns><tableStyleInfo name=\"TableStyleMedium2\" showFirstColumn=\"0\""
                                             " showLastColumn=\"0\" showRowStripes=\"1\" showColumnStripes=\"0\"/></table>"),
                     smlType("table+xml"),
                     {}});
    return wb;
}

// E: a calculated field (Margin = Sales*0.1) — kept as read, read-only.
inline ExcelWorkbook calculatedFieldBook(bool supported = true)
{
    ExcelWorkbook wb = regionsByProductBook();
    for (ExcelPart &part : wb.parts) {
        if (part.name.endsWith(QLatin1String("pivotCacheDefinition1.xml"))) {
            part.data.replace("<cacheFields count=\"5\">", "<cacheFields count=\"6\">");
            part.data.replace("</cacheFields>", "<cacheField name=\"Margin\" numFmtId=\"0\" formula=\"Sales*0.1\""
                                                " databaseField=\"0\"/></cacheFields>");
            if (!supported) part.data.replace("Sales*0.1", "UNSUPPORTED(Sales)");
        } else if (part.name.endsWith(QLatin1String("pivotTable1.xml"))) {
            part.data.replace("<pivotFields count=\"5\">", "<pivotFields count=\"6\">");
            part.data.replace("</pivotFields>", "<pivotField showAll=\"0\"/></pivotFields>");
        }
    }
    return wb;
}

// F: A's pivot with a PivotChart beside it (c:pivotSource names
// PivotTable1 on Pivot; its series read the pivot's cells), as Excel 365
// writes one: a twoCellAnchor in the sheet's drawing, the chart part.
inline ExcelWorkbook pivotChartBook()
{
    ExcelWorkbook wb = regionsByProductBook();
    ExcelSheet &pivot = wb.sheets[1];
    pivot.rels.append({QStringLiteral("rId2"), officeRel("drawing"), QStringLiteral("xl/drawings/drawing1.xml")});
    pivot.afterData = QByteArrayLiteral("<drawing r:id=\"rId2\"/>");
    const QByteArray drawing = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\""
        " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><xdr:twoCellAnchor>"
        "<xdr:from><xdr:col>5</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>2</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>"
        "<xdr:to><xdr:col>12</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>17</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>"
        "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"2\" name=\"Chart 1\"/>"
        "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/>"
        "</xdr:xfrm><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
        "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rId1\"/>"
        "</a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor></xdr:wsDr>");
    const auto series = [](int index, const char *name, const char *cells, const char *values) {
        return QByteArrayLiteral("<c:ser><c:idx val=\"") + QByteArray::number(index) + "\"/><c:order val=\""
            + QByteArray::number(index) + "\"/><c:tx><c:strRef><c:f>" + name
            + "</c:f></c:strRef></c:tx><c:cat><c:strRef><c:f>Pivot!$A$5:$A$7</c:f></c:strRef></c:cat><c:val><c:numRef>"
              "<c:f>" + cells + "</c:f><c:numCache><c:formatCode>General</c:formatCode>" + values
            + "</c:numCache></c:numRef></c:val></c:ser>";
    };
    const QByteArray chart = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
        " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<c:roundedCorners val=\"0\"/><c:pivotSource><c:name>[regions.xlsx]Pivot!PivotTable1</c:name>"
        "<c:fmtId val=\"0\"/></c:pivotSource><c:chart><c:autoTitleDeleted val=\"1\"/><c:pivotFmts><c:pivotFmt>"
        "<c:idx val=\"0\"/></c:pivotFmt><c:pivotFmt><c:idx val=\"1\"/></c:pivotFmt></c:pivotFmts><c:plotArea>"
        "<c:layout/><c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>")
        + series(0, "Pivot!$B$3:$B$4", "Pivot!$B$5:$B$7",
                 "<c:ptCount val=\"3\"/><c:pt idx=\"0\"><c:v>18</c:v></c:pt><c:pt idx=\"1\"><c:v>5</c:v></c:pt>"
                 "<c:pt idx=\"2\"><c:v>7</c:v></c:pt>")
        + series(1, "Pivot!$C$3:$C$4", "Pivot!$C$5:$C$7",
                 "<c:ptCount val=\"3\"/><c:pt idx=\"0\"><c:v>30</c:v></c:pt><c:pt idx=\"2\"><c:v>20</c:v></c:pt>")
        + QByteArrayLiteral(
            "<c:axId val=\"1\"/><c:axId val=\"2\"/></c:barChart><c:catAx><c:axId val=\"1\"/><c:scaling>"
            "<c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"b\"/><c:crossAx val=\"2\"/>"
            "</c:catAx><c:valAx><c:axId val=\"2\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
            "<c:delete val=\"0\"/><c:axPos val=\"l\"/><c:crossAx val=\"1\"/></c:valAx></c:plotArea>"
            "<c:legend><c:legendPos val=\"r\"/><c:overlay val=\"0\"/></c:legend><c:plotVisOnly val=\"1\"/></c:chart>"
            "</c:chartSpace>");
    wb.parts.append({QStringLiteral("xl/drawings/drawing1.xml"), drawing,
                     QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"),
                     {{QStringLiteral("rId1"), officeRel("chart"), QStringLiteral("xl/charts/chart1.xml")}}});
    wb.parts.append({QStringLiteral("xl/charts/chart1.xml"), chart,
                     QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"), {}});
    return wb;
}

} // namespace QindaOffice::Test
