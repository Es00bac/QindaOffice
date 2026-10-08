// SPDX-License-Identifier: GPL-3.0-or-later
// M61 (D-069) package-level interchange: what Excel needs around the cells.
// The source's theme and document properties survive a save; defined names
// keep their scope, constants and formulas; sheet names are quoted wherever
// a reference spells them; table column names match their header cells;
// long validation lists become range references; comments carry the VML
// Excel draws them with; and a drawing holding a dialog chart and a picture
// declares every prefix it uses. Every saved package passes the schema gate.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using QindaOffice::Ooxml::Package;
using QindaOffice::Test::ExcelSheet;
using QindaOffice::Test::ExcelWorkbook;
using QindaOffice::Test::SavedName;

class CalcIoPackageTest : public QObject {
    Q_OBJECT
private slots:
    void themeAndPropertiesSurvive();
    void definedNamesKeepScopeAndBodies();
    void sheetNamesAreQuotedInReferences();
    void tableColumnsMatchTheirHeaders();
    void longValidationListsBecomeRanges();
    void commentsCarryLegacyVml();
    void dialogChartAndPictureShareAValidDrawing();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    QString writeSource(const QString &name, const ExcelWorkbook &book) const
    {
        QFile file(path(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(book.build());
        }
        return path(name);
    }
};

static std::shared_ptr<const Package> saveAndLoad(const Workbook &wb, const QString &file)
{
    QString error;
    if (!IO::writeXlsxFile(wb, file, &error)) {
        qWarning("write failed: %s", qPrintable(error));
        return nullptr;
    }
    return Package::load(file, &error);
}

static const SavedName *findName(const QList<SavedName> &names, const QString &name,
                                 const QString &localSheetId = QString())
{
    for (const SavedName &entry : names) {
        if (entry.name == name && entry.localSheetId == localSheetId) {
            return &entry;
        }
    }
    return nullptr;
}

// D-068 rule 2 for Calc: the parts the engine does not model are carried
// from the package the workbook came from. QXlsx writes its own fixed theme,
// claims "Microsoft Excel" as the application and "Qt Xlsx Library" as the
// author; a save keeps the source's theme, title and author instead and
// names QindaCalc as the application.
void CalcIoPackageTest::themeAndPropertiesSurvive()
{
    Workbook plain;
    plain.addSheet(QStringLiteral("Sheet1"));
    const auto stock = saveAndLoad(plain, path(QStringLiteral("stock.xlsx")));
    QVERIFY(stock);
    QQO_VERIFY_XLSX_SCHEMA(path(QStringLiteral("stock.xlsx")));
    for (const QString &part : stock->partNames()) {
        QVERIFY2(!stock->data(part).contains("Qt Xlsx Library"), qPrintable(part));
    }
    QVERIFY(stock->data(QStringLiteral("docProps/app.xml")).contains("QindaCalc"));

    // A theme of the author's own: the stock XML renamed and recoloured.
    QByteArray theme = stock->data(QStringLiteral("xl/theme/theme1.xml"));
    QVERIFY(theme.contains("4F81BD"));
    theme.replace("4F81BD", "123456");
    const int nameAt = theme.indexOf(" name=\"") + 7;
    theme.replace(nameAt, theme.indexOf('"', nameAt) - nameAt, "Quinda Test Theme");

    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Budget"),
                                  QByteArrayLiteral("<row r=\"1\"><c r=\"A1\"><v>1</v></c></row>"), {}, {}});
    book.themeXml = theme;
    book.coreXml = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>Quarterly Budget</dc:title>"
        "<dc:creator>Alice Example</dc:creator><cp:keywords>budget</cp:keywords>"
        "<dcterms:created xsi:type=\"dcterms:W3CDTF\">2023-01-02T03:04:05Z</dcterms:created>"
        "</cp:coreProperties>");
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(writeSource(QStringLiteral("themed.xlsx"), book), &wb, &error),
             qPrintable(error));
    // The source travels with the workbook object, not with a file the save
    // would have to find again (the app's undo snapshots carry it too).
    const Workbook moved = std::move(wb);
    const auto saved = saveAndLoad(moved, path(QStringLiteral("themed-saved.xlsx")));
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(path(QStringLiteral("themed-saved.xlsx")));
    const QByteArray savedTheme = saved->data(QStringLiteral("xl/theme/theme1.xml"));
    QVERIFY(savedTheme.contains("Quinda Test Theme"));
    QVERIFY(savedTheme.contains("123456"));
    const QByteArray core = saved->data(QStringLiteral("docProps/core.xml"));
    QVERIFY2(core.contains("Quarterly Budget"), core.constData());
    QVERIFY2(core.contains("Alice Example"), core.constData());
    QVERIFY2(core.contains("budget"), core.constData());
    QVERIFY2(core.contains("2023-01-02T03:04:05Z"), core.constData());
    const QByteArray app = saved->data(QStringLiteral("docProps/app.xml"));
    QVERIFY2(app.contains("QindaCalc") && !app.contains("Microsoft Excel"), app.constData());
    for (const QString &part : saved->partNames()) {
        QVERIFY2(!saved->data(part).contains("Qt Xlsx Library"), qPrintable(part));
    }
}

// Defined names are more than workbook-wide ranges: a name can be scoped to
// one sheet (localSheetId), hold a constant or a formula, be hidden, carry a
// comment. Each kind evaluates where the engine can, and every one of them
// is written back with its scope and body — none is dropped.
void CalcIoPackageTest::definedNamesKeepScopeAndBodies()
{
    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Sheet1"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>1</v></c><c r=\"C1\"><f>SUM(Total)</f></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>2</v></c><c r=\"C2\"><f>A1*Rate</f></c></row>"
        "<row r=\"3\"><c r=\"A3\"><v>3</v></c><c r=\"C3\"><f>Doubled+1</f></c></row>"
        "<row r=\"4\"><c r=\"C4\"><f>Weekly</f></c></row>"), {}, {}});
    book.sheets.append(ExcelSheet{QStringLiteral("Data"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"B1\"><v>10</v></c><c r=\"C1\"><f>SUM(Total)</f></c></row>"
        "<row r=\"2\"><c r=\"B2\"><v>20</v></c></row><row r=\"3\"><c r=\"B3\"><v>30</v></c></row>"), {}, {}});
    book.definedNames = QByteArrayLiteral(
        "<definedName name=\"_xlnm._FilterDatabase\" localSheetId=\"0\" hidden=\"1\">Sheet1!$A$1:$A$3</definedName>"
        "<definedName name=\"Doubled\">Sheet1!$A$1*2</definedName>"
        "<definedName name=\"Later\" hidden=\"1\" comment=\"kept\">_xlfn.FUTUREFN(1)</definedName>"
        "<definedName name=\"Rate\">0.07</definedName>"
        "<definedName name=\"Total\">Sheet1!$A$1:$A$3</definedName>"
        "<definedName name=\"Total\" localSheetId=\"1\">Data!$B$1:$B$3</definedName>"
        "<definedName name=\"Weekly\">_xlfn.STDEV.S(Sheet1!$A$1:$A$3)</definedName>");
    const auto check = [](const Workbook &wb) {
        QCOMPARE(wb.cellValue(0, 0, 2), Value(6.0));   // Sheet1: the workbook Total
        QCOMPARE(wb.cellValue(1, 0, 2), Value(60.0));  // Data: its own Total
        QCOMPARE(wb.cellValue(0, 1, 2), Value(0.07));  // a constant name
        QCOMPARE(wb.cellValue(0, 2, 2), Value(3.0));   // a formula name
        QCOMPARE(wb.cellValue(0, 3, 2), Value(1.0));   // prefixed function in a name
    };
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(writeSource(QStringLiteral("names.xlsx"), book), &wb, &error),
             qPrintable(error));
    check(wb);

    const QString file = path(QStringLiteral("names-saved.xlsx"));
    const auto saved = saveAndLoad(wb, file);
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(file);
    const QList<SavedName> names = QindaOffice::Test::savedDefinedNames(*saved);
    QCOMPARE(names.size(), 7);
    const SavedName *rate = findName(names, QStringLiteral("Rate"));
    QVERIFY(rate != nullptr);
    QCOMPARE(rate->body, QStringLiteral("0.07"));
    const SavedName *total = findName(names, QStringLiteral("Total"));
    QVERIFY(total != nullptr);
    QCOMPARE(total->body, QStringLiteral("Sheet1!$A$1:$A$3"));
    const SavedName *local = findName(names, QStringLiteral("Total"), QStringLiteral("1"));
    QVERIFY(local != nullptr);
    QCOMPARE(local->body, QStringLiteral("Data!$B$1:$B$3"));
    const SavedName *doubled = findName(names, QStringLiteral("Doubled"));
    QVERIFY(doubled != nullptr);
    QCOMPARE(doubled->body, QStringLiteral("Sheet1!$A$1*2"));
    const SavedName *later = findName(names, QStringLiteral("Later"));
    QVERIFY(later != nullptr);
    QCOMPARE(later->body, QStringLiteral("_xlfn.FUTUREFN(1)"));
    QCOMPARE(later->hidden, QStringLiteral("1"));
    QCOMPARE(later->comment, QStringLiteral("kept"));
    const SavedName *filter = findName(names, QStringLiteral("_xlnm._FilterDatabase"), QStringLiteral("0"));
    QVERIFY(filter != nullptr);
    QCOMPARE(filter->hidden, QStringLiteral("1"));
    const SavedName *weekly = findName(names, QStringLiteral("Weekly"));
    QVERIFY(weekly != nullptr);
    QCOMPARE(weekly->body, QStringLiteral("_xlfn.STDEV.S(Sheet1!$A$1:$A$3)"));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(file, &back, &error), qPrintable(error));
    check(back);
}

// A sheet name that is not a plain identifier — "2024", "Q1 Sales", "It's"
// — must be quoted wherever a reference spells it: print areas, print
// titles, defined names and formulas. Unquoted, Excel rejects the name and
// repairs the workbook.
void CalcIoPackageTest::sheetNamesAreQuotedInReferences()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("2024"));
    Sheet &sales = wb.addSheet(QStringLiteral("Q1 Sales"));
    wb.addSheet(QStringLiteral("It's"));
    for (int row = 0; row < 3; ++row) {
        wb.setCellValue(1, {row, 0}, double(row + 1));
    }
    wb.sheetAt(0).setPrintArea(CellRange{{0, 0}, {1, 1}});
    sales.setPrintTitles(CellRange{{0, 0}, {0, Sheet::kMaxCols - 1}},
                         CellRange{{0, 0}, {Sheet::kMaxRows - 1, 0}});
    RangeRef target;
    target.sheet = QStringLiteral("Q1 Sales");
    target.range = {{0, 0}, {2, 0}};
    target.absRow0 = target.absCol0 = target.absRow1 = target.absCol1 = true;
    wb.defineName(QStringLiteral("Sales"), target);
    RangeRef quoteRef = target;
    quoteRef.sheet = QStringLiteral("It's");
    quoteRef.range = {{0, 0}, {0, 0}};
    wb.defineName(QStringLiteral("Corner"), quoteRef);
    QVERIFY(wb.setCellInput(0, {0, 2}, QStringLiteral("=SUM(Sales)")));
    QCOMPARE(wb.cellValue(0, 0, 2), Value(6.0));

    const QString file = path(QStringLiteral("quoted.xlsx"));
    const auto saved = saveAndLoad(wb, file);
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(file);
    const QList<SavedName> names = QindaOffice::Test::savedDefinedNames(*saved);
    const SavedName *area = findName(names, QStringLiteral("_xlnm.Print_Area"), QStringLiteral("0"));
    QVERIFY(area != nullptr);
    QCOMPARE(area->body, QStringLiteral("'2024'!$A$1:$B$2"));
    const SavedName *titles = findName(names, QStringLiteral("_xlnm.Print_Titles"), QStringLiteral("1"));
    QVERIFY(titles != nullptr);
    QCOMPARE(titles->body, QStringLiteral("'Q1 Sales'!$A:$A,'Q1 Sales'!$1:$1"));
    const SavedName *salesName = findName(names, QStringLiteral("Sales"));
    QVERIFY(salesName != nullptr);
    QCOMPARE(salesName->body, QStringLiteral("'Q1 Sales'!$A$1:$A$3"));
    const SavedName *corner = findName(names, QStringLiteral("Corner"));
    QVERIFY(corner != nullptr);
    QCOMPARE(corner->body, QStringLiteral("'It''s'!$A$1"));

    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(file, &back, &error), qPrintable(error));
    QVERIFY(back.sheetAt(0).printArea().has_value());
    QCOMPARE(back.sheetAt(0).printArea()->bottomRight, (CellAddr{1, 1}));
    QVERIFY(back.sheetAt(1).printTitleRows().has_value());
    QVERIFY(back.sheetAt(1).printTitleCols().has_value());
    QCOMPARE(back.cellValue(0, 0, 2), Value(6.0));
}

// Excel requires every <tableColumn name> to equal its header cell's text,
// uniquely; a stale or duplicate name makes it repair the file and drop the
// table. Names come from the header cells at write time, and a header that
// is blank, a number or a duplicate is written as the text Excel would show.
void CalcIoPackageTest::tableColumnsMatchTheirHeaders()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Table"));
    wb.setCellValue(0, {0, 0}, QStringLiteral("Name"));
    wb.setCellValue(0, {0, 1}, QStringLiteral("name"));
    wb.setCellValue(0, {0, 2}, 2024.0);
    for (int row = 1; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            wb.setCellValue(0, {row, col}, double(row * 10 + col));
        }
    }
    TableSpec table;
    table.name = QStringLiteral("Results");
    table.range = {{0, 0}, {2, 3}};
    table.columnNames = {QStringLiteral("Old"), QStringLiteral("Older"), QStringLiteral("Stale"),
                         QStringLiteral("Gone")};
    sheet.setTables({table});

    const QString file = path(QStringLiteral("table.xlsx"));
    const auto saved = saveAndLoad(wb, file);
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(file);
    const QString s = QindaOffice::Test::sml();
    const auto tablePart = saved->xml(QStringLiteral("xl/tables/table1.xml"));
    QVERIFY(tablePart);
    QStringList columns;
    for (const auto *column : tablePart->child(s, u"tableColumns")->elements(s, u"tableColumn")) {
        columns.append(column->attr(u"name"));
    }
    const QStringList expected{QStringLiteral("Name"), QStringLiteral("name2"), QStringLiteral("2024"),
                               QStringLiteral("Column4")};
    QCOMPARE(columns, expected);
    const auto cells = QindaOffice::Test::savedCells(*saved, QStringLiteral("xl/worksheets/sheet1.xml"));
    for (int col = 0; col < 4; ++col) {
        const auto cell = cells.value(colName(col) + QStringLiteral("1"));
        QCOMPARE(cell.type, QStringLiteral("s"));
        QCOMPARE(cell.value, expected.at(col));
    }
    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(file, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).tables().value(0).columnNames, expected);
}

// Excel refuses an inline list longer than 255 characters (and an inline
// item cannot contain a comma at all). Such a list is written as a range
// reference — the source's own range when it came from one, else a hidden
// sheet of list values — and a range-sourced list keeps its live reference
// even when the range lives on a later sheet.
void CalcIoPackageTest::longValidationListsBecomeRanges()
{
    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Form"), QByteArrayLiteral("<row r=\"1\"><c r=\"A1\"><v>1</v></c></row>"),
                                  QByteArrayLiteral("<dataValidations count=\"1\"><dataValidation type=\"list\" "
                                                    "allowBlank=\"1\" showErrorMessage=\"1\" sqref=\"C1:C4\">"
                                                    "<formula1>Lists!$A$1:$A$3</formula1></dataValidation>"
                                                    "</dataValidations>"), {}});
    book.sheets.append(ExcelSheet{QStringLiteral("Lists"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>Red</t></is></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"inlineStr\"><is><t>Green</t></is></c></row>"
        "<row r=\"3\"><c r=\"A3\" t=\"inlineStr\"><is><t>Blue</t></is></c></row>"), {}, {}});
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(writeSource(QStringLiteral("lists.xlsx"), book), &wb, &error),
             qPrintable(error));
    QCOMPARE(wb.sheetAt(0).validations().size(), 1);
    QCOMPARE(wb.sheetAt(0).validations().at(0).list,
             (QStringList{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}));

    QStringList many;
    for (int i = 1; i <= 80; ++i) {
        many.append(QStringLiteral("Item%1").arg(i, 3, 10, QLatin1Char('0')));
    }
    QList<ValidationRule> rules = wb.sheetAt(0).validations();
    ValidationRule longRule;
    longRule.kind = ValidationRule::Kind::List;
    longRule.range = {{0, 3}, {4, 3}};
    longRule.list = many;
    rules.append(longRule);
    ValidationRule commaRule = longRule;
    commaRule.range = {{0, 4}, {4, 4}};
    commaRule.list = {QStringLiteral("a,b"), QStringLiteral("c")};
    rules.append(commaRule);
    ValidationRule shortRule = longRule;
    shortRule.range = {{0, 5}, {4, 5}};
    shortRule.list = {QStringLiteral("Yes"), QStringLiteral("No")};
    rules.append(shortRule);
    wb.sheetAt(0).setValidations(rules);

    const QString file = path(QStringLiteral("lists-saved.xlsx"));
    const auto saved = saveAndLoad(wb, file);
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(file);
    const QString s = QindaOffice::Test::sml();
    const auto sheet = saved->xml(QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheet);
    QHash<QString, QString> formulaBySqref;
    for (const auto *rule : sheet->child(s, u"dataValidations")->elements(s, u"dataValidation")) {
        formulaBySqref.insert(rule->attr(u"sqref"), rule->child(s, u"formula1")->text());
    }
    QCOMPARE(formulaBySqref.value(QStringLiteral("C1:C4")), QStringLiteral("Lists!$A$1:$A$3"));
    QCOMPARE(formulaBySqref.value(QStringLiteral("F1:F5")), QStringLiteral("\"Yes,No\""));
    for (const QString &sqref : {QStringLiteral("D1:D5"), QStringLiteral("E1:E5")}) {
        const QString formula = formulaBySqref.value(sqref);
        QVERIFY2(!formula.startsWith(QLatin1Char('"')) && formula.contains(QLatin1Char('!'))
                     && formula.size() <= 255,
                 qPrintable(formula));
    }
    Workbook back;
    QVERIFY2(IO::readXlsxFile(file, &back, &error), qPrintable(error));
    const QList<ValidationRule> &read = back.sheetAt(0).validations();
    QCOMPARE(read.size(), 4);
    for (const ValidationRule &rule : read) {
        if (rule.range.topLeft.col == 3) {
            QCOMPARE(rule.list, many);
        } else if (rule.range.topLeft.col == 4) {
            QCOMPARE(rule.list, commaRule.list);
        }
    }
}

// Excel draws a cell's note from the sheet's legacy VML drawing; a comments
// part without one loads, but its notes cannot be seen or opened.
void CalcIoPackageTest::commentsCarryLegacyVml()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Notes"));
    wb.setCellValue(0, {0, 0}, 1.0);
    sheet.setComments({{CellAddr{0, 0}.key(), CellComment{QStringLiteral("Ann"), QStringLiteral("First")}},
                       {CellAddr{2, 2}.key(), CellComment{QStringLiteral("Bo"), QStringLiteral("Second")}}});
    const QString file = path(QStringLiteral("notes.xlsx"));
    const auto saved = saveAndLoad(wb, file);
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(file);
    const QString sheetPart = QStringLiteral("xl/worksheets/sheet1.xml");
    QString vmlPart;
    QString vmlRid;
    for (const auto &rel : saved->relationships(sheetPart)) {
        if (rel.type.endsWith(QLatin1String("/vmlDrawing"))) {
            vmlPart = saved->targetPart(sheetPart, rel);
            vmlRid = rel.id;
        }
    }
    QVERIFY2(!vmlPart.isEmpty(), "no vmlDrawing relationship from the sheet");
    QVERIFY(saved->contentType(vmlPart).contains(QLatin1String("vmlDrawing")));
    const auto vml = saved->xml(vmlPart);
    QVERIFY2(vml, "the VML part is not well-formed XML");
    const QByteArray vmlBytes = saved->data(vmlPart);
    QCOMPARE(vmlBytes.count("ObjectType=\"Note\""), 2);
    QVERIFY(vmlBytes.contains("<x:Row>2</x:Row>"));
    QVERIFY(vmlBytes.contains("<x:Column>2</x:Column>"));
    const auto sheetXml = saved->xml(sheetPart);
    const auto *legacy = sheetXml->child(QindaOffice::Test::sml(), u"legacyDrawing");
    QVERIFY(legacy != nullptr);
    QCOMPARE(legacy->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"), vmlRid);

    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(file, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).comments().size(), 2);

    // A drawing owns blocks of 1024 shape ids: 1500 notes take two, and the
    // next sheet's drawing starts after them.
    Workbook many;
    Sheet &crowded = many.addSheet(QStringLiteral("Crowded"));
    Sheet &next = many.addSheet(QStringLiteral("Next"));
    QHash<quint64, CellComment> notes;
    for (qint32 row = 0; row < 1500; ++row) {
        notes.insert(CellAddr{row, 0}.key(), CellComment{QStringLiteral("Ann"), QStringLiteral("n")});
    }
    crowded.setComments(notes);
    next.setComments({{CellAddr{0, 0}.key(), CellComment{QStringLiteral("Bo"), QStringLiteral("x")}}});
    const QString manyFile = path(QStringLiteral("many-notes.xlsx"));
    const auto manySaved = saveAndLoad(many, manyFile);
    QVERIFY(manySaved);
    QQO_VERIFY_XLSX_SCHEMA(manyFile);
    QVERIFY(manySaved->data(QStringLiteral("xl/drawings/vmlDrawing1.vml")).contains("data=\"1,2\""));
    QVERIFY(manySaved->data(QStringLiteral("xl/drawings/vmlDrawing3.vml")).contains("data=\"3\""));
}

// A chart inserted through the dialog goes through QXlsx, whose drawing root
// declares xdr and a but not r; the picture anchor the post-pass merged into
// it used r:embed anyway, so the drawing was not even well-formed XML.
void CalcIoPackageTest::dialogChartAndPictureShareAValidDrawing()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    for (int row = 0; row < 4; ++row) {
        wb.setCellInput(0, {row, 0}, QStringLiteral("Cat %1").arg(row));
        wb.setCellInput(0, {row, 1}, QString::number(row * 3));
    }
    ChartSpec chart;
    chart.kind = ChartSpec::Kind::Bar;
    chart.title = QStringLiteral("Dialog chart");
    chart.rangeText = QStringLiteral("A1:B4");
    chart.sheet = 0;
    wb.charts.append(chart);
    QImage pixels(8, 8, QImage::Format_RGB32);
    pixels.fill(Qt::darkCyan);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    pixels.save(&buffer, "PNG");
    ImageSpec image;
    image.bytes = QSharedPointer<const QByteArray>::create(png);
    image.sheet = 0;
    image.anchorRow = 12;
    image.anchorCol = 1;
    image.widthPx = 32;
    image.heightPx = 32;
    wb.images.append(image);

    const QString file = path(QStringLiteral("drawing.xlsx"));
    const auto saved = saveAndLoad(wb, file);
    QVERIFY(saved);
    QQO_VERIFY_XLSX_SCHEMA(file);
    const QStringList drawings = saved->partNames().filter(QStringLiteral("xl/drawings/drawing"));
    QCOMPARE(drawings.size(), 1);
    QString parseError;
    QVERIFY2(saved->xml(drawings.first(), &parseError), qPrintable(parseError));
    Workbook back;
    QString error;
    QVERIFY2(IO::readXlsxFile(file, &back, &error), qPrintable(error));
    QCOMPARE(back.images.size(), 1);
    QCOMPARE(back.charts.size(), 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoPackageTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_package.moc"
