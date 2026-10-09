// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): Excel tables, structured references and array formulas as
// Excel stores them. A table part with a calculated column, a totals row
// (totalsRowFunction / totalsRowLabel) and filter buttons; cells holding
// Table[[#This Row],[Col]], [#Totals], [#All], [#Headers] and column-range
// references — written by Excel fully qualified, read into the formula
// bar's spelling ([@Col] inside the table), evaluated, and written back
// exactly as Excel spells them; a legacy array formula (<f t="array"> with
// no cm) kept as one; _xlfn.SINGLE as '@'; array constants. Every package
// is built by hand the way Excel writes it (excel_package.h) and every
// saved one validates against the ECMA-376 schemas.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_package.h"
#include "m69_fixtures.h"

#include "structure.h"
#include "table_edit.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;

class CalcIoStructuredTest : public QObject {
    Q_OBJECT
private slots:
    void readsAndEvaluatesExcelTables();
    void writesWhatExcelWrites();
    void editsTravelToTheFile();
    void arrayFormulasAndConstants();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool open(const ExcelWorkbook &book, const QString &name, Workbook *wb);
    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name);
};

namespace {

QString formula(const Workbook &wb, qint32 sheet, qint32 row, qint32 col)
{
    const Cell *cell = wb.sheetAt(sheet).cellAt({row, col});
    return cell != nullptr ? cell->formula : QString();
}

// The saved table part's attributes we check, per column name.
QHash<QString, QStringList> savedColumns(const Package &package, const QString &tablePart)
{
    QHash<QString, QStringList> out;
    const auto root = package.xml(tablePart);
    const QindaOffice::Ooxml::XmlElement *columns = root ? root->child(sml(), u"tableColumns") : nullptr;
    for (const auto *column : columns != nullptr ? columns->elements(sml(), u"tableColumn")
                                                 : QList<const QindaOffice::Ooxml::XmlElement *>()) {
        const auto *calculated = column->child(sml(), u"calculatedColumnFormula");
        out.insert(column->attr(u"name"), {column->attr(u"totalsRowFunction"), column->attr(u"totalsRowLabel"),
                                           calculated != nullptr ? calculated->text() : QString()});
    }
    return out;
}

// The cell's <f> t and ref attributes and its cm, by A1 reference.
QStringList arrayAttributes(const Package &package, const QString &part, const QString &ref)
{
    const auto root = package.xml(part);
    const auto *data = root ? root->child(sml(), u"sheetData") : nullptr;
    for (const auto *r : data != nullptr ? data->elements(sml(), u"row") : QList<const QindaOffice::Ooxml::XmlElement *>()) {
        for (const auto *c : r->elements(sml(), u"c")) {
            const auto *f = c->child(sml(), u"f");
            if (c->attr(u"r") == ref && f != nullptr) {
                return {f->attr(u"t"), f->attr(u"ref"), c->attr(u"cm")};
            }
        }
    }
    return {};
}

} // namespace

bool CalcIoStructuredTest::open(const ExcelWorkbook &book, const QString &name, Workbook *wb)
{
    QFile file(path(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(book.build());
    file.close();
    QString error;
    const bool ok = IO::readXlsxFile(path(name), wb, &error);
    if (!ok) {
        qWarning("read: %s", qPrintable(error));
    }
    return ok;
}

std::shared_ptr<const Package> CalcIoStructuredTest::save(const Workbook &wb, const QString &name)
{
    QString error;
    if (!IO::writeXlsxFile(wb, path(name), &error)) {
        qWarning("write: %s", qPrintable(error));
        return nullptr;
    }
    const QString schema = xlsxSchemaErrors(path(name));
    if (!schema.isEmpty()) {
        qWarning("schema: %s", qPrintable(schema));
        return nullptr;
    }
    keepForInspection(path(name));
    return Package::load(path(name));
}

void CalcIoStructuredTest::readsAndEvaluatesExcelTables()
{
    Workbook wb;
    QVERIFY(open(ordersBook(), QStringLiteral("orders.xlsx"), &wb));
    const TableSpec table = wb.sheetAt(0).tables().value(0);
    QCOMPARE(table.name, QStringLiteral("Orders"));
    QVERIFY(table.totalsRow);
    QVERIFY(table.autoFilter);
    QCOMPARE(table.calculatedColumns.value(3), QStringLiteral("[@Qty]*[@Price]"));
    // Inside the table: the formula bar's spelling, unqualified.
    QCOMPARE(formula(wb, 0, 1, 3), QStringLiteral("[@Qty]*[@Price]"));
    QCOMPARE(formula(wb, 0, 4, 1), QStringLiteral("SUBTOTAL(109,[Qty])"));
    // Elsewhere: qualified, '@' for this row and for SINGLE.
    QCOMPARE(formula(wb, 1, 2, 1), QStringLiteral("Orders[@Price]"));
    QCOMPARE(formula(wb, 1, 2, 2), QStringLiteral("@Orders[Qty]"));
    QCOMPARE(formula(wb, 1, 1, 0), QStringLiteral("Orders[[#Totals],[Qty]]"));
    const QList<std::pair<CellAddr, double>> expected{
        {{1, 3}, 20}, {{2, 3}, 20}, {{3, 3}, 21}, {{4, 1}, 10}, {{4, 3}, 61}};
    for (const auto &[at, number] : expected) {
        QCOMPARE(wb.cellValue(0, at.row, at.col), Value(number));
    }
    const QList<std::pair<CellAddr, double>> report{{{0, 0}, 61}, {{1, 0}, 10}, {{2, 0}, 5}, {{2, 1}, 4},
                                                    {{2, 2}, 5},  {{3, 0}, 3},  {{4, 0}, 31}, {{6, 0}, 10},
                                                    {{0, 4}, 4},  {{1, 4}, 10}, {{2, 4}, 6},  {{0, 5}, 8}};
    for (const auto &[at, number] : report) {
        QVERIFY2(wb.cellValue(1, at.row, at.col) == Value(number), qPrintable(addrText(at)));
    }
    // The legacy array formula stays one, over its block.
    QCOMPARE(wb.arrayBlockAt(1, {1, 4}), std::optional<CellRange>(CellRange{{0, 4}, {2, 4}}));
    QVERIFY(!wb.sheetAt(1).cellAt({0, 4})->dynamicArray);
}

void CalcIoStructuredTest::writesWhatExcelWrites()
{
    Workbook wb;
    QVERIFY(open(ordersBook(), QStringLiteral("orders-in.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("orders-out.xlsx"));
    QVERIFY(package);
    QVERIFY2(brokenWiring(*package).isEmpty(), qPrintable(brokenWiring(*package).join(QLatin1Char('\n'))));
    const auto orders = savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(orders.value(QStringLiteral("D3")).formula,
             QStringLiteral("Orders[[#This Row],[Qty]]*Orders[[#This Row],[Price]]"));
    QCOMPARE(orders.value(QStringLiteral("B5")).formula, QStringLiteral("SUBTOTAL(109,Orders[Qty])"));
    QCOMPARE(orders.value(QStringLiteral("D5")).value, QStringLiteral("61"));
    const auto report = savedCells(*package, QStringLiteral("xl/worksheets/sheet2.xml"));
    QCOMPARE(report.value(QStringLiteral("B3")).formula, QStringLiteral("Orders[[#This Row],[Price]]"));
    QCOMPARE(report.value(QStringLiteral("C3")).formula, QStringLiteral("_xlfn.SINGLE(Orders[Qty])"));
    QCOMPARE(report.value(QStringLiteral("A4")).formula, QStringLiteral("COUNTA(Orders[[#Headers],[Region]:[Price]])"));
    QCOMPARE(report.value(QStringLiteral("A7")).formula, QStringLiteral("SUM({1,2;3,4})"));
    // The legacy array formula: t="array" over its block, no cm.
    const QString part2 = QStringLiteral("xl/worksheets/sheet2.xml");
    QCOMPARE(arrayAttributes(*package, part2, QStringLiteral("E1")),
             (QStringList{QStringLiteral("array"), QStringLiteral("E1:E3"), QString()}));
    QCOMPARE(report.value(QStringLiteral("E1")).formula, QStringLiteral("Orders[Qty]*2"));
    QCOMPARE(arrayAttributes(*package, part2, QStringLiteral("F1")),
             (QStringList{QStringLiteral("array"), QStringLiteral("F1"), QString()}));
    QCOMPARE(report.value(QStringLiteral("E2")).value, QStringLiteral("10"));
    // The table part: filter buttons, totals functions and label, the
    // calculated column in Excel's spelling.
    const QStringList tables = partsByType(*package, QStringLiteral("xl/worksheets/sheet1.xml"), officeRel("table"));
    QCOMPARE(tables.size(), 1);
    const QByteArray tableXml = package->data(tables.first());
    QVERIFY(tableXml.contains("<autoFilter ref=\"A1:D4\"/>"));
    const QHash<QString, QStringList> columns = savedColumns(*package, tables.first());
    QCOMPARE(columns.value(QStringLiteral("Region")), (QStringList{QString(), QStringLiteral("Total"), QString()}));
    QCOMPARE(columns.value(QStringLiteral("Qty")), (QStringList{QStringLiteral("sum"), QString(), QString()}));
    QCOMPARE(columns.value(QStringLiteral("Total")),
             (QStringList{QStringLiteral("sum"), QString(),
                          QStringLiteral("Orders[[#This Row],[Qty]]*Orders[[#This Row],[Price]]")}));
    // And it reads back as it was.
    Workbook back;
    QVERIFY(IO::readXlsxFile(path(QStringLiteral("orders-out.xlsx")), &back));
    QCOMPARE(formula(back, 0, 1, 3), QStringLiteral("[@Qty]*[@Price]"));
    QCOMPARE(back.sheetAt(0).tables().value(0).calculatedColumns.value(3), QStringLiteral("[@Qty]*[@Price]"));
    QCOMPARE(back.cellValue(1, 0, 0), Value(61.0));
    QCOMPARE(back.cellValue(1, 0, 5), Value(8.0));
    QCOMPARE(formula(back, 1, 2, 2), QStringLiteral("@Orders[Qty]"));
}

void CalcIoStructuredTest::editsTravelToTheFile()
{
    Workbook wb;
    QVERIFY(open(ordersBook(), QStringLiteral("edit-in.xlsx"), &wb));
    // A row inserted inside the table: the table grows, the new row is
    // calculated, the totals row moves down.
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 2, 1});
    wb.setCellValue(0, {2, 1}, 1.0);
    wb.setCellValue(0, {2, 2}, 9.0);
    QCOMPARE(wb.cellValue(0, 2, 3), Value(9.0));
    QCOMPARE(wb.cellValue(1, 0, 0), Value(70.0));
    // A header retyped: the column and every reference to it are renamed.
    QVERIFY(setTableAwareInput(wb, 0, {0, 1}, QStringLiteral("Units")));
    QCOMPARE(formula(wb, 1, 4, 0), QStringLiteral("SUM(Orders[[Units]:[Price]])"));
    const auto package = save(wb, QStringLiteral("edit-out.xlsx"));
    QVERIFY(package);
    const auto orders = savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(orders.value(QStringLiteral("D3")).formula,
             QStringLiteral("Orders[[#This Row],[Units]]*Orders[[#This Row],[Price]]"));
    QCOMPARE(orders.value(QStringLiteral("B6")).formula, QStringLiteral("SUBTOTAL(109,Orders[Units])"));
    const QStringList tables = partsByType(*package, QStringLiteral("xl/worksheets/sheet1.xml"), officeRel("table"));
    QCOMPARE(tables.size(), 1);
    const QByteArray tableXml = package->data(tables.first());
    QVERIFY(tableXml.contains("ref=\"A1:D6\""));
    QVERIFY(tableXml.contains("<autoFilter ref=\"A1:D5\"/>"));
    QVERIFY(tableXml.contains("name=\"Units\" totalsRowFunction=\"sum\""));
    const auto report = savedCells(*package, QStringLiteral("xl/worksheets/sheet2.xml"));
    QCOMPARE(report.value(QStringLiteral("A5")).formula, QStringLiteral("SUM(Orders[[Units]:[Price]])"));
    QCOMPARE(report.value(QStringLiteral("A1")).value, QStringLiteral("70"));
}

void CalcIoStructuredTest::arrayFormulasAndConstants()
{
    // A Ctrl+Shift+Enter formula made in QindaCalc saves as Excel's.
    Workbook wb;
    wb.addSheet(QStringLiteral("Sheet1"));
    for (qint32 r = 0; r < 3; ++r) {
        wb.setCellValue(0, {r, 0}, double(r + 1));
    }
    const ParseResult parsed = parseFormula(QStringLiteral("A1:A3*{10;100;1000}"));
    QVERIFY(parsed.error.isEmpty());
    wb.installArrayFormula(0, {{0, 1}, {2, 1}}, parsed.root, QStringLiteral("A1:A3*{10;100;1000}"));
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=@A1:A3+1")));
    const auto package = save(wb, QStringLiteral("cse.xlsx"));
    QVERIFY(package);
    QCOMPARE(arrayAttributes(*package, QStringLiteral("xl/worksheets/sheet1.xml"), QStringLiteral("B1")),
             (QStringList{QStringLiteral("array"), QStringLiteral("B1:B3"), QString()}));
    const auto cells = savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(cells.value(QStringLiteral("B1")).formula, QStringLiteral("A1:A3*{10;100;1000}"));
    QCOMPARE(cells.value(QStringLiteral("B3")).value, QStringLiteral("3000"));
    QCOMPARE(cells.value(QStringLiteral("D1")).formula, QStringLiteral("_xlfn.SINGLE(A1:A3)+1"));
    Workbook back;
    QVERIFY(IO::readXlsxFile(path(QStringLiteral("cse.xlsx")), &back));
    QCOMPARE(back.arrayBlockAt(0, {2, 1}), std::optional<CellRange>(CellRange{{0, 1}, {2, 1}}));
    QCOMPARE(back.cellValue(0, 1, 1), Value(200.0));
    QCOMPARE(formula(back, 0, 0, 3), QStringLiteral("@A1:A3+1"));
    QCOMPARE(back.cellValue(0, 0, 3), Value(2.0));
}

QTEST_GUILESS_MAIN(CalcIoStructuredTest)
#include "tst_calc_io_structured.moc"
