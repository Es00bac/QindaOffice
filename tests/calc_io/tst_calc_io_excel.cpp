// SPDX-License-Identifier: GPL-3.0-or-later
// M61 (D-069) cell and formula interchange with Excel. Each workbook here is
// built by hand the way Microsoft Excel writes it (excel_package.h), loaded,
// checked for the types and values a user would see, saved, and checked
// again in the saved XML (excel_inspect.h) — then every saved package goes
// through the ECMA-376 schema gate.

#include "xlsx_bridge.h"
#include "xlsx_formula.h"

#include "calc_io_schema.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

#include <cmath>

using namespace QindaOffice::Calc;
using QindaOffice::Test::ExcelSheet;
using QindaOffice::Test::ExcelWorkbook;
using QindaOffice::Test::SavedCell;

class CalcIoExcelTest : public QObject {
    Q_OBJECT
private slots:
    void typedCellsLoadWithTheirTypes();
    void futureFunctionsEvaluateAndKeepTheirPrefix();
    void textIsWrittenAsText();
    void unparseableFormulasKeepTheirText();
    void sharedFormulasKeepTheirGrouping();
    void formulaSpellingsRoundTrip();
    void laterSheetsAreDependencies();

private:
    QTemporaryDir m_dir;
    QString writeSource(const QString &name, const ExcelWorkbook &book);
    static Value value(const Workbook &wb, qint32 sheet, const char *a1);
    static QString formulaText(const Workbook &wb, qint32 sheet, const char *a1);
};

QString CalcIoExcelTest::writeSource(const QString &name, const ExcelWorkbook &book)
{
    const QString path = m_dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(book.build());
    }
    return path;
}

static bool near(const Value &got, double want)
{
    return isNumber(got) && std::abs(std::get<double>(got) - want) <= 1e-9 * qMax(1.0, std::abs(want));
}

static bool near(double got, double want)
{
    return near(Value(got), want);
}

static CellAddr addr(const char *a1)
{
    const QString text = QString::fromLatin1(a1);
    int i = 0;
    while (i < text.size() && text.at(i).isLetter()) {
        ++i;
    }
    return {text.mid(i).toInt() - 1, colFromName(text.left(i)).value_or(0)};
}

Value CalcIoExcelTest::value(const Workbook &wb, qint32 sheet, const char *a1)
{
    const CellAddr at = addr(a1);
    return wb.cellValue(sheet, at.row, at.col);
}

QString CalcIoExcelTest::formulaText(const Workbook &wb, qint32 sheet, const char *a1)
{
    const Cell *cell = wb.sheetAt(sheet).cellAt(addr(a1));
    return cell != nullptr && cell->hasFormula() ? cell->formula : QStringLiteral("<no formula>");
}

// Excel writes numbers with no t attribute, strings through the shared
// string table (rich runs and phonetic guides included), and errors,
// booleans, formula string results and ISO dates with t="e"/"b"/"str"/"d".
// QXlsx keeps every untyped <v> as a QString, so before M61 a plain 5 read
// as the TEXT "5": ISTEXT(A1) was TRUE and SUM skipped it.
void CalcIoExcelTest::typedCellsLoadWithTheirTypes()
{
    ExcelWorkbook book;
    book.sharedStrings = {
        QByteArrayLiteral("<t>hello</t>"),
        QByteArrayLiteral("<r><t>Hel</t></r><r><rPr><b/><sz val=\"11\"/></rPr><t>lo</t></r>"),
        QByteArrayLiteral("<t>\xe6\x9d\xb1\xe4\xba\xac</t><rPh sb=\"0\" eb=\"2\"><t>\xe3\x83\x88\xe3\x82\xa6</t></rPh>"
                          "<phoneticPr fontId=\"0\" type=\"noConversion\"/>"),
        QByteArrayLiteral("<t>line_x000D_break</t>"),
    };
    book.sheets.append(ExcelSheet{QStringLiteral("Data"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>5</v></c><c r=\"B1\"><f>A1*2</f><v>10</v></c>"
        "<c r=\"C1\"><f>ISTEXT(A1)</f><v>0</v></c></row>"
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>0</v></c><c r=\"B2\" t=\"str\"><f>A2&amp;\"!\"</f><v>hello!</v></c>"
        "<c r=\"C2\" t=\"b\"><f>ISNUMBER(A1)</f><v>1</v></c></row>"
        "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>1</v></c><c r=\"B3\" t=\"e\"><f>1/0</f><v>#DIV/0!</v></c>"
        "<c r=\"C3\"><f>SUM(A1,A12,A15)</f><v>3.5025</v></c></row>"
        "<row r=\"4\"><c r=\"A4\" t=\"s\"><v>2</v></c><c r=\"B4\" t=\"b\"><f>A1&gt;3</f><v>1</v></c>"
        "<c r=\"C4\" t=\"b\"><f>ISERROR(A8)</f><v>1</v></c></row>"
        "<row r=\"5\"><c r=\"A5\" t=\"inlineStr\"><is><r><t>in</t></r><r><rPr><i/></rPr><t>line</t></r>"
        "<rPh sb=\"0\" eb=\"1\"><t>x</t></rPh></is></c><c r=\"C5\" t=\"b\"><f>ISTEXT(A8)</f><v>0</v></c></row>"
        "<row r=\"6\"><c r=\"A6\" t=\"inlineStr\"><is><t xml:space=\"preserve\"> padded </t></is></c></row>"
        "<row r=\"7\"><c r=\"A7\" t=\"b\"><v>1</v></c></row>"
        "<row r=\"8\"><c r=\"A8\" t=\"e\"><v>#DIV/0!</v></c></row>"
        "<row r=\"9\"><c r=\"A9\" t=\"e\"><v>#N/A</v></c></row>"
        "<row r=\"10\"><c r=\"A10\" s=\"1\"><v>45366</v></c></row>"
        "<row r=\"11\"><c r=\"A11\" t=\"str\"><v>plain</v></c></row>"
        "<row r=\"12\"><c r=\"A12\" t=\"n\"><v>2.5E-3</v></c></row>"
        "<row r=\"13\"><c r=\"A13\" s=\"1\" t=\"d\"><v>2024-03-15T00:00:00</v></c></row>"
        "<row r=\"14\"><c r=\"A14\" t=\"s\"><v>3</v></c></row>"
        "<row r=\"15\"><c r=\"A15\"><v>-1.5</v></c></row>"), {}, {}});
    const QString source = writeSource(QStringLiteral("typed.xlsx"), book);

    const auto check = [](const Workbook &wb) {
        QCOMPARE(value(wb, 0, "A1"), Value(5.0));
        QCOMPARE(value(wb, 0, "B1"), Value(10.0));
        QCOMPARE(value(wb, 0, "C1"), Value(false)); // ISTEXT of a number
        QCOMPARE(value(wb, 0, "A2"), Value(QStringLiteral("hello")));
        QCOMPARE(value(wb, 0, "B2"), Value(QStringLiteral("hello!")));
        QCOMPARE(value(wb, 0, "C2"), Value(true));
        QCOMPARE(value(wb, 0, "A3"), Value(QStringLiteral("Hello"))); // every rich run
        QCOMPARE(value(wb, 0, "B3"), Value(Err::Div0));
        QVERIFY(near(value(wb, 0, "C3"), 3.5025));
        QCOMPARE(value(wb, 0, "A4"), Value(QStringLiteral("東京"))); // not the phonetic guide
        QCOMPARE(value(wb, 0, "B4"), Value(true));
        QCOMPARE(value(wb, 0, "C4"), Value(true));
        QCOMPARE(value(wb, 0, "A5"), Value(QStringLiteral("inline"))); // all runs, not the last
        QCOMPARE(value(wb, 0, "C5"), Value(false)); // an error is not text
        QCOMPARE(value(wb, 0, "A6"), Value(QStringLiteral(" padded ")));
        QCOMPARE(value(wb, 0, "A7"), Value(true));
        QCOMPARE(value(wb, 0, "A8"), Value(Err::Div0));
        QCOMPARE(value(wb, 0, "A9"), Value(Err::NA));
        QCOMPARE(value(wb, 0, "A10"), Value(45366.0));
        QCOMPARE(value(wb, 0, "A11"), Value(QStringLiteral("plain")));
        QCOMPARE(value(wb, 0, "A12"), Value(0.0025));
        QCOMPARE(value(wb, 0, "A13"), Value(45366.0)); // ISO date → serial
        QCOMPARE(value(wb, 0, "A14"), Value(QStringLiteral("line\rbreak"))); // _x000D_ decoded
        QCOMPARE(value(wb, 0, "A15"), Value(-1.5));
    };

    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    check(wb);

    const QString saved = m_dir.filePath(QStringLiteral("typed-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const auto package = QindaOffice::Ooxml::Package::load(saved);
    QVERIFY(package);
    const QHash<QString, SavedCell> cells =
        QindaOffice::Test::savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    // Literal errors and every cached formula result carry their real type.
    QCOMPARE(cells.value(QStringLiteral("A8")).type, QStringLiteral("e"));
    QCOMPARE(cells.value(QStringLiteral("A8")).value, QStringLiteral("#DIV/0!"));
    QCOMPARE(cells.value(QStringLiteral("A9")).value, QStringLiteral("#N/A"));
    QCOMPARE(cells.value(QStringLiteral("A7")).type, QStringLiteral("b"));
    QCOMPARE(cells.value(QStringLiteral("B2")).type, QStringLiteral("str"));
    QCOMPARE(cells.value(QStringLiteral("B2")).value, QStringLiteral("hello!"));
    QCOMPARE(cells.value(QStringLiteral("B3")).type, QStringLiteral("e"));
    QCOMPARE(cells.value(QStringLiteral("B3")).value, QStringLiteral("#DIV/0!"));
    QCOMPARE(cells.value(QStringLiteral("B4")).type, QStringLiteral("b"));
    QCOMPARE(cells.value(QStringLiteral("B4")).value, QStringLiteral("1"));
    QVERIFY(near(cells.value(QStringLiteral("C3")).value.toDouble(), 3.5025));
    // The file spells the carriage return Excel's way (the reload below
    // decodes it again).
    QCOMPARE(cells.value(QStringLiteral("A14")).value, QStringLiteral("line_x000D_break"));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    check(back);
}

// Functions Excel added after 2007 are stored as _xlfn.NAME (FILTER and SORT
// as _xlfn._xlws.NAME, LET parameters as _xlpm.name). Read strips exactly the
// prefixes Excel documents so the engine evaluates; write puts them back on
// every function Excel requires them on — typed formulas included — and an
// unknown future function keeps its prefix verbatim both ways.
void CalcIoExcelTest::futureFunctionsEvaluateAndKeepTheirPrefix()
{
    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Calc"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>1</v></c><c r=\"B1\" t=\"str\"><f>_xlfn.IFS(A1&gt;2,\"big\",TRUE,\"small\")</f>"
        "<v>small</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>2</v></c><c r=\"B2\"><f>_xlfn.STDEV.S(A1:A3)</f><v>1</v></c></row>"
        "<row r=\"3\"><c r=\"A3\"><v>3</v></c><c r=\"B3\"><f>_xlfn.XLOOKUP(2,A1:A3,A1:A3)</f><v>2</v></c></row>"
        "<row r=\"4\"><c r=\"B4\"><f>_xlfn.LET(_xlpm.x,A3,_xlpm.x*2)</f><v>6</v></c></row>"
        "<row r=\"5\"><c r=\"B5\"><f>_xlfn._xlws.SORT(A1:A3)</f><v>1</v></c></row>"
        "<row r=\"6\"><c r=\"B6\"><f>_xlfn.FUTUREFN(1)</f><v>1</v></c></row>"
        "<row r=\"7\"><c r=\"B7\" t=\"str\"><f>\"_xlfn.IFS\"&amp;\"x\"</f><v>_xlfn.IFSx</v></c></row>"
        "<row r=\"8\"><c r=\"B8\" t=\"str\"><f>_xlfn.CONCAT(\"a\",_xlfn.IFNA(NA(),\"b\"))</f><v>ab</v></c></row>"
        "<row r=\"9\"><c r=\"B9\"><f>_xlfn.MAXIFS(A1:A3,A1:A3,\"&lt;3\")</f><v>2</v></c></row>"), {}, {}});
    const QString source = writeSource(QStringLiteral("future.xlsx"), book);

    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    QCOMPARE(value(wb, 0, "B1"), Value(QStringLiteral("small")));
    QCOMPARE(formulaText(wb, 0, "B1"), QStringLiteral("IFS(A1>2,\"big\",TRUE,\"small\")"));
    QCOMPARE(value(wb, 0, "B2"), Value(1.0));
    QCOMPARE(value(wb, 0, "B3"), Value(2.0));
    QCOMPARE(value(wb, 0, "B4"), Value(6.0));
    QCOMPARE(formulaText(wb, 0, "B4"), QStringLiteral("LET(x,A3,x*2)"));
    QCOMPARE(formulaText(wb, 0, "B5"), QStringLiteral("SORT(A1:A3)"));
    QCOMPARE(formulaText(wb, 0, "B6"), QStringLiteral("_xlfn.FUTUREFN(1)"));
    QCOMPARE(value(wb, 0, "B7"), Value(QStringLiteral("_xlfn.IFSx")));
    QCOMPARE(value(wb, 0, "B8"), Value(QStringLiteral("ab")));
    QCOMPARE(value(wb, 0, "B9"), Value(2.0));
    // Formulas a user types get the prefix on save too, and the ';' the
    // engine accepts as a separator never reaches the file.
    QVERIFY(wb.setCellInput(0, addr("C1"), QStringLiteral("=IFS(TRUE,1)")));
    QVERIFY(wb.setCellInput(0, addr("C2"), QStringLiteral("=SUM(A1;A2)")));
    QVERIFY(wb.setCellInput(0, addr("C3"), QStringLiteral("=concat(\"x\";\"y\")")));
    QCOMPARE(value(wb, 0, "C2"), Value(3.0));

    const QString saved = m_dir.filePath(QStringLiteral("future-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const auto package = QindaOffice::Ooxml::Package::load(saved);
    QVERIFY(package);
    const QHash<QString, SavedCell> cells =
        QindaOffice::Test::savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    const auto f = [&cells](const char *ref) { return cells.value(QString::fromLatin1(ref)).formula; };
    QCOMPARE(f("B1"), QStringLiteral("_xlfn.IFS(A1>2,\"big\",TRUE,\"small\")"));
    QCOMPARE(f("B2"), QStringLiteral("_xlfn.STDEV.S(A1:A3)"));
    QCOMPARE(f("B3"), QStringLiteral("_xlfn.XLOOKUP(2,A1:A3,A1:A3)"));
    QCOMPARE(f("B4"), QStringLiteral("_xlfn.LET(_xlpm.x,A3,_xlpm.x*2)"));
    QCOMPARE(f("B5"), QStringLiteral("_xlfn._xlws.SORT(A1:A3)"));
    QCOMPARE(f("B6"), QStringLiteral("_xlfn.FUTUREFN(1)"));
    QCOMPARE(f("B7"), QStringLiteral("\"_xlfn.IFS\"&\"x\""));
    QCOMPARE(f("B8"), QStringLiteral("_xlfn.CONCAT(\"a\",_xlfn.IFNA(NA(),\"b\"))"));
    QCOMPARE(f("B9"), QStringLiteral("_xlfn.MAXIFS(A1:A3,A1:A3,\"<3\")"));
    QCOMPARE(f("C1"), QStringLiteral("_xlfn.IFS(TRUE,1)"));
    QCOMPARE(f("C2"), QStringLiteral("SUM(A1,A2)"));
    QCOMPARE(f("C3"), QStringLiteral("_xlfn.CONCAT(\"x\",\"y\")"));
    for (const QString &formula : QindaOffice::Test::allSavedFormulas(*package)) {
        QVERIFY2(!formula.contains(QLatin1Char(';')), qPrintable(formula));
        QVERIFY2(!formula.contains(QLatin1String("_xlfn._xlfn.")), qPrintable(formula));
    }
    QCOMPARE(cells.value(QStringLiteral("B1")).type, QStringLiteral("str"));
    QCOMPARE(cells.value(QStringLiteral("B1")).value, QStringLiteral("small"));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QCOMPARE(value(back, 0, "B1"), Value(QStringLiteral("small")));
    QCOMPARE(formulaText(back, 0, "B4"), QStringLiteral("LET(x,A3,x*2)"));
    QCOMPARE(value(back, 0, "C1"), Value(1.0));
}

// Text is written as text. QXlsx turns any string that starts with '=' into
// a formula, anything URL-shaped into a blue hyperlink, and HTML-looking
// text into rich text; and characters XML cannot carry must use Excel's
// _xHHHH_ escape (a literal "_x000D_" in the text is itself escaped).
void CalcIoExcelTest::textIsWrittenAsText()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Text"));
    const QStringList texts{QStringLiteral("=1+2"),
                            QStringLiteral("http://example.com/page"),
                            QStringLiteral("mailto:someone@example.com"),
                            QStringLiteral("<b>not html</b>"),
                            QStringLiteral("007"),
                            QStringLiteral("literal _x000D_ text"),
                            QStringLiteral("tab\tand\x01" "control")};
    for (int row = 0; row < texts.size(); ++row) {
        wb.setCellValue(0, {row, 0}, texts.at(row));
    }
    const QString saved = m_dir.filePath(QStringLiteral("text-saved.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const auto package = QindaOffice::Ooxml::Package::load(saved);
    QVERIFY(package);
    const QString sheetPart = QStringLiteral("xl/worksheets/sheet1.xml");
    const QHash<QString, SavedCell> cells = QindaOffice::Test::savedCells(*package, sheetPart);
    for (int row = 0; row < texts.size(); ++row) {
        const SavedCell cell = cells.value(QStringLiteral("A%1").arg(row + 1));
        QVERIFY2(!cell.hasFormula, qPrintable(texts.at(row)));
        QCOMPARE(cell.type, QStringLiteral("s"));
    }
    QVERIFY(!package->data(sheetPart).contains("<hyperlinks"));
    for (const auto &rel : package->relationships(sheetPart)) {
        QVERIFY2(!rel.type.endsWith(QLatin1String("/hyperlink")), qPrintable(rel.target));
    }

    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    for (int row = 0; row < texts.size(); ++row) {
        QCOMPARE(back.cellValue(0, row, 0), Value(texts.at(row)));
        QVERIFY(!back.sheetAt(0).cellAt({row, 0})->hasFormula());
    }
}

// A formula the engine cannot parse (a structured table reference, the
// space intersection operator) used to load as the TEXT "=…", which the
// honest writer would then save as a string: the formula would be gone.
// It loads as a formula that shows #NAME?, keeps its text, and is written
// back as a formula — shared-formula dependents included, shifted per cell.
void CalcIoExcelTest::unparseableFormulasKeepTheirText()
{
    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Sheet1"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>2</v></c><c r=\"B1\"><f>Table1[[#This Row],[Qty]]*2</f><v>14</v></c>"
        "<c r=\"C1\"><f>VLOOKUP(2,$A:$A,1,FALSE)</f><v>2</v></c><c r=\"D1\"><f>SUM($A:$A)</f><v>9</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>3</v></c><c r=\"B2\"><f t=\"shared\" ref=\"B2:B4\" si=\"0\">SUM(A2:A4 A3:B3)</f>"
        "<v>3</v></c></row>"
        "<row r=\"3\"><c r=\"A3\"><v>4</v></c><c r=\"B3\"><f t=\"shared\" si=\"0\"/><v>4</v></c></row>"
        "<row r=\"4\"><c r=\"B4\"><f t=\"shared\" si=\"0\"/><v>0</v></c></row>"), {}, {}});
    const QString source = writeSource(QStringLiteral("unparsed.xlsx"), book);

    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    QCOMPARE(formulaText(wb, 0, "B1"), QStringLiteral("Table1[[#This Row],[Qty]]*2"));
    QCOMPARE(value(wb, 0, "B1"), Value(Err::Name));
    QCOMPARE(formulaText(wb, 0, "B2"), QStringLiteral("SUM(A2:A4 A3:B3)"));
    QCOMPARE(formulaText(wb, 0, "B3"), QStringLiteral("SUM(A3:A5 A4:B4)"));
    QCOMPARE(formulaText(wb, 0, "B4"), QStringLiteral("SUM(A4:A6 A5:B5)"));
    // Absolute whole-column references are Excel's everyday spelling.
    QCOMPARE(value(wb, 0, "C1"), Value(2.0));
    QCOMPARE(value(wb, 0, "D1"), Value(9.0));

    const QString saved = m_dir.filePath(QStringLiteral("unparsed-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const auto package = QindaOffice::Ooxml::Package::load(saved);
    QVERIFY(package);
    const QHash<QString, SavedCell> cells =
        QindaOffice::Test::savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(cells.value(QStringLiteral("B1")).formula, QStringLiteral("Table1[[#This Row],[Qty]]*2"));
    QCOMPARE(cells.value(QStringLiteral("B1")).type, QStringLiteral("e"));
    QCOMPARE(cells.value(QStringLiteral("B1")).value, QStringLiteral("#NAME?"));
    QCOMPARE(cells.value(QStringLiteral("B3")).formula, QStringLiteral("SUM(A3:A5 A4:B4)"));
    QCOMPARE(cells.value(QStringLiteral("C1")).formula, QStringLiteral("VLOOKUP(2,$A:$A,1,FALSE)"));
    QCOMPARE(cells.value(QStringLiteral("D1")).formula, QStringLiteral("SUM($A:$A)"));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QCOMPARE(formulaText(back, 0, "B4"), QStringLiteral("SUM(A4:A6 A5:B5)"));
    QCOMPARE(value(back, 0, "D1"), Value(9.0));
}

// Excel stores a filled-down column as one shared formula: the master's
// text plus a group index on every other cell. Those cells are rebuilt by
// shifting the master's tree and printing it again — and the printer must
// put back the parentheses the parser folded into the tree, or
// (A2+B2)*2 is saved as A2+B2*2.
void CalcIoExcelTest::sharedFormulasKeepTheirGrouping()
{
    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Sheet1"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>1</v></c><c r=\"B1\"><v>2</v></c>"
        "<c r=\"C1\"><f t=\"shared\" ref=\"C1:C3\" si=\"0\">(A1+B1)*2</f><v>6</v></c>"
        "<c r=\"D1\"><f t=\"shared\" ref=\"D1:D3\" si=\"1\">A1-(B1-A1)/(B1*2)^-(A1-2)</f><v>0</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>3</v></c><c r=\"B2\"><v>4</v></c><c r=\"C2\"><f t=\"shared\" si=\"0\"/><v>14</v></c>"
        "<c r=\"D2\"><f t=\"shared\" si=\"1\"/><v>0</v></c></row>"
        "<row r=\"3\"><c r=\"A3\"><v>5</v></c><c r=\"B3\"><v>6</v></c><c r=\"C3\"><f t=\"shared\" si=\"0\"/><v>22</v></c>"
        "<c r=\"D3\"><f t=\"shared\" si=\"1\"/><v>0</v></c></row>"), {}, {}});
    const QString source = writeSource(QStringLiteral("shared.xlsx"), book);

    // D: A - (B-A) / (2B)^(-(A-2)), worked by hand per row.
    const auto expectD = [](double a, double b) { return a - (b - a) / std::pow(b * 2, -(a - 2)); };
    const auto check = [&expectD](const Workbook &wb) {
        QCOMPARE(value(wb, 0, "C1"), Value(6.0));
        QCOMPARE(value(wb, 0, "C2"), Value(14.0));
        QCOMPARE(value(wb, 0, "C3"), Value(22.0));
        QVERIFY(near(value(wb, 0, "D2"), expectD(3, 4)));
        QVERIFY(near(value(wb, 0, "D3"), expectD(5, 6)));
    };
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    check(wb);
    QCOMPARE(formulaText(wb, 0, "C2"), QStringLiteral("(A2+B2)*2"));

    const QString saved = m_dir.filePath(QStringLiteral("shared-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    check(back);
}

// Summary comes before Detail in the file, the usual layout. While the load
// installed Summary!A1, Detail did not exist yet, so its reference recorded
// no dependency edge: the value then depended on hash order (the laptop
// survey saw a different count of #DIV/0! cells on every run), and an edit
// on Detail never reached Summary.
void CalcIoExcelTest::laterSheetsAreDependencies()
{
    ExcelWorkbook book;
    book.sheets.append(ExcelSheet{QStringLiteral("Summary"), QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><f>Detail!B2*2</f><v>24</v></c></row>"), {}, {}});
    book.sheets.append(ExcelSheet{QStringLiteral("Detail"), QByteArrayLiteral(
        "<row r=\"2\"><c r=\"B2\"><f>SUM(B3:B4)</f><v>12</v></c></row>"
        "<row r=\"3\"><c r=\"B3\"><v>5</v></c></row><row r=\"4\"><c r=\"B4\"><v>7</v></c></row>"), {}, {}});
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(writeSource(QStringLiteral("later.xlsx"), book), &wb, &error), qPrintable(error));
    QCOMPARE(value(wb, 0, "A1"), Value(24.0));
    QVERIFY(wb.setCellInput(1, addr("B3"), QStringLiteral("10")));
    QCOMPARE(value(wb, 1, "B2"), Value(17.0));
    QCOMPARE(value(wb, 0, "A1"), Value(34.0));
}

// The spelling transforms themselves, both directions: nested LET and a
// LAMBDA called through its LET name, structured references whose column
// names must never be prefixed or shifted, array constants whose ';' is a
// row separator, string literals and quoted sheet names left alone.
void CalcIoExcelTest::formulaSpellingsRoundTrip()
{
    const QList<QPair<QString, QString>> both = {
        {QStringLiteral("IFS(A1>2,\"big\",TRUE,\"small\")"), QStringLiteral("_xlfn.IFS(A1>2,\"big\",TRUE,\"small\")")},
        {QStringLiteral("LET(x,1,LET(y,x+1,x*y))"),
         QStringLiteral("_xlfn.LET(_xlpm.x,1,_xlfn.LET(_xlpm.y,_xlpm.x+1,_xlpm.x*_xlpm.y))")},
        {QStringLiteral("LET(f,LAMBDA(n,n*2),f(3))"),
         QStringLiteral("_xlfn.LET(_xlpm.f,_xlfn.LAMBDA(_xlpm.n,_xlpm.n*2),_xlpm.f(3))")},
        {QStringLiteral("SUM(Table1[Qty])+LET(Qty,1,Qty)"),
         QStringLiteral("SUM(Table1[Qty])+_xlfn.LET(_xlpm.Qty,1,_xlpm.Qty)")},
        {QStringLiteral("FILTER(A1:A3,B1:B3>0)"), QStringLiteral("_xlfn._xlws.FILTER(A1:A3,B1:B3>0)")},
        {QStringLiteral("\"_xlfn.IFS(\"&'My Sheet'!A1"), QStringLiteral("\"_xlfn.IFS(\"&'My Sheet'!A1")},
        {QStringLiteral("_xlfn.UNKNOWNFN(1)"), QStringLiteral("_xlfn.UNKNOWNFN(1)")},
        {QStringLiteral("SUM({1,2;3,4})"), QStringLiteral("SUM({1,2;3,4})")},
        {QStringLiteral("STDEV.S(A1:A9)+STDEV(A1:A9)"), QStringLiteral("_xlfn.STDEV.S(A1:A9)+STDEV(A1:A9)")},
    };
    for (const auto &[engine, file] : both) {
        QCOMPARE(IO::formulaForFile(engine), file);
        QCOMPARE(IO::formulaFromFile(file), engine);
    }
    // One way: the engine's ';' separator and lower-case known names.
    QCOMPARE(IO::formulaForFile(QStringLiteral("sum(A1;A2)")), QStringLiteral("SUM(A1,A2)"));
    QCOMPARE(IO::formulaForFile(QStringLiteral("_XLFN.UNKNOWNFN(1)")), QStringLiteral("_xlfn.UNKNOWNFN(1)"));
    // A shifted copy moves cell references only, never a column name.
    QCOMPARE(IO::shiftedFormulaText(QStringLiteral("Table1[Q1]+Q1+$A$1+'Q 1'!B2"), 1, 1),
             QStringLiteral("Table1[Q1]+R2+$A$1+'Q 1'!C3"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoExcelTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_excel.moc"
