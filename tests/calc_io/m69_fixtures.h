// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The M69 (D-078) packages, built by hand the way Microsoft Excel writes
// them (excel_package.h): a table with a calculated column, a totals row
// and the references Excel stores for it, legacy array formulas and an
// array constant (ordersBook); a chart sheet between two worksheets with
// its drawing, chart and chart style part (chartBook). Shared by
// tst_calc_io_structured, tst_calc_io_chartsheets and the LibreOffice gate.

#include "excel_package.h"

namespace QindaOffice::Test {

inline QByteArray row(int r, const QByteArray &cells)
{
    return QByteArrayLiteral("<row r=\"") + QByteArray::number(r) + "\">" + cells + "</row>";
}

// Orders!A1:D5 — Region | Qty | Price | Total — three data rows, Total a
// calculated column, a totals row; Report reads it every way Excel can.
inline ExcelWorkbook ordersBook()
{
    ExcelWorkbook book;
    book.sharedStrings = {"<t>Region</t>", "<t>Qty</t>", "<t>Price</t>", "<t>Total</t>",
                          "<t>North</t>", "<t>South</t>", "<t>East</t>"};
    const QByteArray total = QByteArrayLiteral("Orders[[#This Row],[Qty]]*Orders[[#This Row],[Price]]");
    ExcelSheet orders(QStringLiteral("Orders"),
                      row(1, "<c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c>"
                             "<c r=\"C1\" t=\"s\"><v>2</v></c><c r=\"D1\" t=\"s\"><v>3</v></c>")
                          + row(2, "<c r=\"A2\" t=\"s\"><v>4</v></c><c r=\"B2\"><v>2</v></c><c r=\"C2\"><v>10</v></c>"
                                   "<c r=\"D2\"><f>" + total + "</f><v>20</v></c>")
                          + row(3, "<c r=\"A3\" t=\"s\"><v>5</v></c><c r=\"B3\"><v>5</v></c><c r=\"C3\"><v>4</v></c>"
                                   "<c r=\"D3\"><f>" + total + "</f><v>20</v></c>")
                          + row(4, "<c r=\"A4\" t=\"s\"><v>6</v></c><c r=\"B4\"><v>3</v></c><c r=\"C4\"><v>7</v></c>"
                                   "<c r=\"D4\"><f>" + total + "</f><v>21</v></c>")
                          + row(5, "<c r=\"A5\" t=\"str\"><v>Total</v></c>"
                                   "<c r=\"B5\"><f>SUBTOTAL(109,Orders[Qty])</f><v>10</v></c>"
                                   "<c r=\"D5\"><f>SUBTOTAL(109,Orders[Total])</f><v>61</v></c>"),
                      QByteArrayLiteral("<tableParts count=\"1\"><tablePart r:id=\"rIdT\"/></tableParts>"));
    orders.rels = {{QStringLiteral("rIdT"), officeRel("table"), QStringLiteral("xl/tables/table1.xml")}};
    ExcelSheet report(QStringLiteral("Report"),
                      row(1, "<c r=\"A1\"><f>SUM(Orders[Total])</f><v>61</v></c>"
                             "<c r=\"E1\"><f t=\"array\" ref=\"E1:E3\">Orders[Qty]*2</f><v>4</v></c>"
                             "<c r=\"F1\"><f t=\"array\" ref=\"F1\">SUM(IF(Orders[Qty]&gt;2,Orders[Qty]))</f><v>8</v></c>")
                          + row(2, "<c r=\"A2\"><f>Orders[[#Totals],[Qty]]</f><v>10</v></c><c r=\"E2\"><v>10</v></c>")
                          + row(3, "<c r=\"A3\"><f>ROWS(Orders[#All])</f><v>5</v></c>"
                                   "<c r=\"B3\"><f>Orders[[#This Row],[Price]]</f><v>4</v></c>"
                                   "<c r=\"C3\"><f>_xlfn.SINGLE(Orders[Qty])</f><v>5</v></c><c r=\"E3\"><v>6</v></c>")
                          + row(4, "<c r=\"A4\"><f>COUNTA(Orders[[#Headers],[Region]:[Price]])</f><v>3</v></c>")
                          + row(5, "<c r=\"A5\"><f>SUM(Orders[[Qty]:[Price]])</f><v>31</v></c>")
                          + row(7, "<c r=\"A7\"><f>SUM({1,2;3,4})</f><v>10</v></c>"));
    book.sheets = {orders, report};
    book.parts.append({QStringLiteral("xl/tables/table1.xml"),
                       QByteArrayLiteral(
                           "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                           "<table xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" id=\"1\""
                           " name=\"Orders\" displayName=\"Orders\" ref=\"A1:D5\" totalsRowCount=\"1\">"
                           "<autoFilter ref=\"A1:D4\"/><tableColumns count=\"4\">"
                           "<tableColumn id=\"1\" name=\"Region\" totalsRowLabel=\"Total\"/>"
                           "<tableColumn id=\"2\" name=\"Qty\" totalsRowFunction=\"sum\"/>"
                           "<tableColumn id=\"3\" name=\"Price\"/>"
                           "<tableColumn id=\"4\" name=\"Total\" totalsRowFunction=\"sum\"><calculatedColumnFormula>")
                           + total + QByteArrayLiteral(
                               "</calculatedColumnFormula></tableColumn></tableColumns>"
                               "<tableStyleInfo name=\"TableStyleMedium2\" showFirstColumn=\"0\" showLastColumn=\"0\""
                               " showRowStripes=\"1\" showColumnStripes=\"0\"/></table>"),
                       smlType("table+xml"),
                       {}});
    return book;
}

inline const QByteArray kChartSheet = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<chartsheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
    " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheetPr/>"
    "<sheetViews><sheetView zoomScale=\"115\" workbookViewId=\"0\" zoomToFit=\"1\"/></sheetViews>"
    "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
    "<drawing r:id=\"rId1\"/></chartsheet>");

inline const QByteArray kDrawing = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\""
    " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><xdr:absoluteAnchor>"
    "<xdr:pos x=\"0\" y=\"0\"/><xdr:ext cx=\"8667750\" cy=\"6286500\"/><xdr:graphicFrame macro=\"\">"
    "<xdr:nvGraphicFramePr><xdr:cNvPr id=\"2\" name=\"Chart 1\"/><xdr:cNvGraphicFramePr>"
    "<a:graphicFrameLocks noGrp=\"1\"/></xdr:cNvGraphicFramePr></xdr:nvGraphicFramePr>"
    "<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic>"
    "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
    "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
    " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rId1\"/>"
    "</a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:absoluteAnchor></xdr:wsDr>");

inline QByteArray chartXml()
{
    const auto numbers = [](const char *ref, const QList<int> &values) {
        QByteArray out = QByteArrayLiteral("<c:numRef><c:f>") + ref + "</c:f><c:numCache><c:formatCode>General</c:formatCode>"
            + "<c:ptCount val=\"" + QByteArray::number(values.size()) + "\"/>";
        for (qsizetype i = 0; i < values.size(); ++i) {
            out += "<c:pt idx=\"" + QByteArray::number(i) + "\"><c:v>" + QByteArray::number(values.at(i)) + "</c:v></c:pt>";
        }
        return out + "</c:numCache></c:numRef>";
    };
    return QByteArrayLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
               " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
               " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
               "<c:roundedCorners val=\"0\"/><c:chart><c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>Quarter sales</a:t>"
               "</a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/><c:plotArea>"
               "<c:layout/><c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>"
               "<c:ser><c:idx val=\"0\"/><c:order val=\"0\"/><c:tx><c:strRef><c:f>'Q1 Data'!$B$1</c:f></c:strRef></c:tx>"
               "<c:cat><c:strRef><c:f>'Q1 Data'!$A$2:$A$4</c:f></c:strRef></c:cat><c:val>")
        + numbers("'Q1 Data'!$B$2:$B$4", {10, 20, 30})
        + QByteArrayLiteral(
            "</c:val></c:ser><c:axId val=\"1\"/><c:axId val=\"2\"/></c:barChart><c:catAx><c:axId val=\"1\"/>"
            "<c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"b\"/>"
            "<c:crossAx val=\"2\"/></c:catAx><c:valAx><c:axId val=\"2\"/><c:scaling><c:orientation val=\"minMax\"/>"
            "</c:scaling><c:delete val=\"0\"/><c:axPos val=\"l\"/><c:crossAx val=\"1\"/></c:valAx></c:plotArea>"
            "<c:plotVisOnly val=\"1\"/></c:chart></c:chartSpace>");
}

inline const QByteArray kChartStyle = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<cs:chartStyle xmlns:cs=\"http://schemas.microsoft.com/office/drawing/2012/chartStyle\""
    " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" id=\"201\"/>");

// 'Q1 Data' (the numbers), 'Sales Chart' (the chart sheet), Summary — and
// a name scoped to Summary, which counts sheets by position.
inline ExcelWorkbook chartBook()
{
    ExcelWorkbook book;
    book.sharedStrings = {"<t>Region</t>", "<t>Sales</t>", "<t>North</t>", "<t>South</t>", "<t>East</t>"};
    ExcelSheet data(QStringLiteral("Q1 Data"),
                    QByteArrayLiteral("<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
                                      "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\"><v>10</v></c></row>"
                                      "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>3</v></c><c r=\"B3\"><v>20</v></c></row>"
                                      "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>4</v></c><c r=\"B4\"><v>30</v></c></row>"));
    ExcelSheet chart(QStringLiteral("Sales Chart"));
    chart.chartSheetXml = kChartSheet;
    chart.rels = {{QStringLiteral("rId1"), officeRel("drawing"), QStringLiteral("xl/drawings/drawing1.xml")}};
    ExcelSheet summary(QStringLiteral("Summary"),
                       QByteArrayLiteral("<row r=\"1\"><c r=\"A1\"><f>SUM('Q1 Data'!B2:B4)</f><v>60</v></c></row>"));
    book.sheets = {data, chart, summary};
    book.definedNames = QByteArrayLiteral("<definedName name=\"Top\" localSheetId=\"2\">Summary!$A$1</definedName>");
    book.parts.append({QStringLiteral("xl/drawings/drawing1.xml"), kDrawing,
                       QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"),
                       {{QStringLiteral("rId1"), officeRel("chart"), QStringLiteral("xl/charts/chart1.xml")}}});
    book.parts.append({QStringLiteral("xl/charts/chart1.xml"), chartXml(),
                       QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"),
                       {{QStringLiteral("rId1"), QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/chartStyle"),
                         QStringLiteral("xl/charts/style1.xml")}}});
    book.parts.append({QStringLiteral("xl/charts/style1.xml"), kChartStyle,
                       QStringLiteral("application/vnd.ms-office.chartstyle+xml"), {}});
    return book;
}

} // namespace QindaOffice::Test
