// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): a sheet's print settings through an Excel save. A package
// built by hand the way Excel writes it — every <pageSetup> attribute, the
// margins, the print options, odd / even / first headers and footers,
// manual row and column breaks, Fit to in sheetPr, a printer-settings part,
// a two-area print area and print titles of rows and columns — reads into
// Sheet::PageSetup, saves schema-valid with the printer settings still
// linked, and reads back equal. Excel's defaults write nothing; a custom
// sheet view's own print settings are not the sheet's; a dynamic print
// area (=OFFSET(…)) the model cannot hold is written back as read; a
// header's picture (&G, its VML part and image) travels with it; and
// LibreOffice prints a sheet saved with print titles and Fit to one page
// wide to PDF with the titles on every page (skips without soffice or
// pdftotext; QQO_GRAB_DIR keeps the PDF).

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_package.h"
#include "support/soffice_env.h"

#include "page_layout.h"
#include "workbook.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;

namespace {

const QByteArray kPrinterBytes = QByteArrayLiteral("DEVMODE-A4-LANDSCAPE-OPAQUE");

QByteArray rows(int count, int columns)
{
    QByteArray out;
    for (int r = 1; r <= count; ++r) {
        out += "<row r=\"" + QByteArray::number(r) + "\">";
        for (int c = 0; c < columns; ++c) {
            out += "<c r=\"" + QByteArray(1, char('A' + c)) + QByteArray::number(r) + "\"><v>"
                + QByteArray::number(r * 10 + c) + "</v></c>";
        }
        out += "</row>";
    }
    return out;
}

ExcelRel printerRel()
{
    return {QStringLiteral("rId1"), officeRel("printerSettings"), QStringLiteral("xl/printerSettings/printerSettings1.bin")};
}

ExcelPart printerPart()
{
    return {QStringLiteral("xl/printerSettings/printerSettings1.bin"), kPrinterBytes, smlType("printerSettings"), {}};
}

// Every print setting Excel writes, as Excel writes it.
ExcelWorkbook everySettingBook()
{
    ExcelWorkbook book;
    ExcelSheet report(QStringLiteral("Report"), rows(40, 10));
    report.beforeData = QByteArrayLiteral("<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>");
    report.afterData = QByteArrayLiteral(
        "<printOptions horizontalCentered=\"1\" verticalCentered=\"1\" headings=\"1\" gridLines=\"1\"/>"
        "<pageMargins left=\"0.25\" right=\"0.25\" top=\"0.5\" bottom=\"0.5\" header=\"0.2\" footer=\"0.2\"/>"
        "<pageSetup paperSize=\"9\" scale=\"80\" firstPageNumber=\"3\" fitToWidth=\"1\" fitToHeight=\"0\""
        " pageOrder=\"overThenDown\" orientation=\"landscape\" blackAndWhite=\"1\" draft=\"1\" cellComments=\"atEnd\""
        " useFirstPageNumber=\"1\" errors=\"dash\" horizontalDpi=\"600\" verticalDpi=\"600\" copies=\"2\" r:id=\"rId1\"/>"
        "<headerFooter differentOddEven=\"1\" differentFirst=\"1\" scaleWithDoc=\"0\" alignWithMargins=\"0\">"
        "<oddHeader>&amp;LDraft&amp;CPage &amp;P of &amp;N</oddHeader><oddFooter>&amp;R&amp;D &amp;T</oddFooter>"
        "<evenHeader>&amp;CEven page &amp;P</evenHeader><evenFooter>&amp;C&amp;F</evenFooter>"
        "<firstHeader>&amp;C&amp;\"Arial,Bold\"&amp;14Cover</firstHeader><firstFooter>&amp;C&amp;A</firstFooter>"
        "</headerFooter>"
        "<rowBreaks count=\"2\" manualBreakCount=\"2\"><brk id=\"10\" max=\"16383\" man=\"1\"/>"
        "<brk id=\"25\" max=\"16383\" man=\"1\"/></rowBreaks>"
        "<colBreaks count=\"1\" manualBreakCount=\"1\"><brk id=\"3\" max=\"1048575\" man=\"1\"/></colBreaks>");
    report.rels = {printerRel()};
    book.sheets = {report};
    book.parts = {printerPart()};
    book.definedNames = QByteArrayLiteral(
        "<definedName name=\"_xlnm.Print_Area\" localSheetId=\"0\">Report!$A$1:$F$40,Report!$H$1:$J$10</definedName>"
        "<definedName name=\"_xlnm.Print_Titles\" localSheetId=\"0\">Report!$A:$A,Report!$1:$2</definedName>");
    return book;
}

const XmlElement *sheetChild(const XmlElement &sheet, QStringView name)
{
    return sheet.child(sml(), name);
}

} // namespace

class CalcIoPrintTest : public QObject {
    Q_OBJECT
private slots:
    void everyPrintSettingRoundTrips();
    void defaultsWriteNothing();
    void customSheetViewsKeepTheirOwnSettings();
    void dynamicPrintAreaIsKept();
    void printerSettingsAloneTravel();
    void headerPicturesTravel();
    void libreOfficePrintsTitlesAndFitToWidth();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool open(const ExcelWorkbook &book, const QString &name, Workbook *wb)
    {
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(book.build());
        file.close();
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("fixture %s is not schema-valid: %s", qPrintable(name), qPrintable(schema));
            return false;
        }
        QString error;
        return IO::readXlsxFile(path(name), wb, &error);
    }
    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name)
    {
        QString error;
        if (!IO::writeXlsxFile(wb, path(name), &error)) {
            qWarning("write failed: %s", qPrintable(error));
            return nullptr;
        }
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("saved package is not schema-valid: %s", qPrintable(schema));
            return nullptr;
        }
        keepForInspection(path(name));
        auto package = Package::load(path(name), &error);
        if (package) {
            const QStringList broken = brokenWiring(*package);
            if (!broken.isEmpty()) {
                qWarning("broken wiring: %s", qPrintable(broken.join(QLatin1String("; "))));
                return nullptr;
            }
        }
        return package;
    }
};

void CalcIoPrintTest::everyPrintSettingRoundTrips()
{
    Workbook wb;
    QVERIFY(open(everySettingBook(), QStringLiteral("print.xlsx"), &wb));
    const Sheet &sheet = wb.sheetAt(0);
    const Sheet::PageSetup &read = sheet.pageSetup();
    QVERIFY(read.fitToPage);
    QCOMPARE(read.fitToWidth, 1);
    QCOMPARE(read.fitToHeight, 0);
    QCOMPARE(read.paperSize, 9);
    QCOMPARE(read.scale, 80);
    QCOMPARE(read.firstPageNumber, 3);
    QVERIFY(read.useFirstPageNumber);
    QVERIFY(read.overThenDown);
    QCOMPARE(read.orientation, 1);
    QVERIFY(read.blackAndWhite && read.draft);
    QCOMPARE(read.cellComments, 1);
    QCOMPARE(read.errors, 2);
    QCOMPARE(read.marginLeft, 0.25);
    QCOMPARE(read.marginTop, 0.5);
    QCOMPARE(read.marginHeader, 0.2);
    QVERIFY(read.centerHorizontally && read.centerVertically && read.headings && read.gridLines);
    QVERIFY(read.differentOddEven && read.differentFirst && !read.scaleWithDoc && !read.alignWithMargins);
    QCOMPARE(read.oddHeader, QStringLiteral("&LDraft&CPage &P of &N"));
    QCOMPARE(read.oddFooter, QStringLiteral("&R&D &T"));
    QCOMPARE(read.evenHeader, QStringLiteral("&CEven page &P"));
    QCOMPARE(read.firstHeader, QStringLiteral("&C&\"Arial,Bold\"&14Cover"));
    QCOMPARE(read.firstFooter, QStringLiteral("&C&A"));
    QCOMPARE(read.rowBreaks, (QList<qint32>{10, 25}));
    QCOMPARE(read.colBreaks, (QList<qint32>{3}));
    QCOMPARE(*sheet.printArea(), (CellRange{{0, 0}, {39, 5}}));
    QCOMPARE(read.morePrintAreas, (QList<CellRange>{CellRange{{0, 7}, {9, 9}}}));
    QCOMPARE(sheet.printTitleRows()->topLeft.row, 0);
    QCOMPARE(sheet.printTitleRows()->bottomRight.row, 1);
    QCOMPARE(sheet.printTitleCols()->bottomRight.col, 0);
    // Built-in print names are no user names.
    QVERIFY(wb.definedNames().isEmpty());

    const auto package = save(wb, QStringLiteral("print-saved.xlsx"));
    QVERIFY(package);
    const QString part = savedSheetPart(*package, QStringLiteral("Report"));
    const auto xml = package->xml(part);
    QVERIFY(xml);
    const XmlElement *setup = sheetChild(*xml, u"pageSetup");
    QVERIFY(setup != nullptr);
    const QList<std::pair<const char16_t *, const char *>> attributes{
        {u"paperSize", "9"},       {u"scale", "80"},         {u"firstPageNumber", "3"}, {u"fitToHeight", "0"},
        {u"pageOrder", "overThenDown"}, {u"orientation", "landscape"}, {u"blackAndWhite", "1"},
        {u"draft", "1"},           {u"cellComments", "atEnd"}, {u"useFirstPageNumber", "1"}, {u"errors", "dash"},
        {u"horizontalDpi", "600"}, {u"verticalDpi", "600"}, {u"copies", "2"}};
    for (const auto &[name, value] : attributes) {
        QCOMPARE(setup->attr(QStringView(name)), QString::fromLatin1(value));
    }
    QVERIFY(!setup->hasAttribute(QString(), u"fitToWidth")); // the schema's default, 1
    // The printer settings travel with their link.
    const QString printer = partForId(*package, part, setup->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    QCOMPARE(package->data(printer), kPrinterBytes);
    const XmlElement *options = sheetChild(*xml, u"printOptions");
    QVERIFY(options != nullptr);
    for (const char16_t *name : {u"horizontalCentered", u"verticalCentered", u"headings", u"gridLines"}) {
        QCOMPARE(options->attr(QStringView(name)), QStringLiteral("1"));
    }
    const XmlElement *margins = sheetChild(*xml, u"pageMargins");
    QVERIFY(margins != nullptr);
    QCOMPARE(margins->attr(u"left"), QStringLiteral("0.25"));
    QCOMPARE(margins->attr(u"footer"), QStringLiteral("0.2"));
    const XmlElement *headerFooter = sheetChild(*xml, u"headerFooter");
    QVERIFY(headerFooter != nullptr);
    QCOMPARE(headerFooter->attr(u"scaleWithDoc"), QStringLiteral("0"));
    QCOMPARE(headerFooter->child(sml(), u"evenFooter")->text(), QStringLiteral("&C&F"));
    QCOMPARE(headerFooter->child(sml(), u"firstHeader")->text(), QStringLiteral("&C&\"Arial,Bold\"&14Cover"));
    const XmlElement *rowBreaks = sheetChild(*xml, u"rowBreaks");
    QVERIFY(rowBreaks != nullptr);
    QCOMPARE(rowBreaks->attr(u"manualBreakCount"), QStringLiteral("2"));
    const QList<const XmlElement *> breaks = rowBreaks->elements(sml(), u"brk");
    QCOMPARE(breaks.size(), 2);
    QCOMPARE(breaks.at(1)->attr(u"id"), QStringLiteral("25"));
    QCOMPARE(breaks.at(1)->attr(u"max"), QStringLiteral("16383"));
    QCOMPARE(breaks.at(1)->attr(u"man"), QStringLiteral("1"));
    QCOMPARE(sheetChild(*xml, u"colBreaks")->elements(sml(), u"brk").first()->attr(u"max"), QStringLiteral("1048575"));
    const XmlElement *sheetPr = sheetChild(*xml, u"sheetPr");
    QVERIFY(sheetPr != nullptr && sheetPr->child(sml(), u"pageSetUpPr") != nullptr);
    QCOMPARE(sheetPr->child(sml(), u"pageSetUpPr")->attr(u"fitToPage"), QStringLiteral("1"));
    QString area;
    QString titles;
    for (const SavedName &name : savedDefinedNames(*package)) {
        (name.name == QLatin1String("_xlnm.Print_Area") ? area : titles) = name.body;
        QCOMPARE(name.localSheetId, QStringLiteral("0"));
    }
    QCOMPARE(area, QStringLiteral("Report!$A$1:$F$40,Report!$H$1:$J$10"));
    QCOMPARE(titles, QStringLiteral("Report!$A:$A,Report!$1:$2"));

    // Read back: the same settings.
    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(path(QStringLiteral("print-saved.xlsx")), &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).pageSetup(), read);
    QCOMPARE(back.sheetAt(0).printArea(), sheet.printArea());
    QCOMPARE(back.sheetAt(0).printTitleRows(), sheet.printTitleRows());
    QCOMPARE(back.sheetAt(0).printTitleCols(), sheet.printTitleCols());
}

void CalcIoPrintTest::defaultsWriteNothing()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Plain"));
    wb.setCellValue(0, {0, 0}, Value(1.0));
    auto package = save(wb, QStringLiteral("plain.xlsx"));
    QVERIFY(package);
    auto xml = package->xml(savedSheetPart(*package, QStringLiteral("Plain")));
    QVERIFY(xml);
    for (const char16_t *name : {u"printOptions", u"pageMargins", u"pageSetup", u"headerFooter", u"rowBreaks", u"colBreaks"}) {
        QVERIFY2(sheetChild(*xml, QStringView(name)) == nullptr, qPrintable(QString::fromUtf16(name)));
    }
    const XmlElement *sheetPr = sheetChild(*xml, u"sheetPr");
    QVERIFY(sheetPr == nullptr || sheetPr->child(sml(), u"pageSetUpPr") == nullptr);
    // One setting: Excel's pair, margins and page setup, and nothing else.
    Sheet::PageSetup setup;
    setup.orientation = 1;
    wb.sheetAt(0).setPageSetup(setup);
    package = save(wb, QStringLiteral("landscape.xlsx"));
    QVERIFY(package);
    xml = package->xml(savedSheetPart(*package, QStringLiteral("Plain")));
    QVERIFY(sheetChild(*xml, u"pageMargins") != nullptr);
    QCOMPARE(sheetChild(*xml, u"pageMargins")->attr(u"top"), QStringLiteral("0.75"));
    QCOMPARE(sheetChild(*xml, u"pageSetup")->attributes.size(), 1);
    QCOMPARE(sheetChild(*xml, u"pageSetup")->attr(u"orientation"), QStringLiteral("landscape"));
    QVERIFY(sheetChild(*xml, u"printOptions") == nullptr && sheetChild(*xml, u"headerFooter") == nullptr);
    Workbook back;
    QVERIFY(IO::readXlsxFile(path(QStringLiteral("landscape.xlsx")), &back));
    QCOMPARE(back.sheetAt(0).pageSetup().orientation, 1);
    QVERIFY(!back.sheetAt(0).pageSetup().fitToPage);
}

void CalcIoPrintTest::customSheetViewsKeepTheirOwnSettings()
{
    ExcelWorkbook book;
    ExcelSheet sheet(QStringLiteral("Views"), rows(3, 3));
    sheet.afterData = QByteArrayLiteral(
        "<customSheetViews><customSheetView guid=\"{0C6F0E0F-0000-4000-8000-000000000001}\">"
        "<pageMargins left=\"1\" right=\"1\" top=\"1\" bottom=\"1\" header=\"0.5\" footer=\"0.5\"/>"
        "<pageSetup paperSize=\"9\" orientation=\"landscape\"/></customSheetView></customSheetViews>"
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
        "<pageSetup orientation=\"portrait\"/>");
    book.sheets = {sheet};
    Workbook wb;
    QVERIFY(open(book, QStringLiteral("views.xlsx"), &wb));
    const Sheet::PageSetup &setup = wb.sheetAt(0).pageSetup();
    QCOMPARE(setup.orientation, 0);
    QCOMPARE(setup.paperSize, 0);
    QCOMPARE(setup.marginLeft, 0.7);
}

void CalcIoPrintTest::dynamicPrintAreaIsKept()
{
    ExcelWorkbook book;
    book.sheets = {ExcelSheet(QStringLiteral("Report"), rows(5, 3))};
    book.definedNames = QByteArrayLiteral(
        "<definedName name=\"_xlnm.Print_Area\" localSheetId=\"0\">OFFSET(Report!$A$1,0,0,COUNTA(Report!$A:$A),3)</definedName>");
    Workbook wb;
    QVERIFY(open(book, QStringLiteral("dynamic.xlsx"), &wb));
    QVERIFY(!wb.sheetAt(0).printArea().has_value()); // the model cannot hold it…
    auto package = save(wb, QStringLiteral("dynamic-saved.xlsx"));
    QVERIFY(package);
    QList<SavedName> names = savedDefinedNames(*package);
    QCOMPARE(names.size(), 1); // …so it is written back as read
    QCOMPARE(names.first().name, QStringLiteral("_xlnm.Print_Area"));
    QCOMPARE(names.first().localSheetId, QStringLiteral("0"));
    QCOMPARE(names.first().body, QStringLiteral("OFFSET(Report!$A$1,0,0,COUNTA(Report!$A:$A),3)"));
    // A print area set in QindaCalc takes its place (one per sheet).
    wb.sheetAt(0).setPrintArea(CellRange{{0, 0}, {1, 1}});
    package = save(wb, QStringLiteral("dynamic-replaced.xlsx"));
    QVERIFY(package);
    names = savedDefinedNames(*package);
    QCOMPARE(names.size(), 1);
    QCOMPARE(names.first().body, QStringLiteral("Report!$A$1:$B$2"));
}

void CalcIoPrintTest::printerSettingsAloneTravel()
{
    // A sheet whose print settings are Excel's defaults but whose printer
    // settings are not: the model writes nothing, the carry stage keeps the
    // link — with the margins Excel writes beside it.
    ExcelWorkbook book;
    ExcelSheet sheet(QStringLiteral("Printer"), rows(3, 3));
    sheet.afterData = QByteArrayLiteral(
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
        "<pageSetup orientation=\"portrait\" r:id=\"rId1\"/>");
    sheet.rels = {printerRel()};
    book.sheets = {sheet};
    book.parts = {printerPart()};
    Workbook wb;
    QVERIFY(open(book, QStringLiteral("printer.xlsx"), &wb));
    QCOMPARE(wb.sheetAt(0).pageSetup(), Sheet::PageSetup());
    wb.setCellValue(0, {5, 0}, Value(QStringLiteral("edited")));
    const auto package = save(wb, QStringLiteral("printer-saved.xlsx"));
    QVERIFY(package);
    const QString part = savedSheetPart(*package, QStringLiteral("Printer"));
    const auto xml = package->xml(part);
    QVERIFY(xml);
    const XmlElement *setup = sheetChild(*xml, u"pageSetup");
    QVERIFY(setup != nullptr);
    QCOMPARE(setup->attr(u"orientation"), QStringLiteral("portrait"));
    QCOMPARE(package->data(partForId(*package, part, setup->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"))),
             kPrinterBytes);
    QVERIFY(sheetChild(*xml, u"pageMargins") != nullptr);
}

void CalcIoPrintTest::headerPicturesTravel()
{
    // A logo in the centre header (&G): Excel keeps the picture in a VML
    // part of its own behind <legacyDrawingHF>.
    const QByteArray png = QByteArrayLiteral("\x89PNG\r\n\x1a\nLOGO-OPAQUE");
    const QByteArray vml = QByteArrayLiteral(
        "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\""
        " xmlns:x=\"urn:schemas-microsoft-com:office:excel\"><o:shapelayout v:ext=\"edit\"><o:idmap v:ext=\"edit\" data=\"1\"/>"
        "</o:shapelayout><v:shapetype id=\"_x0000_t75\" coordsize=\"21600,21600\" o:spt=\"75\" filled=\"f\" stroked=\"f\">"
        "<v:path o:extrusionok=\"f\" gradientshapeok=\"t\" o:connecttype=\"rect\"/></v:shapetype>"
        "<v:shape id=\"CH\" o:spid=\"_x0000_s1025\" type=\"#_x0000_t75\" style=\"position:absolute;width:60pt;height:20pt\">"
        "<v:imagedata o:relid=\"rId1\" o:title=\"logo\"/></v:shape></xml>");
    ExcelWorkbook book;
    ExcelSheet sheet(QStringLiteral("Letter"), rows(3, 3));
    sheet.afterData = QByteArrayLiteral(
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
        "<headerFooter><oddHeader>&amp;C&amp;G</oddHeader></headerFooter><legacyDrawingHF r:id=\"rId2\"/>");
    sheet.rels = {{QStringLiteral("rId2"), officeRel("vmlDrawing"), QStringLiteral("xl/drawings/vmlDrawing1.vml")}};
    book.sheets = {sheet};
    book.parts = {{QStringLiteral("xl/drawings/vmlDrawing1.vml"), vml,
                   QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"),
                   {{QStringLiteral("rId1"), officeRel("image"), QStringLiteral("xl/media/image1.png")}}},
                  {QStringLiteral("xl/media/image1.png"), png, QStringLiteral("image/png"), {}}};
    Workbook wb;
    QVERIFY(open(book, QStringLiteral("logo.xlsx"), &wb));
    QCOMPARE(wb.sheetAt(0).pageSetup().oddHeader, QStringLiteral("&C&G"));
    wb.setCellValue(0, {5, 0}, Value(QStringLiteral("edited")));
    const auto package = save(wb, QStringLiteral("logo-saved.xlsx"));
    QVERIFY(package);
    const QString part = savedSheetPart(*package, QStringLiteral("Letter"));
    const auto xml = package->xml(part);
    QVERIFY(xml);
    QCOMPARE(sheetChild(*xml, u"headerFooter")->child(sml(), u"oddHeader")->text(), QStringLiteral("&C&G"));
    const XmlElement *legacy = sheetChild(*xml, u"legacyDrawingHF");
    QVERIFY2(legacy != nullptr, "the header's picture part was lost");
    const QString drawing = partForId(*package, part, legacy->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    const QByteArray saved = package->data(drawing);
    QVERIFY2(saved.contains("id=\"CH\"") && saved.contains("o:title=\"logo\""), saved.constData());
    // Its picture: the relationship the shape names leads to the same bytes.
    const QRegularExpressionMatch relid = QRegularExpression(QStringLiteral("o:relid=\"([^\"]+)\"")).match(QString::fromUtf8(saved));
    QVERIFY(relid.hasMatch());
    QCOMPARE(package->data(partForId(*package, drawing, relid.captured(1))), png);
    // No notes drawing took the logo for a control.
    QCOMPARE(partsByType(*package, part, officeRel("vmlDrawing")).size(), 1);
    QVERIFY(sheetChild(*xml, u"legacyDrawing") == nullptr);
}

void CalcIoPrintTest::libreOfficePrintsTitlesAndFitToWidth()
{
    const QString pdftotext = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    QProcess probe;
    startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0 || pdftotext.isEmpty()) {
        QSKIP("LibreOffice or pdftotext unavailable in this environment");
    }
    // Twenty 64 px columns (960 pt) and 120 rows; row 1 holds the titles.
    Workbook wb;
    wb.addSheet(QStringLiteral("Wide"));
    for (int c = 0; c < 20; ++c) {
        wb.sheetAt(0).setColumnWidth(c, 64);
        wb.setCellValue(0, {0, c}, Value(QStringLiteral("Col%1").arg(c + 1)));
        for (int r = 1; r < 120; ++r) {
            wb.setCellValue(0, {r, c}, Value(double(r * 100 + c)));
        }
    }
    wb.sheetAt(0).setPrintTitles(CellRange{{0, 0}, {0, Sheet::kMaxCols - 1}}, std::nullopt);
    Sheet::PageSetup setup;
    setup.fitToPage = true;
    setup.fitToWidth = 1;
    setup.fitToHeight = 0;
    wb.sheetAt(0).setPageSetup(setup);
    // Ours: 511.2 / 960 → 53 %; 86 rows on the first page, the rest under
    // the repeated title row on the second.
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.scalePercent, 53);
    QCOMPARE(layout.pages.size(), 2);
    QVERIFY(layout.pages.at(1).titleRows.has_value());
    QVERIFY(save(wb, QStringLiteral("titles.xlsx")));

    QProcess soffice;
    startSoffice(soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nologo"),
                           QStringLiteral("-env:UserInstallation=file://%1/profile").arg(m_dir.path()),
                           QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"),
                           path(QStringLiteral("pdf")), path(QStringLiteral("titles.xlsx"))});
    QVERIFY(soffice.waitForFinished(180000));
    const QString pdf = path(QStringLiteral("pdf/titles.pdf"));
    QVERIFY2(QFileInfo::exists(pdf), soffice.readAllStandardError().constData());
    if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty() && QDir().mkpath(grabs)) {
        const QString kept = QDir(grabs).filePath(QStringLiteral("m78-print-titles-fit-width.pdf"));
        QFile::remove(kept);
        QFile::copy(pdf, kept);
    }
    QProcess text;
    text.start(pdftotext, {QStringLiteral("-layout"), pdf, QStringLiteral("-")});
    QVERIFY(text.waitForFinished(60000));
    QStringList pages = QString::fromUtf8(text.readAllStandardOutput()).split(QLatin1Char('\f'));
    while (!pages.isEmpty() && pages.last().trimmed().isEmpty()) {
        pages.removeLast();
    }
    // One page wide: every page holds the whole title row, first and last
    // column; and as many pages as Fit to's rows make.
    QCOMPARE(pages.size(), int(layout.pages.size()));
    for (const QString &page : std::as_const(pages)) {
        QVERIFY2(page.contains(QLatin1String("Col1")) && page.contains(QLatin1String("Col20")), qPrintable(page.left(300)));
    }
    QVERIFY(pages.last().contains(QLatin1String("11919"))); // the last row, last column
}

QTEST_GUILESS_MAIN(CalcIoPrintTest)
#include "tst_calc_io_print.moc"
