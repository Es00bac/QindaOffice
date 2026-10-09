// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel-shaped workbooks with the parts QindaCalc does not model, for the
// M66 carried-parts gates (D-075, D-068 rule 2): a pivot table with its
// cache and records, a slicer and a timeline over it (their caches, the
// sheet and workbook extLst entries, and their drawing anchors), a query
// table with its connection, an external link, a data model with custom
// XML, threaded comments with their persons, a form control, printer
// settings and a VBA project. The XML is the shape Excel writes, cut down
// to what each part needs; every SpreadsheetML part validates against
// sml.xsd (the tests check the SOURCE package too). Built by hand: no
// binary fixture, and nothing from the owner's documents.

#include "excel_package.h"

namespace QindaOffice::Test {

inline QString msRel(const char *path)
{
    return QStringLiteral("http://schemas.microsoft.com/office/") + QString::fromLatin1(path);
}

inline QByteArray xmlHead()
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
}

// "xmlns=… xmlns:r=…" for a SpreadsheetML root.
inline QByteArray smlRoot()
{
    return QByteArrayLiteral("xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
}

inline QByteArray drawingRoot()
{
    return QByteArrayLiteral("<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                             "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">");
}

// A from/to pair in the drawing's own spelling (0-based cells).
inline QByteArray anchorCells(int fromCol, int fromRow, int toCol, int toRow)
{
    const auto cell = [](const char *tag, int col, int row) {
        return QByteArrayLiteral("<xdr:") + tag + QByteArrayLiteral("><xdr:col>") + QByteArray::number(col)
            + QByteArrayLiteral("</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>") + QByteArray::number(row)
            + QByteArrayLiteral("</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:") + tag + QByteArrayLiteral(">");
    };
    return cell("from", fromCol, fromRow) + cell("to", toCol, toRow);
}

// A text-box shape (what Excel's fallbacks and plain shapes are).
inline QByteArray textShape(int id, const QByteArray &text)
{
    return QByteArrayLiteral("<xdr:sp macro=\"\" textlink=\"\"><xdr:nvSpPr><xdr:cNvPr id=\"") + QByteArray::number(id)
        + QByteArrayLiteral("\" name=\"Shape ") + QByteArray::number(id)
        + QByteArrayLiteral("\"/><xdr:cNvSpPr txBox=\"1\"/></xdr:nvSpPr><xdr:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
                            "<a:ext cx=\"1828800\" cy=\"381000\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                            "</xdr:spPr><xdr:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:t>")
        + text + QByteArrayLiteral("</a:t></a:r></a:p></xdr:txBody></xdr:sp>");
}

// Data on sheet 1 (Region/Sales), a pivot table summing it on sheet 2
// (cacheId 5), and the cache with its records. `withSlicers` adds a slicer
// and a timeline over the pivot table plus a plain text box, all anchored
// in sheet 2's drawing.
inline ExcelWorkbook pivotBook(bool withSlicers)
{
    ExcelWorkbook book;
    book.sharedStrings = {QByteArrayLiteral("<t>Region</t>"), QByteArrayLiteral("<t>Sales</t>"),
                          QByteArrayLiteral("<t>East</t>"), QByteArrayLiteral("<t>West</t>"),
                          QByteArrayLiteral("<t>Row Labels</t>"), QByteArrayLiteral("<t>Sum of Sales</t>"),
                          QByteArrayLiteral("<t>Grand Total</t>")};
    ExcelSheet data;
    data.name = QStringLiteral("Data");
    data.sheetData = QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\"><v>10</v></c></row>"
        "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>3</v></c><c r=\"B3\"><v>20</v></c></row>"
        "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>2</v></c><c r=\"B4\"><v>30</v></c></row>");
    ExcelSheet pivot;
    pivot.name = QStringLiteral("Pivot");
    pivot.sheetData = QByteArrayLiteral(
        "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>4</v></c><c r=\"B3\" t=\"s\"><v>5</v></c></row>"
        "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>2</v></c><c r=\"B4\"><v>40</v></c></row>"
        "<row r=\"5\"><c r=\"A5\" t=\"s\"><v>3</v></c><c r=\"B5\"><v>20</v></c></row>"
        "<row r=\"6\"><c r=\"A6\" t=\"s\"><v>6</v></c><c r=\"B6\"><v>60</v></c></row>");
    pivot.rels = {{QStringLiteral("rId1"), officeRel("pivotTable"), QStringLiteral("xl/pivotTables/pivotTable1.xml")}};
    book.parts.append({QStringLiteral("xl/pivotTables/pivotTable1.xml"),
                       xmlHead() + QByteArrayLiteral("<pivotTableDefinition ") + smlRoot()
                           + QByteArrayLiteral(
                               " name=\"PivotTable1\" cacheId=\"5\" applyNumberFormats=\"0\" applyBorderFormats=\"0\""
                               " applyFontFormats=\"0\" applyPatternFormats=\"0\" applyAlignmentFormats=\"0\""
                               " applyWidthHeightFormats=\"1\" dataCaption=\"Values\" updatedVersion=\"8\""
                               " minRefreshableVersion=\"3\" createdVersion=\"8\" indent=\"0\" outline=\"1\" outlineData=\"1\">"
                               "<location ref=\"A3:B6\" firstHeaderRow=\"1\" firstDataRow=\"1\" firstDataCol=\"1\"/>"
                               "<pivotFields count=\"2\"><pivotField axis=\"axisRow\" showAll=\"0\"><items count=\"3\">"
                               "<item x=\"0\"/><item x=\"1\"/><item t=\"default\"/></items></pivotField>"
                               "<pivotField dataField=\"1\" showAll=\"0\"/></pivotFields>"
                               "<rowFields count=\"1\"><field x=\"0\"/></rowFields>"
                               "<rowItems count=\"3\"><i><x/></i><i><x v=\"1\"/></i><i t=\"grand\"><x/></i></rowItems>"
                               "<colItems count=\"1\"><i/></colItems>"
                               "<dataFields count=\"1\"><dataField name=\"Sum of Sales\" fld=\"1\" baseField=\"0\" baseItem=\"0\"/></dataFields>"
                               "<pivotTableStyleInfo name=\"PivotStyleLight16\" showRowHeaders=\"1\" showColHeaders=\"1\""
                               " showRowStripes=\"0\" showColStripes=\"0\" showLastColumn=\"1\"/></pivotTableDefinition>"),
                       smlType("pivotTable+xml"),
                       {{QStringLiteral("rId1"), officeRel("pivotCacheDefinition"),
                         QStringLiteral("xl/pivotCache/pivotCacheDefinition1.xml")}}});
    book.parts.append({QStringLiteral("xl/pivotCache/pivotCacheDefinition1.xml"),
                       xmlHead() + QByteArrayLiteral("<pivotCacheDefinition ") + smlRoot()
                           + QByteArrayLiteral(
                               " r:id=\"rId1\" refreshedBy=\"Owner\" refreshedDate=\"45567.5\" createdVersion=\"8\""
                               " refreshedVersion=\"8\" minRefreshableVersion=\"3\" recordCount=\"3\">"
                               "<cacheSource type=\"worksheet\"><worksheetSource ref=\"A1:B4\" sheet=\"Data\"/></cacheSource>"
                               "<cacheFields count=\"2\"><cacheField name=\"Region\" numFmtId=\"0\"><sharedItems count=\"2\">"
                               "<s v=\"East\"/><s v=\"West\"/></sharedItems></cacheField>"
                               "<cacheField name=\"Sales\" numFmtId=\"0\"><sharedItems containsSemiMixedTypes=\"0\""
                               " containsString=\"0\" containsNumber=\"1\" containsInteger=\"1\" minValue=\"10\" maxValue=\"30\"/>"
                               "</cacheField></cacheFields></pivotCacheDefinition>"),
                       smlType("pivotCacheDefinition+xml"),
                       {{QStringLiteral("rId1"), officeRel("pivotCacheRecords"),
                         QStringLiteral("xl/pivotCache/pivotCacheRecords1.xml")}}});
    book.parts.append({QStringLiteral("xl/pivotCache/pivotCacheRecords1.xml"),
                       xmlHead() + QByteArrayLiteral("<pivotCacheRecords ") + smlRoot()
                           + QByteArrayLiteral(" count=\"3\"><r><x v=\"0\"/><n v=\"10\"/></r><r><x v=\"1\"/><n v=\"20\"/></r>"
                                               "<r><x v=\"0\"/><n v=\"30\"/></r></pivotCacheRecords>"),
                       smlType("pivotCacheRecords+xml"),
                       {}});
    book.workbookRels.append({QStringLiteral("rIdPivot1"), officeRel("pivotCacheDefinition"),
                              QStringLiteral("xl/pivotCache/pivotCacheDefinition1.xml")});
    book.afterCalcPr = QByteArrayLiteral("<pivotCaches><pivotCache cacheId=\"5\" r:id=\"rIdPivot1\"/></pivotCaches>");
    if (withSlicers) {
        pivot.rels.append({QStringLiteral("rId2"), officeRel("drawing"), QStringLiteral("xl/drawings/drawing1.xml")});
        pivot.rels.append({QStringLiteral("rId3"), msRel("2007/relationships/slicer"), QStringLiteral("xl/slicers/slicer1.xml")});
        pivot.rels.append({QStringLiteral("rId4"), msRel("2011/relationships/timeline"), QStringLiteral("xl/timelines/timeline1.xml")});
        pivot.rootAttributes = QByteArrayLiteral(
            " xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\""
            " xmlns:x15=\"http://schemas.microsoft.com/office/spreadsheetml/2010/11/main\"");
        pivot.afterData = QByteArrayLiteral(
            "<drawing r:id=\"rId2\"/><extLst>"
            "<ext uri=\"{A8765BA9-456A-4dab-B4F3-ACF838C121DE}\"><x14:slicerList><x14:slicer r:id=\"rId3\"/></x14:slicerList></ext>"
            "<ext uri=\"{7E03D99C-DC04-49d9-9315-930204A7B6E9}\"><x15:timelineRefs><x15:timelineRef r:id=\"rId4\"/></x15:timelineRefs></ext>"
            "</extLst>");
        const QByteArray mc = QByteArrayLiteral("xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"");
        book.parts.append({QStringLiteral("xl/drawings/drawing1.xml"),
                           xmlHead() + drawingRoot()
                               // A slicer: its graphicFrame for Excel 2010+, a text box for older readers.
                               + QByteArrayLiteral("<mc:AlternateContent ") + mc
                               + QByteArrayLiteral("><mc:Choice xmlns:sle15=\"http://schemas.microsoft.com/office/drawing/2012/slicer\" Requires=\"sle15\">"
                                                   "<xdr:twoCellAnchor editAs=\"oneCell\">")
                               + anchorCells(3, 2, 6, 12)
                               + QByteArrayLiteral("<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"2\" name=\"Region\"/>"
                                                   "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm><a:off x=\"0\" y=\"0\"/>"
                                                   "<a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/drawing/2010/slicer\">"
                                                   "<sle:slicer xmlns:sle=\"http://schemas.microsoft.com/office/drawing/2010/slicer\" name=\"Region\"/>"
                                                   "</a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor></mc:Choice>"
                                                   "<mc:Fallback><xdr:twoCellAnchor editAs=\"oneCell\">")
                               + anchorCells(3, 2, 6, 12) + textShape(3, "This shape represents a slicer.")
                               + QByteArrayLiteral("<xdr:clientData/></xdr:twoCellAnchor></mc:Fallback></mc:AlternateContent>")
                               // A timeline, the same way.
                               + QByteArrayLiteral("<mc:AlternateContent ") + mc
                               + QByteArrayLiteral("><mc:Choice xmlns:tsle=\"http://schemas.microsoft.com/office/drawing/2012/timeslicer\" Requires=\"tsle\">"
                                                   "<xdr:twoCellAnchor editAs=\"oneCell\">")
                               + anchorCells(3, 13, 9, 20)
                               + QByteArrayLiteral("<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"4\" name=\"Date\"/>"
                                                   "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm><a:off x=\"0\" y=\"0\"/>"
                                                   "<a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"http://schemas.microsoft.com/office/drawing/2012/timeslicer\">"
                                                   "<tsle:timeslicer xmlns:tsle=\"http://schemas.microsoft.com/office/drawing/2012/timeslicer\" name=\"NativeTimeline_Date\"/>"
                                                   "</a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor></mc:Choice>"
                                                   "<mc:Fallback><xdr:twoCellAnchor editAs=\"oneCell\">")
                               + anchorCells(3, 13, 9, 20) + textShape(5, "This shape represents a timeline.")
                               + QByteArrayLiteral("<xdr:clientData/></xdr:twoCellAnchor></mc:Fallback></mc:AlternateContent>")
                               // A plain text box, no extension vocabulary at all.
                               + QByteArrayLiteral("<xdr:twoCellAnchor>") + anchorCells(0, 8, 2, 10)
                               + textShape(6, "Totals are refreshed on open")
                               + QByteArrayLiteral("<xdr:clientData/></xdr:twoCellAnchor></xdr:wsDr>"),
                           QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"),
                           {}});
        book.parts.append({QStringLiteral("xl/slicers/slicer1.xml"),
                           xmlHead() + QByteArrayLiteral(
                                           "<slicers xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
                                           "<slicer name=\"Region\" cache=\"Slicer_Region\" caption=\"Region\" rowHeight=\"241300\"/>"
                                           "</slicers>"),
                           QStringLiteral("application/vnd.ms-excel.slicer+xml"),
                           {}});
        book.parts.append({QStringLiteral("xl/slicerCaches/slicerCache1.xml"),
                           xmlHead() + QByteArrayLiteral(
                                           "<slicerCacheDefinition xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\""
                                           " name=\"Slicer_Region\" sourceName=\"Region\"><pivotTables><pivotTable tabId=\"2\" name=\"PivotTable1\"/>"
                                           "</pivotTables><data><tabular pivotCacheId=\"1\"><items count=\"2\"><i x=\"0\" s=\"1\"/>"
                                           "<i x=\"1\" s=\"1\"/></items></tabular></data></slicerCacheDefinition>"),
                           QStringLiteral("application/vnd.ms-excel.slicerCache+xml"),
                           {}});
        book.parts.append({QStringLiteral("xl/timelines/timeline1.xml"),
                           xmlHead() + QByteArrayLiteral(
                                           "<timelines xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2010/11/main\">"
                                           "<timeline name=\"NativeTimeline_Date\" cache=\"NativeTimeline_Date\" caption=\"Date\" level=\"2\""
                                           " selectionLevel=\"2\" scrollPosition=\"2024-01-01T00:00:00\"/></timelines>"),
                           QStringLiteral("application/vnd.ms-excel.timeline+xml"),
                           {}});
        book.parts.append({QStringLiteral("xl/timelineCaches/timelineCache1.xml"),
                           xmlHead() + QByteArrayLiteral(
                                           "<timelineCacheDefinition xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2010/11/main\""
                                           " name=\"NativeTimeline_Date\" sourceName=\"Date\"><pivotTables><pivotTable tabId=\"2\" name=\"PivotTable1\"/>"
                                           "</pivotTables><state minimalRefreshVersion=\"6\" lastRefreshVersion=\"6\" pivotCacheId=\"1\" filterType=\"unknown\">"
                                           "<bounds startDate=\"2024-01-01T00:00:00\" endDate=\"2025-01-01T00:00:00\"/></state>"
                                           "</timelineCacheDefinition>"),
                           QStringLiteral("application/vnd.ms-excel.timelineCache+xml"),
                           {}});
        book.workbookRels.append({QStringLiteral("rIdSlicerCache1"), msRel("2007/relationships/slicerCache"),
                                  QStringLiteral("xl/slicerCaches/slicerCache1.xml")});
        book.workbookRels.append({QStringLiteral("rIdTimelineCache1"), msRel("2011/relationships/timelineCache"),
                                  QStringLiteral("xl/timelineCaches/timelineCache1.xml")});
        book.workbookRootAttributes = QByteArrayLiteral(
            " xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\""
            " xmlns:x15=\"http://schemas.microsoft.com/office/spreadsheetml/2010/11/main\"");
        book.afterCalcPr += QByteArrayLiteral(
            "<extLst><ext uri=\"{BBE1A952-AA13-448e-AADC-164F8A28A991}\"><x14:slicerCaches>"
            "<x14:slicerCache r:id=\"rIdSlicerCache1\"/></x14:slicerCaches></ext>"
            "<ext uri=\"{D0CA8CA8-9F24-4464-BF8E-62219DCF47F9}\"><x15:timelineCacheRefs>"
            "<x15:timelineCacheRef r:id=\"rIdTimelineCache1\"/></x15:timelineCacheRefs></ext></extLst>");
    }
    book.sheets = {data, pivot};
    return book;
}

// A query table (Power Query output) with its connection, an external
// link a formula reads through ([1]Prices!A1), and the data model with its
// custom-XML mashup — the workbook-level parts an import keeps.
inline ExcelWorkbook connectionBook()
{
    ExcelWorkbook book;
    book.sharedStrings = {QByteArrayLiteral("<t>Item</t>"), QByteArrayLiteral("<t>Qty</t>"),
                          QByteArrayLiteral("<t>Bolts</t>"), QByteArrayLiteral("<t>Nuts</t>")};
    ExcelSheet query;
    query.name = QStringLiteral("Inventory");
    query.sheetData = QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c><c r=\"D1\"><f>[1]Prices!A1*2</f><v>19</v></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\"><v>12</v></c></row>"
        "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>3</v></c><c r=\"B3\"><v>40</v></c></row>");
    query.afterData = QByteArrayLiteral("<tableParts count=\"1\"><tablePart r:id=\"rId1\"/></tableParts>");
    query.rels = {{QStringLiteral("rId1"), officeRel("table"), QStringLiteral("xl/tables/table1.xml")}};
    book.parts.append({QStringLiteral("xl/tables/table1.xml"),
                       xmlHead() + QByteArrayLiteral("<table ") + smlRoot()
                           + QByteArrayLiteral(
                               " id=\"1\" name=\"Inventory\" displayName=\"Inventory\" ref=\"A1:B3\" tableType=\"queryTable\""
                               " totalsRowShown=\"0\"><autoFilter ref=\"A1:B3\"/><tableColumns count=\"2\">"
                               "<tableColumn id=\"1\" uniqueName=\"1\" name=\"Item\" queryTableFieldId=\"1\"/>"
                               "<tableColumn id=\"2\" uniqueName=\"2\" name=\"Qty\" queryTableFieldId=\"2\"/></tableColumns>"
                               "<tableStyleInfo name=\"TableStyleMedium2\" showFirstColumn=\"0\" showLastColumn=\"0\""
                               " showRowStripes=\"1\" showColumnStripes=\"0\"/></table>"),
                       smlType("table+xml"),
                       {{QStringLiteral("rId1"), officeRel("queryTable"), QStringLiteral("xl/queryTables/queryTable1.xml")}}});
    book.parts.append({QStringLiteral("xl/queryTables/queryTable1.xml"),
                       xmlHead() + QByteArrayLiteral("<queryTable ") + smlRoot()
                           + QByteArrayLiteral(
                               " name=\"Inventory\" connectionId=\"1\" autoFormatId=\"16\" applyNumberFormats=\"0\""
                               " applyBorderFormats=\"0\" applyFontFormats=\"0\" applyPatternFormats=\"0\""
                               " applyAlignmentFormats=\"0\" applyWidthHeightFormats=\"0\"><queryTableRefresh nextId=\"3\">"
                               "<queryTableFields count=\"2\"><queryTableField id=\"1\" name=\"Item\" tableColumnId=\"1\"/>"
                               "<queryTableField id=\"2\" name=\"Qty\" tableColumnId=\"2\"/></queryTableFields>"
                               "</queryTableRefresh></queryTable>"),
                       smlType("queryTable+xml"),
                       {}});
    book.parts.append({QStringLiteral("xl/connections.xml"),
                       xmlHead() + QByteArrayLiteral("<connections ") + smlRoot()
                           + QByteArrayLiteral(
                               "><connection id=\"1\" keepAlive=\"1\" name=\"Query - Inventory\" type=\"5\""
                               " refreshedVersion=\"8\" background=\"1\" saveData=\"1\"><dbPr connection=\"Provider=Microsoft.Mashup.OleDb.1;"
                               "Data Source=$Workbook$;Location=Inventory\" command=\"SELECT * FROM [Inventory]\"/></connection>"
                               "</connections>"),
                       smlType("connections+xml"),
                       {}});
    book.parts.append({QStringLiteral("xl/externalLinks/externalLink1.xml"),
                       xmlHead() + QByteArrayLiteral("<externalLink ") + smlRoot()
                           + QByteArrayLiteral(
                               "><externalBook r:id=\"rId1\"><sheetNames><sheetName val=\"Prices\"/></sheetNames>"
                               "<sheetDataSet><sheetData sheetId=\"0\"><row r=\"1\"><cell r=\"A1\"><v>9.5</v></cell></row>"
                               "</sheetData></sheetDataSet></externalBook></externalLink>"),
                       smlType("externalLink+xml"),
                       {{QStringLiteral("rId1"), officeRel("externalLinkPath"), QStringLiteral("Prices.xlsx"), true}}});
    book.parts.append({QStringLiteral("customXml/item1.xml"),
                       xmlHead() + QByteArrayLiteral("<DataMashup xmlns=\"http://schemas.microsoft.com/DataMashup\">AAAAAA==</DataMashup>"),
                       QString(),
                       {{QStringLiteral("rId1"), officeRel("customXmlProps"), QStringLiteral("customXml/itemProps1.xml")}}});
    book.parts.append({QStringLiteral("customXml/itemProps1.xml"),
                       xmlHead() + QByteArrayLiteral(
                                       "<ds:datastoreItem ds:itemID=\"{8A6E4B3C-0000-4000-8000-000000000001}\""
                                       " xmlns:ds=\"http://schemas.openxmlformats.org/officeDocument/2006/customXml\">"
                                       "<ds:schemaRefs/></ds:datastoreItem>"),
                       QStringLiteral("application/vnd.openxmlformats-officedocument.customXmlProperties+xml"),
                       {}});
    book.parts.append({QStringLiteral("xl/model/item.data"), QByteArrayLiteral("PKDATAMODEL-OPAQUE-BYTES"),
                       QStringLiteral("application/vnd.openxmlformats-officedocument.model+data"),
                       {}});
    book.workbookRels = {
        {QStringLiteral("rIdExt1"), officeRel("externalLink"), QStringLiteral("xl/externalLinks/externalLink1.xml")},
        {QStringLiteral("rIdConn"), officeRel("connections"), QStringLiteral("xl/connections.xml")},
        {QStringLiteral("rIdCustom1"), officeRel("customXml"), QStringLiteral("customXml/item1.xml")},
        {QStringLiteral("rIdModel"), officeRel("powerPivotData"), QStringLiteral("xl/model/item.data")},
    };
    book.afterSheets = QByteArrayLiteral("<externalReferences><externalReference r:id=\"rIdExt1\"/></externalReferences>");
    book.workbookRootAttributes = QByteArrayLiteral(
        " xmlns:x15=\"http://schemas.microsoft.com/office/spreadsheetml/2010/11/main\"");
    book.afterCalcPr = QByteArrayLiteral(
        "<extLst><ext uri=\"{FCE2AD5D-F65C-4FA6-A056-5C36A1767C68}\"><x15:dataModel><x15:modelTables>"
        "<x15:modelTable id=\"Inventory\" name=\"Inventory\" connection=\"Query - Inventory\"/>"
        "</x15:modelTables></x15:dataModel></ext></extLst>");
    book.sheets = {query};
    return book;
}

// The text Excel 365 writes into the legacy note that stands in for a
// threaded comment.
inline QByteArray threadedPlaceholder(const QByteArray &text)
{
    return QByteArrayLiteral("[Threaded comment]\n\nYour version of Excel allows you to read this threaded comment; "
                             "however, any edits to it will get removed if the file is opened in a newer version of "
                             "Excel. Learn more: https://go.microsoft.com/fwlink/?linkid=870924\n\nComment:\n    ")
        + text;
}

// Sheet-level parts: a threaded comment with a reply (and its legacy
// placeholder note), its person, a form check box (control properties,
// <controls>, its VML shape and its drawing anchor), and printer settings
// behind <pageSetup r:id>.
inline ExcelWorkbook sheetPartsBook()
{
    ExcelWorkbook book;
    ExcelSheet review;
    review.name = QStringLiteral("Review");
    review.rootAttributes = QByteArrayLiteral(
        " xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\""
        " xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\""
        " xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\"");
    review.sheetData = QByteArrayLiteral("<row r=\"1\"><c r=\"A1\"><v>100</v></c></row>"
                                         "<row r=\"3\"><c r=\"C3\" t=\"b\"><v>1</v></c></row>");
    review.afterData = QByteArrayLiteral(
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
        "<pageSetup paperSize=\"9\" orientation=\"landscape\" r:id=\"rId1\"/>"
        "<drawing r:id=\"rId5\"/><legacyDrawing r:id=\"rId4\"/>"
        "<mc:AlternateContent><mc:Choice Requires=\"x14\"><controls><mc:AlternateContent><mc:Choice Requires=\"x14\">"
        "<control shapeId=\"1025\" r:id=\"rId6\" name=\"Check Box 1\"><controlPr defaultSize=\"0\" autoFill=\"0\""
        " autoLine=\"0\" autoPict=\"0\"><anchor moveWithCells=\"1\"><from><xdr:col>1</xdr:col><xdr:colOff>0</xdr:colOff>"
        "<xdr:row>2</xdr:row><xdr:rowOff>0</xdr:rowOff></from><to><xdr:col>2</xdr:col><xdr:colOff>0</xdr:colOff>"
        "<xdr:row>3</xdr:row><xdr:rowOff>0</xdr:rowOff></to></anchor></controlPr></control></mc:Choice>"
        "</mc:AlternateContent></controls></mc:Choice></mc:AlternateContent>");
    review.rels = {
        {QStringLiteral("rId1"), officeRel("printerSettings"), QStringLiteral("xl/printerSettings/printerSettings1.bin")},
        {QStringLiteral("rId2"), officeRel("comments"), QStringLiteral("xl/comments1.xml")},
        {QStringLiteral("rId3"), msRel("2017/10/relationships/threadedComment"),
         QStringLiteral("xl/threadedComments/threadedComment1.xml")},
        {QStringLiteral("rId4"), officeRel("vmlDrawing"), QStringLiteral("xl/drawings/vmlDrawing1.vml")},
        {QStringLiteral("rId5"), officeRel("drawing"), QStringLiteral("xl/drawings/drawing1.xml")},
        {QStringLiteral("rId6"), officeRel("ctrlProp"), QStringLiteral("xl/ctrlProps/ctrlProp1.xml")},
    };
    book.parts.append({QStringLiteral("xl/printerSettings/printerSettings1.bin"),
                       QByteArrayLiteral("DEVMODE-A4-LANDSCAPE-OPAQUE"),
                       smlType("printerSettings"),
                       {}});
    book.parts.append({QStringLiteral("xl/comments1.xml"),
                       xmlHead() + QByteArrayLiteral("<comments ") + smlRoot()
                           + QByteArrayLiteral("><authors><author>tc={2B6F3A54-0000-4000-8000-000000000001}</author>"
                                               "</authors><commentList><comment ref=\"A1\" authorId=\"0\" shapeId=\"0\"><text><t>")
                           + threadedPlaceholder("Check this total")
                           + QByteArrayLiteral("</t></text></comment></commentList></comments>"),
                       smlType("comments+xml"),
                       {}});
    book.parts.append({QStringLiteral("xl/threadedComments/threadedComment1.xml"),
                       xmlHead() + QByteArrayLiteral(
                                       "<ThreadedComments xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments\">"
                                       "<threadedComment ref=\"A1\" dT=\"2026-10-01T10:00:00.00\" personId=\"{7C4A0000-0000-4000-8000-000000000001}\""
                                       " id=\"{2B6F3A54-0000-4000-8000-000000000001}\"><text>Check this total</text></threadedComment>"
                                       "<threadedComment ref=\"A1\" dT=\"2026-10-01T11:00:00.00\" personId=\"{7C4A0000-0000-4000-8000-000000000001}\""
                                       " id=\"{2B6F3A54-0000-4000-8000-000000000002}\" parentId=\"{2B6F3A54-0000-4000-8000-000000000001}\">"
                                       "<text>Fixed</text></threadedComment></ThreadedComments>"),
                       QStringLiteral("application/vnd.ms-excel.threadedcomments+xml"),
                       {}});
    book.parts.append({QStringLiteral("xl/persons/person.xml"),
                       xmlHead() + QByteArrayLiteral(
                                       "<personList xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments\">"
                                       "<person displayName=\"Owner\" id=\"{7C4A0000-0000-4000-8000-000000000001}\" userId=\"Owner\""
                                       " providerId=\"None\"/></personList>"),
                       QStringLiteral("application/vnd.ms-excel.person+xml"),
                       {}});
    book.parts.append({QStringLiteral("xl/ctrlProps/ctrlProp1.xml"),
                       xmlHead() + QByteArrayLiteral(
                                       "<formControlPr xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\""
                                       " objectType=\"CheckBox\" checked=\"Checked\" fmlaLink=\"$C$3\" lockText=\"1\" noThreeD=\"1\"/>"),
                       QStringLiteral("application/vnd.ms-excel.controlproperties+xml"),
                       {}});
    // The legacy drawing holds the check box (block 1: _x0000_s1025) and
    // the threaded comment's note (_x0000_s1026).
    book.parts.append({QStringLiteral("xl/drawings/vmlDrawing1.vml"),
                       QByteArrayLiteral(
                           "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\""
                           " xmlns:x=\"urn:schemas-microsoft-com:office:excel\"><o:shapelayout v:ext=\"edit\">"
                           "<o:idmap v:ext=\"edit\" data=\"1\"/></o:shapelayout>"
                           "<v:shapetype id=\"_x0000_t201\" coordsize=\"21600,21600\" o:spt=\"201\" path=\"m,l,21600r21600,l21600,xe\">"
                           "<v:stroke joinstyle=\"miter\"/><v:path shadowok=\"f\" o:extrusionok=\"f\" strokeok=\"f\" fillok=\"f\""
                           " o:connecttype=\"rect\"/><o:lock v:ext=\"edit\" shapetype=\"t\"/></v:shapetype>"
                           "<v:shape id=\"_x0000_s1025\" type=\"#_x0000_t201\" style=\"position:absolute;margin-left:48pt;"
                           "margin-top:30pt;width:48pt;height:15pt;z-index:1\" filled=\"f\" stroked=\"f\"><v:textbox>"
                           "<div style=\"text-align:left\">Approved</div></v:textbox><x:ClientData ObjectType=\"Checkbox\">"
                           "<x:Anchor>1, 0, 2, 0, 2, 0, 3, 0</x:Anchor><x:FmlaLink>$C$3</x:FmlaLink><x:Checked>1</x:Checked>"
                           "<x:AutoFill>False</x:AutoFill><x:NoThreeD/></x:ClientData></v:shape>"
                           "<v:shapetype id=\"_x0000_t202\" coordsize=\"21600,21600\" o:spt=\"202\" path=\"m,l,21600r21600,l21600,xe\">"
                           "<v:path gradientshapeok=\"t\" o:connecttype=\"rect\"/></v:shapetype>"
                           "<v:shape id=\"_x0000_s1026\" type=\"#_x0000_t202\" style=\"position:absolute;margin-left:59pt;"
                           "margin-top:1.5pt;width:108pt;height:59pt;z-index:2;visibility:hidden\"><x:ClientData ObjectType=\"Note\">"
                           "<x:Anchor>1, 15, 0, 2, 3, 15, 4, 4</x:Anchor><x:Row>0</x:Row><x:Column>0</x:Column></x:ClientData>"
                           "</v:shape></xml>"),
                       QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"),
                       {}});
    const QByteArray mc = QByteArrayLiteral("xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\"");
    book.parts.append({QStringLiteral("xl/drawings/drawing1.xml"),
                       xmlHead() + drawingRoot() + QByteArrayLiteral("<mc:AlternateContent ") + mc
                           + QByteArrayLiteral("><mc:Choice xmlns:a14=\"http://schemas.microsoft.com/office/drawing/2010/main\" Requires=\"a14\">"
                                               "<xdr:twoCellAnchor editAs=\"oneCell\">")
                           + anchorCells(1, 2, 2, 3)
                           + QByteArrayLiteral("<xdr:sp macro=\"\" textlink=\"\"><xdr:nvSpPr><xdr:cNvPr id=\"1025\" name=\"Check Box 1\" hidden=\"1\">"
                                               "<a:extLst><a:ext uri=\"{63B3BB69-23CF-44E3-9099-C40C66FF867C}\"><a14:compatExt spid=\"_x0000_s1025\"/>"
                                               "</a:ext></a:extLst></xdr:cNvPr><xdr:cNvSpPr/></xdr:nvSpPr><xdr:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
                                               "<a:ext cx=\"0\" cy=\"0\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr>"
                                               "</xdr:sp><xdr:clientData/></xdr:twoCellAnchor></mc:Choice></mc:AlternateContent></xdr:wsDr>"),
                       QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"),
                       {}});
    book.workbookRels = {{QStringLiteral("rIdPersons"), msRel("2017/10/relationships/person"),
                          QStringLiteral("xl/persons/person.xml")}};
    book.sheets = {review};
    return book;
}

// A macro-enabled workbook: the VBA project (never run), its signature,
// and the code names the project binds its modules to.
inline ExcelWorkbook macroBook()
{
    ExcelWorkbook book;
    book.mainContentType = QStringLiteral("application/vnd.ms-excel.sheet.macroEnabled.main+xml");
    book.workbookPr = QByteArrayLiteral("<workbookPr codeName=\"ThisWorkbook\"/>");
    ExcelSheet sheet;
    sheet.name = QStringLiteral("Macros");
    sheet.beforeData = QByteArrayLiteral("<sheetPr codeName=\"Sheet1\"/>");
    sheet.sheetData = QByteArrayLiteral("<row r=\"1\"><c r=\"A1\"><v>3</v></c></row>");
    book.sheets = {sheet};
    book.parts.append({QStringLiteral("xl/vbaProject.bin"), QByteArrayLiteral("VBA-PROJECT-OPAQUE-BYTES\x01\x02"),
                       QStringLiteral("application/vnd.ms-office.vbaProject"),
                       {{QStringLiteral("rId1"), msRel("2006/relationships/vbaProjectSignature"),
                         QStringLiteral("xl/vbaProjectSignature.bin")}}});
    book.parts.append({QStringLiteral("xl/vbaProjectSignature.bin"), QByteArrayLiteral("SIGNATURE-OPAQUE"),
                       QStringLiteral("application/vnd.ms-office.vbaProjectSignature"),
                       {}});
    book.workbookRels = {{QStringLiteral("rIdVba"), msRel("2006/relationships/vbaProject"),
                          QStringLiteral("xl/vbaProject.bin")}};
    return book;
}

} // namespace QindaOffice::Test
