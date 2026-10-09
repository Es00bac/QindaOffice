// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091): Excel's Custom Views through a save. A package built by hand
// the way Excel writes them — two views in workbook.xml's
// <customWorkbookViews>, each sheet's <customSheetView> parts (a frozen
// pane, a split in twips, zoom, the selection, a sheet hidden by one view,
// the view's own breaks, margins, page setup and filter) and the hidden
// Z_<GUID>_.wvu.* names that hold a view's hidden rows and columns, print
// area, print titles and filter range — reads into the model (the names
// do not land among the workbook's defined names), saves schema-valid with
// every element in its schema slot (customSheetViews before mergeCells,
// customWorkbookViews before pivotCaches) and the parts the save does not
// own byte for byte, and re-reads equal. A view added in QindaCalc is
// written the same way, and a sheet part whose view the workbook does not
// list is dropped.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include "custom_view.h"
#include "workbook.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

#include <algorithm>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;

namespace {

const QString kSummary = QStringLiteral("{5D7B1A2C-3E4F-4A5B-8C6D-7E8F9A0B1C2D}");
const QString kWide = QStringLiteral("{A1B2C3D4-E5F6-4789-ABCD-EF0123456789}");
const QByteArray kPrinterBytes = QByteArrayLiteral("DEVMODE-VIEWS-OPAQUE");

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

// Two views as Excel 365 writes them: "Summary" holds everything (frozen
// first row and column, 85 %, rows 5, 6 and 9 and column C hidden, a filter
// on column B, a print area, titles and its own page setup; the Notes
// sheet hidden), "Wide" neither hidden lines nor print settings (a split,
// 60 %, the Notes sheet active).
ExcelWorkbook excelViews()
{
    ExcelWorkbook book;
    ExcelSheet data(QStringLiteral("Data"), rows(30, 4));
    data.afterData = QByteArrayLiteral(
        "<customSheetViews>"
        "<customSheetView guid=\"{5D7B1A2C-3E4F-4A5B-8C6D-7E8F9A0B1C2D}\" scale=\"85\" showGridLines=\"0\""
        " showPageBreaks=\"1\" fitToPage=\"1\" printArea=\"1\" filter=\"1\" showAutoFilter=\"1\" hiddenRows=\"1\""
        " hiddenColumns=\"1\" topLeftCell=\"A3\">"
        "<pane xSplit=\"1\" ySplit=\"1\" topLeftCell=\"B4\" activePane=\"bottomRight\" state=\"frozen\"/>"
        "<selection pane=\"bottomRight\" activeCell=\"C5\" sqref=\"C5\"/>"
        "<rowBreaks count=\"1\" manualBreakCount=\"1\"><brk id=\"15\" max=\"16383\" man=\"1\"/></rowBreaks>"
        "<pageMargins left=\"0.5\" right=\"0.5\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
        "<pageSetup paperSize=\"9\" fitToHeight=\"0\" orientation=\"landscape\" r:id=\"rId1\"/>"
        "<autoFilter ref=\"A1:D30\"><filterColumn colId=\"1\"><filters><filter val=\"21\"/><filter val=\"31\"/>"
        "</filters></filterColumn></autoFilter>"
        "</customSheetView>"
        "<customSheetView guid=\"{A1B2C3D4-E5F6-4789-ABCD-EF0123456789}\" scale=\"60\" topLeftCell=\"A1\">"
        "<pane xSplit=\"2400\" ySplit=\"1800\" topLeftCell=\"C6\" activePane=\"bottomRight\" state=\"split\"/>"
        "<selection pane=\"bottomRight\" activeCell=\"D8\" sqref=\"D8\"/>"
        "</customSheetView>"
        "</customSheetViews>"
        "<mergeCells count=\"1\"><mergeCell ref=\"F1:G1\"/></mergeCells>"
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
        "<pageSetup orientation=\"portrait\" r:id=\"rId2\"/>");
    data.rels = {{QStringLiteral("rId1"), officeRel("printerSettings"), QStringLiteral("xl/printerSettings/printerSettings1.bin")},
                 {QStringLiteral("rId2"), officeRel("printerSettings"), QStringLiteral("xl/printerSettings/printerSettings2.bin")}};
    ExcelSheet notes(QStringLiteral("Notes"), rows(3, 2));
    notes.afterData = QByteArrayLiteral(
        "<customSheetViews>"
        "<customSheetView guid=\"{5D7B1A2C-3E4F-4A5B-8C6D-7E8F9A0B1C2D}\" state=\"hidden\" topLeftCell=\"A1\">"
        "<selection activeCell=\"A1\" sqref=\"A1\"/></customSheetView>"
        "<customSheetView guid=\"{A1B2C3D4-E5F6-4789-ABCD-EF0123456789}\" showGridLines=\"0\" topLeftCell=\"B2\">"
        "<selection activeCell=\"B3\" sqref=\"B3\"/></customSheetView>"
        "<customSheetView guid=\"{0BADF00D-0000-4000-8000-000000000000}\" topLeftCell=\"A1\"/>"
        "</customSheetViews>"
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>");
    book.sheets = {data, notes};
    book.parts = {{QStringLiteral("xl/printerSettings/printerSettings1.bin"), kPrinterBytes, smlType("printerSettings"), {}},
                  {QStringLiteral("xl/printerSettings/printerSettings2.bin"), kPrinterBytes + "2", smlType("printerSettings"), {}}};
    book.definedNames = QByteArrayLiteral(
        "<definedName name=\"Z_5D7B1A2C_3E4F_4A5B_8C6D_7E8F9A0B1C2D_.wvu.Cols\" localSheetId=\"0\" hidden=\"1\">Data!$C:$C</definedName>"
        "<definedName name=\"Z_5D7B1A2C_3E4F_4A5B_8C6D_7E8F9A0B1C2D_.wvu.FilterData\" localSheetId=\"0\" hidden=\"1\">Data!$A$1:$D$30</definedName>"
        "<definedName name=\"Z_5D7B1A2C_3E4F_4A5B_8C6D_7E8F9A0B1C2D_.wvu.PrintArea\" localSheetId=\"0\" hidden=\"1\">Data!$A$1:$D$20</definedName>"
        "<definedName name=\"Z_5D7B1A2C_3E4F_4A5B_8C6D_7E8F9A0B1C2D_.wvu.PrintTitles\" localSheetId=\"0\" hidden=\"1\">Data!$A:$A,Data!$1:$1</definedName>"
        "<definedName name=\"Z_5D7B1A2C_3E4F_4A5B_8C6D_7E8F9A0B1C2D_.wvu.Rows\" localSheetId=\"0\" hidden=\"1\">Data!$5:$6,Data!$9:$9</definedName>");
    book.afterCalcPr = QByteArrayLiteral(
        "<customWorkbookViews>"
        "<customWorkbookView name=\"Summary\" guid=\"{5D7B1A2C-3E4F-4A5B-8C6D-7E8F9A0B1C2D}\" maximized=\"1\""
        " xWindow=\"-8\" yWindow=\"-8\" windowWidth=\"1936\" windowHeight=\"1056\" activeSheetId=\"1\"/>"
        "<customWorkbookView name=\"Wide\" guid=\"{A1B2C3D4-E5F6-4789-ABCD-EF0123456789}\" includePrintSettings=\"0\""
        " includeHiddenRowCol=\"0\" windowWidth=\"1500\" windowHeight=\"900\" tabRatio=\"600\" activeSheetId=\"2\"/>"
        "</customWorkbookViews>");
    return book;
}

QList<std::pair<QString, QString>> pairs(std::initializer_list<std::pair<const char *, const char *>> list)
{
    QList<std::pair<QString, QString>> out;
    for (const auto &[name, value] : list) {
        out.append({QString::fromLatin1(name), QString::fromLatin1(value)});
    }
    return out;
}

// The element names of `parent`'s children, in order.
QStringList childNames(const XmlElement &parent)
{
    QStringList out;
    for (const XmlElement *child : parent.elements()) {
        out.append(child->name);
    }
    return out;
}

void compareViews(const Workbook &a, const Workbook &b)
{
    QCOMPARE(a.customViews, b.customViews);
    QCOMPARE(a.sheetCount(), b.sheetCount());
    for (qint32 s = 0; s < a.sheetCount(); ++s) {
        QCOMPARE(a.sheetAt(s).customViews().size(), b.sheetAt(s).customViews().size());
        for (qsizetype v = 0; v < a.sheetAt(s).customViews().size(); ++v) {
            const Sheet::CustomView &x = a.sheetAt(s).customViews().at(v);
            const Sheet::CustomView &y = b.sheetAt(s).customViews().at(v);
            QCOMPARE(x.guid, y.guid);
            QCOMPARE(x.pageSetup, y.pageSetup);
            QCOMPARE(x.autoFilter, y.autoFilter);
            QCOMPARE(x.attributes, y.attributes);
            QVERIFY2(x == y, qPrintable(x.guid));
        }
    }
}

} // namespace

class CalcIoViewsTest : public QObject {
    Q_OBJECT
private slots:
    void excelViewsReadIntoTheModel();
    void excelViewsSurviveASave();
    void viewsAddedInQindaCalcAreExcels();

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

void CalcIoViewsTest::excelViewsReadIntoTheModel()
{
    Workbook wb;
    QVERIFY(open(excelViews(), QStringLiteral("views.xlsx"), &wb));
    QCOMPARE(wb.customViews.size(), 2);
    const CustomViewSpec &summary = wb.customViews.at(0);
    QCOMPARE(summary.name, QStringLiteral("Summary"));
    QCOMPARE(summary.guid, kSummary);
    QVERIFY(summary.includePrintSettings && summary.includeHiddenRowCol);
    QCOMPARE(summary.activeSheet, QStringLiteral("Data"));
    QCOMPARE(summary.attributes, pairs({{"maximized", "1"}, {"xWindow", "-8"}, {"yWindow", "-8"},
                                         {"windowWidth", "1936"}, {"windowHeight", "1056"}}));
    const CustomViewSpec &wide = wb.customViews.at(1);
    QVERIFY(!wide.includePrintSettings && !wide.includeHiddenRowCol);
    QCOMPARE(wide.activeSheet, QStringLiteral("Notes"));
    // The view's names are the view's, not the workbook's.
    QVERIFY(wb.definedNames().isEmpty());

    const Sheet &data = wb.sheetAt(0);
    QCOMPARE(data.customViews().size(), 2);
    const Sheet::CustomView *s = customViewOf(data, kSummary);
    QVERIFY(s != nullptr);
    QCOMPARE(s->scale, 85);
    QCOMPARE(s->topLeft, (CellAddr{2, 0}));
    QCOMPARE(s->activeCell, (CellAddr{4, 2}));
    QCOMPARE(s->freezeRows, 1);
    QCOMPARE(s->freezeCols, 1);
    QCOMPARE(s->splitTopLeft, (CellAddr{3, 1})); // the frozen view scrolled to B4
    QCOMPARE(s->hiddenRows, (QList<qint32>{4, 5, 8}));
    QCOMPARE(s->hiddenColumns, (QList<qint32>{2}));
    QVERIFY(s->filter);
    QCOMPARE(s->autoFilter.range, (CellRange{{0, 0}, {29, 3}}));
    QCOMPARE(s->autoFilter.allowed.value(1), (QStringList{QStringLiteral("21"), QStringLiteral("31")}));
    QVERIFY(s->printSettings);
    QCOMPARE(s->pageSetup.orientation, 1);
    QCOMPARE(s->pageSetup.paperSize, 9);
    QVERIFY(s->pageSetup.fitToPage);
    QCOMPARE(s->pageSetup.marginLeft, 0.5);
    QCOMPARE(s->pageSetup.rowBreaks, (QList<qint32>{15}));
    QCOMPARE(*s->printArea, (CellRange{{0, 0}, {19, 3}}));
    QCOMPARE(s->printTitleRows->topLeft.row, 0);
    QCOMPARE(s->printTitleRows->bottomRight.row, 0);
    QCOMPARE(s->printTitleCols->topLeft.col, 0);
    QCOMPARE(s->printTitleCols->bottomRight.col, 0);
    QCOMPARE(s->attributes, pairs({{"showGridLines", "0"}, {"showPageBreaks", "1"}}));
    const Sheet::CustomView *w = customViewOf(data, kWide);
    QVERIFY(w != nullptr);
    QCOMPARE(w->scale, 60);
    QCOMPARE(w->splitX, 2400.0);
    QCOMPARE(w->splitY, 1800.0);
    QCOMPARE(w->splitTopLeft, (CellAddr{5, 2}));
    QCOMPARE(w->activeCell, (CellAddr{7, 3}));
    QCOMPARE(w->freezeRows, 0);
    QVERIFY(!w->printSettings);
    QVERIFY(w->hiddenRows.isEmpty());
    // The sheet's own settings are not the views'.
    QCOMPARE(data.pageSetup().orientation, 0);
    QVERIFY(!data.printArea().has_value());
    QCOMPARE(data.freezeRows(), 0);

    // Notes: hidden by Summary; the part no workbook view lists is dropped.
    const Sheet &notes = wb.sheetAt(1);
    QCOMPARE(notes.customViews().size(), 2);
    QVERIFY(customViewOf(notes, kSummary)->sheetHidden);
    QCOMPARE(customViewOf(notes, kWide)->activeCell, (CellAddr{2, 1}));

    // Show puts Summary's document part back.
    qint32 active = -1;
    QVERIFY(showCustomView(wb, kSummary, &active));
    QCOMPARE(active, 0);
    QCOMPARE(wb.sheetAt(0).freezeRows(), 1);
    QVERIFY(wb.sheetAt(0).outline().isHidden(OutlineAxis::Rows, 8));
    QVERIFY(wb.sheetAt(0).autoFilter().active);
    QCOMPARE(*wb.sheetAt(0).printArea(), (CellRange{{0, 0}, {19, 3}}));
    QCOMPARE(wb.sheetAt(0).pageSetup().orientation, 1);
    QVERIFY(wb.sheetAt(1).isHidden());
}

void CalcIoViewsTest::excelViewsSurviveASave()
{
    Workbook wb;
    QVERIFY(open(excelViews(), QStringLiteral("views.xlsx"), &wb));
    const auto saved = save(wb, QStringLiteral("views-saved.xlsx"));
    QVERIFY(saved);

    // workbook.xml: the views after calcPr, the names hidden and sheet-local.
    const auto book = saved->xml(QStringLiteral("xl/workbook.xml"));
    QVERIFY(book);
    const QStringList order = childNames(*book);
    QVERIFY2(order.indexOf(QStringLiteral("customWorkbookViews")) == order.indexOf(QStringLiteral("calcPr")) + 1,
             qPrintable(order.join(u',')));
    const XmlElement *views = book->child(sml(), u"customWorkbookViews");
    QCOMPARE(views->elements(sml(), u"customWorkbookView").size(), 2);
    const XmlElement *wide = views->elements(sml(), u"customWorkbookView").at(1);
    QCOMPARE(wide->attr(u"includePrintSettings"), QStringLiteral("0"));
    QCOMPARE(wide->attr(u"tabRatio"), QStringLiteral("600"));
    QCOMPARE(wide->attr(u"activeSheetId"), QStringLiteral("2"));
    const QList<SavedName> names = savedDefinedNames(*saved);
    QCOMPARE(names.size(), 5);
    for (const SavedName &name : names) {
        QVERIFY2(name.name.startsWith(QLatin1String("Z_5D7B1A2C_3E4F_4A5B_8C6D_7E8F9A0B1C2D_.wvu.")), qPrintable(name.name));
        QCOMPARE(name.localSheetId, QStringLiteral("0"));
        QCOMPARE(name.hidden, QStringLiteral("1"));
    }
    const auto rowsName = std::find_if(names.cbegin(), names.cend(), [](const SavedName &n) { return n.name.endsWith(u"Rows"); });
    QCOMPARE(rowsName->body, QStringLiteral("Data!$5:$6,Data!$9:$9"));

    // The sheet: customSheetViews in its slot, before mergeCells.
    const auto sheet = saved->xml(QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheet);
    const QStringList children = childNames(*sheet);
    QVERIFY2(children.indexOf(QStringLiteral("customSheetViews")) >= 0
                 && children.indexOf(QStringLiteral("customSheetViews")) < children.indexOf(QStringLiteral("mergeCells")),
             qPrintable(children.join(u',')));
    const XmlElement *frozen = sheet->child(sml(), u"customSheetViews")->elements(sml(), u"customSheetView").at(0);
    QCOMPARE(childNames(*frozen), (QStringList{QStringLiteral("pane"), QStringLiteral("selection"), QStringLiteral("rowBreaks"),
                                                QStringLiteral("pageMargins"), QStringLiteral("pageSetup"),
                                                QStringLiteral("autoFilter")}));
    QCOMPARE(frozen->attr(u"showGridLines"), QStringLiteral("0"));

    // The printer settings the sheet links are not the save's: byte for byte.
    QCOMPARE(saved->data(QStringLiteral("xl/printerSettings/printerSettings2.bin")), kPrinterBytes + "2");

    // Read back: the same views.
    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(path(QStringLiteral("views-saved.xlsx")), &back, &error), qPrintable(error));
    compareViews(wb, back);
    QVERIFY(back.definedNames().isEmpty());
}

void CalcIoViewsTest::viewsAddedInQindaCalcAreExcels()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Q1 Sales"));
    wb.addSheet(QStringLiteral("Q2"));
    for (qint32 r = 0; r < 12; ++r) {
        wb.setCellValue(0, {r, 0}, Value(double(r)));
        wb.setCellValue(1, {r, 0}, Value(double(r * 2)));
    }
    Sheet &sales = wb.sheetAt(0);
    SheetOutline outline = sales.outline();
    outline.setLine(OutlineAxis::Rows, 3, LineOutline{0, true, false});
    outline.setLine(OutlineAxis::Columns, 1, LineOutline{0, true, false});
    sales.setOutline(outline);
    sales.setPrintArea(CellRange{{0, 0}, {9, 2}});
    sales.setPrintTitles(CellRange{{0, 0}, {0, Sheet::kMaxCols - 1}}, std::nullopt);
    Sheet::PageSetup setup;
    setup.orientation = 1;
    sales.setPageSetup(setup);
    sales.setFreeze(1, 0);
    QHash<qint32, Sheet::CustomView> window;
    Sheet::CustomView split;
    split.scale = 120;
    split.splitX = 3000;
    split.splitTopLeft = {0, 4};
    split.activeCell = {2, 0};
    window.insert(1, split);
    const QString guid = addCustomView(wb, QStringLiteral("Review & print"), true, true, 1, window);
    const QString plain = addCustomView(wb, QStringLiteral("Plain"), false, false, 0, {});

    const auto saved = save(wb, QStringLiteral("made.xlsx"));
    QVERIFY(saved);
    const QList<SavedName> names = savedDefinedNames(*saved);
    QStringList kinds;
    for (const SavedName &name : names) {
        kinds.append(name.name.section(u'.', -1) + QLatin1Char('@') + name.localSheetId);
    }
    kinds.sort();
    // The view's four names on the first sheet (beside the sheet's own
    // _xlnm.Print_Area and _xlnm.Print_Titles); Plain has none.
    QVERIFY2(kinds.contains(QStringLiteral("Cols@0")) && kinds.contains(QStringLiteral("Rows@0"))
                 && kinds.contains(QStringLiteral("PrintArea@0")) && kinds.contains(QStringLiteral("PrintTitles@0")),
             qPrintable(kinds.join(u',')));
    const auto titles = std::find_if(names.cbegin(), names.cend(), [](const SavedName &n) { return n.name.endsWith(u"wvu.PrintTitles"); });
    QCOMPARE(titles->body, QStringLiteral("'Q1 Sales'!$1:$1"));
    const auto q2 = saved->xml(QStringLiteral("xl/worksheets/sheet2.xml"));
    const XmlElement *q2view = q2->child(sml(), u"customSheetViews")->elements(sml(), u"customSheetView").at(0);
    QCOMPARE(q2view->attr(u"scale"), QStringLiteral("120"));
    const XmlElement *pane = q2view->child(sml(), u"pane");
    QCOMPARE(pane->attr(u"state"), QStringLiteral("split"));
    QCOMPARE(pane->attr(u"xSplit"), QStringLiteral("3000"));
    QCOMPARE(pane->attr(u"topLeftCell"), QStringLiteral("E1"));
    QCOMPARE(pane->attr(u"activePane"), QStringLiteral("topRight"));
    QCOMPARE(q2view->child(sml(), u"selection")->attr(u"pane"), QStringLiteral("topRight"));

    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(path(QStringLiteral("made.xlsx")), &back, &error), qPrintable(error));
    compareViews(wb, back);
    QCOMPARE(back.customViews.at(0).name, QStringLiteral("Review & print"));
    QCOMPARE(back.customViews.at(0).activeSheet, QStringLiteral("Q2"));
    QCOMPARE(back.customViews.at(1).guid, plain);
    QCOMPARE(customViewOf(back.sheetAt(0), guid)->hiddenRows, (QList<qint32>{3}));
    QCOMPARE(customViewOf(back.sheetAt(0), guid)->freezeRows, 1);
}

QTEST_GUILESS_MAIN(CalcIoViewsTest)
#include "tst_calc_io_views.moc"
