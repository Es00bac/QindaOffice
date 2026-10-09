// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): scenarios and data tables as Excel writes them. A package
// built by hand the way Excel stores them — a one-variable table down a
// column with two formulas, one across a row, a two-variable table, each
// with its top-left cell's <f t="dataTable" …> and stale cached results,
// and two scenarios — opens with the tables worked out and the scenarios in
// the model; our save writes them back in Excel's markup (the master
// cells' attributes exactly, a text result as t="str", every package
// schema-valid) and re-reads equal; tables and scenarios made in QindaCalc
// are written the same way; and LibreOffice 25.2, which imports Excel's
// data tables as MULTIPLE.OPERATIONS, re-opens a written table with the
// same numbers (skipped without soffice).

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "excel_inspect.h"
#include "excel_package.h"
#include "support/soffice_env.h"

#include "data_table.h"
#include "scenario.h"
#include "workbook.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;
using QindaOffice::Test::ExcelSheet;
using QindaOffice::Test::ExcelWorkbook;
using QindaOffice::Test::savedCells;
using QindaOffice::Test::sml;

namespace {

QString a1(int row, int col)
{
    return QString(QChar(u'A' + col)) + QString::number(row + 1);
}

CellAddr at(const char *ref)
{
    const QString text = QString::fromLatin1(ref);
    return {text.mid(1).toInt() - 1, text.at(0).unicode() - 'A'};
}

QByteArray cell(const char *ref, const char *value, const char *formula = nullptr)
{
    return QByteArrayLiteral("<c r=\"") + ref + QByteArrayLiteral("\">")
        + (formula != nullptr ? QByteArray(formula) : QByteArray()) + QByteArrayLiteral("<v>") + value
        + QByteArrayLiteral("</v></c>");
}

// Excel's markup: model inputs A1 and B1; a column table A3:C6 (formulas
// B3:C3, inputs A4:A6, results B4:C6); a two-variable table E3:H6 (corner
// formula, row inputs F3:H3 for A1, column inputs E4:E6 for B1); a row
// table A8:D9 (inputs B8:D8, formula A9). Every cached result is 0: the
// numbers have to come from the engine.
ExcelWorkbook excelBook()
{
    ExcelWorkbook book;
    ExcelSheet sheet;
    sheet.name = QStringLiteral("Model");
    QByteArray rows;
    rows += "<row r=\"1\">" + cell("A1", "2") + cell("B1", "3") + "</row>";
    rows += "<row r=\"3\">" + cell("B3", "20", "<f>A1*10</f>") + cell("C3", "102", "<f>A1+100</f>")
        + cell("E3", "6", "<f>A1*B1</f>") + cell("F3", "1") + cell("G3", "2") + cell("H3", "3") + "</row>";
    rows += "<row r=\"4\">" + cell("A4", "1")
        + cell("B4", "0", "<f t=\"dataTable\" ref=\"B4:C6\" dt2D=\"0\" dtr=\"0\" r1=\"A1\"/>") + cell("C4", "0")
        + cell("E4", "10") + cell("F4", "0", "<f t=\"dataTable\" ref=\"F4:H6\" dt2D=\"1\" dtr=\"1\" r1=\"A1\" r2=\"B1\"/>")
        + cell("G4", "0") + cell("H4", "0") + "</row>";
    rows += "<row r=\"5\">" + cell("A5", "2") + cell("B5", "0") + cell("C5", "0") + cell("E5", "20") + cell("F5", "0")
        + cell("G5", "0") + cell("H5", "0") + "</row>";
    rows += "<row r=\"6\">" + cell("A6", "3") + cell("B6", "0") + cell("C6", "0") + cell("E6", "30") + cell("F6", "0")
        + cell("G6", "0") + cell("H6", "0") + "</row>";
    rows += "<row r=\"8\">" + cell("B8", "4") + cell("C8", "5") + cell("D8", "6") + "</row>";
    rows += "<row r=\"9\">" + cell("A9", "4", "<f>A1*2</f>")
        + cell("B9", "0", "<f t=\"dataTable\" ref=\"B9:D9\" dt2D=\"0\" dtr=\"1\" r1=\"A1\"/>") + cell("C9", "0")
        + cell("D9", "0") + "</row>";
    sheet.sheetData = rows;
    sheet.afterData = QByteArrayLiteral(
        "<scenarios current=\"1\" show=\"0\" sqref=\"B3 C3\">"
        "<scenario name=\"Best\" locked=\"1\" count=\"2\" user=\"Pat Doe\" comment=\"Created by Pat Doe on 10/9/2026\">"
        "<inputCells r=\"A1\" val=\"7\"/><inputCells r=\"B1\" val=\"0.25\"/></scenario>"
        "<scenario name=\"Worst\" hidden=\"1\" count=\"1\"><inputCells r=\"A1\" val=\"1\"/></scenario>"
        "</scenarios>");
    book.sheets.append(sheet);
    return book;
}

double number(const Workbook &wb, const char *ref)
{
    return toNumber(wb.cellValue(0, at(ref).row, at(ref).col)).value_or(-1);
}

// The <f> of every cell that has one, by reference.
QHash<QString, const XmlElement *> formulas(const XmlElement &root)
{
    QHash<QString, const XmlElement *> out;
    const XmlElement *data = root.child(sml(), u"sheetData");
    for (const XmlElement *row : data != nullptr ? data->elements(sml(), u"row") : QList<const XmlElement *>()) {
        for (const XmlElement *c : row->elements(sml(), u"c")) {
            if (const XmlElement *f = c->child(sml(), u"f")) {
                out.insert(c->attr(u"r"), f);
            }
        }
    }
    return out;
}

} // namespace

class CalcIoWhatIfTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void excelTablesAndScenariosRead();
    void savedAsExcelWritesThem();
    void tablesMadeHereAreWritten();
    void libreOfficeReopensWrittenTables();

private:
    QTemporaryDir m_dir;
    bool m_soffice = false;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool readFixture(Workbook *wb);
    QString saveChecked(const Workbook &wb, const QString &name);
};

void CalcIoWhatIfTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QProcess probe;
    QindaOffice::Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    m_soffice = probe.waitForFinished(15000) && probe.exitCode() == 0;
}

bool CalcIoWhatIfTest::readFixture(Workbook *wb)
{
    QFile file(path(QStringLiteral("excel.xlsx")));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(excelBook().build());
    file.close();
    if (!QindaOffice::Test::xlsxSchemaErrors(path(QStringLiteral("excel.xlsx"))).isEmpty()) {
        qWarning("the fixture is not schema-valid: %s",
                 qPrintable(QindaOffice::Test::xlsxSchemaErrors(path(QStringLiteral("excel.xlsx")))));
        return false;
    }
    QString error;
    if (!IO::readXlsxFile(path(QStringLiteral("excel.xlsx")), wb, &error)) {
        qWarning("read failed: %s", qPrintable(error));
        return false;
    }
    return true;
}

QString CalcIoWhatIfTest::saveChecked(const Workbook &wb, const QString &name)
{
    QString error;
    if (!IO::writeXlsxFile(wb, path(name), &error)) {
        qWarning("write failed: %s", qPrintable(error));
        return {};
    }
    return path(name);
}

void CalcIoWhatIfTest::excelTablesAndScenariosRead()
{
    Workbook wb;
    QVERIFY(readFixture(&wb));
    const QList<DataTableSpec> &tables = wb.sheetAt(0).dataTables();
    QCOMPARE(tables.size(), 3);
    QCOMPARE(tables.at(0).ref, (CellRange{at("B4"), at("C6")}));
    QVERIFY(!tables.at(0).twoD && !tables.at(0).rowInput);
    QCOMPARE(tables.at(0).r1, at("A1"));
    QCOMPARE(tables.at(1).ref, (CellRange{at("F4"), at("H6")}));
    QVERIFY(tables.at(1).twoD);
    QCOMPARE(tables.at(1).r1, at("A1")); // the ROW input cell
    QCOMPARE(tables.at(1).r2, at("B1")); // the COLUMN input cell
    QVERIFY(tables.at(2).rowInput && !tables.at(2).twoD);
    // Worked out on open, as LibreOffice and Excel show them.
    const QList<QPair<const char *, double>> expected{{"B4", 10},  {"C4", 101}, {"B6", 30}, {"C6", 103}, {"F4", 10},
                                                      {"H4", 30},  {"G5", 40},  {"H6", 90}, {"B9", 8},   {"D9", 12}};
    for (const auto &[ref, value] : expected) {
        QVERIFY2(number(wb, ref) == value, ref);
    }
    QCOMPARE(number(wb, "A1"), 2.0);
    const ScenarioSet &set = wb.sheetAt(0).scenarios();
    QCOMPARE(set.scenarios.size(), 2);
    QCOMPARE(set.current, 1);
    QCOMPARE(set.shown, 0);
    QCOMPARE(set.resultCells, (QList<CellRange>{{at("B3"), at("B3")}, {at("C3"), at("C3")}}));
    const Scenario &best = set.scenarios.at(0);
    QCOMPARE(best.name, QStringLiteral("Best"));
    QCOMPARE(best.cells, (QList<CellAddr>{at("A1"), at("B1")}));
    QCOMPARE(best.values, (QStringList{QStringLiteral("7"), QStringLiteral("0.25")}));
    QVERIFY(best.locked && !best.hidden);
    QCOMPARE(best.user, QStringLiteral("Pat Doe"));
    QCOMPARE(best.comment, QStringLiteral("Created by Pat Doe on 10/9/2026"));
    QVERIFY(!set.scenarios.at(1).locked && set.scenarios.at(1).hidden);
}

void CalcIoWhatIfTest::savedAsExcelWritesThem()
{
    Workbook wb;
    QVERIFY(readFixture(&wb));
    const QString saved = saveChecked(wb, QStringLiteral("saved.xlsx"));
    QVERIFY(!saved.isEmpty());
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QString error;
    const std::shared_ptr<const Package> package = Package::load(saved, &error);
    QVERIFY2(package != nullptr, qPrintable(error));
    const QindaOffice::Ooxml::XmlElementPtr root = package->xml(QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(root != nullptr);
    const QHash<QString, const XmlElement *> f = formulas(*root);
    const auto attributes = [&f](const char *ref) {
        QStringList out;
        if (const XmlElement *element = f.value(QString::fromLatin1(ref))) {
            for (const auto &attribute : element->attributes) {
                out.append(attribute.name + QLatin1Char('=') + attribute.value);
            }
        }
        return out.join(QLatin1Char(' '));
    };
    QCOMPARE(attributes("B4"), QStringLiteral("t=dataTable ref=B4:C6 dt2D=0 dtr=0 r1=A1"));
    QCOMPARE(attributes("F4"), QStringLiteral("t=dataTable ref=F4:H6 dt2D=1 dtr=1 r1=A1 r2=B1"));
    QCOMPARE(attributes("B9"), QStringLiteral("t=dataTable ref=B9:D9 dt2D=0 dtr=1 r1=A1"));
    QVERIFY(!f.contains(QStringLiteral("C4")) && !f.contains(QStringLiteral("G5"))); // only the top-left cell
    const QHash<QString, QindaOffice::Test::SavedCell> cells = savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(cells.value(QStringLiteral("B4")).value, QStringLiteral("10"));
    QCOMPARE(cells.value(QStringLiteral("H6")).value, QStringLiteral("90"));
    QCOMPARE(cells.value(QStringLiteral("D9")).value, QStringLiteral("12"));
    // The scenarios, untouched: what was read, in Excel's spelling.
    const XmlElement *scenarios = root->child(sml(), u"scenarios");
    QVERIFY(scenarios != nullptr);
    QCOMPARE(scenarios->attr(u"current"), QStringLiteral("1"));
    QCOMPARE(scenarios->attr(u"show"), QStringLiteral("0"));
    QCOMPARE(scenarios->attr(u"sqref"), QStringLiteral("B3 C3"));
    const QList<const XmlElement *> list = scenarios->elements(sml(), u"scenario");
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0)->attr(u"locked"), QStringLiteral("1"));
    QCOMPARE(list.at(0)->attr(u"count"), QStringLiteral("2"));
    QCOMPARE(list.at(0)->attr(u"user"), QStringLiteral("Pat Doe"));
    QVERIFY(!list.at(1)->hasAttribute(QString(), u"locked"));
    QCOMPARE(list.at(1)->attr(u"hidden"), QStringLiteral("1"));
    QCOMPARE(list.at(0)->elements(sml(), u"inputCells").at(1)->attr(u"val"), QStringLiteral("0.25"));
    // Read back: the same model.
    Workbook again;
    QVERIFY(IO::readXlsxFile(saved, &again, &error));
    QCOMPARE(again.sheetAt(0).dataTables(), wb.sheetAt(0).dataTables());
    QCOMPARE(again.sheetAt(0).scenarios(), wb.sheetAt(0).scenarios());
    QCOMPARE(number(again, "G5"), 40.0);
}

void CalcIoWhatIfTest::tablesMadeHereAreWritten()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Plan"));
    QVERIFY(wb.setCellInput(0, at("A1"), QStringLiteral("1")));
    QVERIFY(wb.setCellInput(0, at("B3"), QStringLiteral("=IF(A1>1,\"big\",\"small\")")));
    QVERIFY(wb.setCellInput(0, at("C3"), QStringLiteral("=A1/(A1-2)")));
    QVERIFY(wb.setCellInput(0, at("A4"), QStringLiteral("1")));
    QVERIFY(wb.setCellInput(0, at("A5"), QStringLiteral("2")));
    const std::optional<DataTableSpec> spec = makeDataTable({at("A3"), at("C5")}, CellAddr{}, at("A1"));
    QVERIFY(spec.has_value());
    wb.sheetAt(0).setDataTables({*spec});
    ScenarioSet set;
    Scenario odd;
    odd.name = QStringLiteral("Q1 & Q2 <\"best\">");
    odd.cells = {at("A1")};
    odd.values = {QStringLiteral("2")};
    odd.comment = QStringLiteral("two lines\nof notes");
    set.scenarios.append(odd);
    wb.sheetAt(0).setScenarios(set);
    wb.recalcAll();
    QCOMPARE(wb.cellValue(0, 3, 1), Value(QStringLiteral("small")));
    QCOMPARE(wb.cellValue(0, 4, 1), Value(QStringLiteral("big")));
    QCOMPARE(wb.cellValue(0, 4, 2), Value(Err::Div0));
    const QString saved = saveChecked(wb, QStringLiteral("made.xlsx"));
    QVERIFY(!saved.isEmpty());
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QString error;
    const std::shared_ptr<const Package> package = Package::load(saved, &error);
    QVERIFY2(package != nullptr, qPrintable(error));
    const QHash<QString, QindaOffice::Test::SavedCell> cells = savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    // The master cell: the table's formula and its text result as t="str".
    QVERIFY(cells.value(QStringLiteral("B4")).hasFormula);
    QCOMPARE(cells.value(QStringLiteral("B4")).type, QStringLiteral("str"));
    QCOMPARE(cells.value(QStringLiteral("B4")).value, QStringLiteral("small"));
    QCOMPARE(cells.value(QStringLiteral("B5")).value, QStringLiteral("big"));
    QCOMPARE(cells.value(QStringLiteral("C5")).type, QStringLiteral("e"));
    QCOMPARE(cells.value(QStringLiteral("C5")).value, QStringLiteral("#DIV/0!"));
    Workbook again;
    QVERIFY(IO::readXlsxFile(saved, &again, &error));
    QCOMPARE(again.sheetAt(0).dataTables(), wb.sheetAt(0).dataTables());
    QCOMPARE(again.sheetAt(0).scenarios().scenarios.value(0).name, odd.name);
    QCOMPARE(again.sheetAt(0).scenarios().scenarios.value(0).comment, odd.comment);
    QCOMPARE(again.cellValue(0, 4, 1), Value(QStringLiteral("big")));
}

void CalcIoWhatIfTest::libreOfficeReopensWrittenTables()
{
    if (!m_soffice) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    Workbook wb;
    QVERIFY(readFixture(&wb));
    // An edit first, so the numbers LibreOffice shows are the new ones.
    QVERIFY(wb.setCellInput(0, at("H3"), QStringLiteral("4")));
    QVERIFY(wb.setCellInput(0, at("A6"), QStringLiteral("5")));
    const QString saved = saveChecked(wb, QStringLiteral("libre.xlsx"));
    QVERIFY(!saved.isEmpty());
    QProcess soffice;
    QindaOffice::Test::startSoffice(
        soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nologo"),
                  QStringLiteral("-env:UserInstallation=file://%1/profile").arg(m_dir.path()), QStringLiteral("--convert-to"),
                  QStringLiteral("csv"), QStringLiteral("--outdir"), path(QStringLiteral("csv")), saved});
    QVERIFY(soffice.waitForFinished(180000));
    QFile csv(path(QStringLiteral("csv/libre.csv")));
    if (!csv.open(QIODevice::ReadOnly)) {
        QSKIP("LibreOffice cannot convert in this environment");
    }
    QHash<QString, QString> shown;
    const QList<QByteArray> lines = csv.readAll().split('\n');
    for (int r = 0; r < lines.size(); ++r) {
        const QList<QByteArray> fields = lines.at(r).trimmed().split(',');
        for (int c = 0; c < fields.size(); ++c) {
            shown.insert(a1(r, c), QString::fromUtf8(fields.at(c)));
        }
    }
    // LibreOffice works the tables out itself from our <f t="dataTable">:
    // its numbers are ours.
    for (const char *ref : {"B4", "C4", "B6", "C6", "F4", "H4", "G5", "H6", "B9", "D9"}) {
        QCOMPARE(shown.value(QString::fromLatin1(ref)), toText(wb.cellValue(0, at(ref).row, at(ref).col)));
    }
    QCOMPARE(shown.value(QStringLiteral("H6")), QStringLiteral("120"));
    QCOMPARE(shown.value(QStringLiteral("C6")), QStringLiteral("105"));
}

QTEST_GUILESS_MAIN(CalcIoWhatIfTest)
#include "tst_calc_io_whatif.moc"
