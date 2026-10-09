// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075): dynamic arrays as Excel stores them. The anchor cell holds
// <f t="array" ref="B1:B3"> with the spill range, cm="1" points it at the
// cell metadata block that xl/metadata.xml declares as XLDAPR with
// fDynamic="1", the spilled cells hold plain cached values, and the
// functions keep their prefixes (_xlfn.UNIQUE, _xlfn._xlws.SORT). Without
// the cm attribute Excel reads the formula as a legacy array (or implicit
// intersection) and shows one value; without the metadata the package is
// broken. Before M66 these formulas evaluated #NAME? and were written as
// ordinary formulas.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_package.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;

class CalcIoArraysTest : public QObject {
    Q_OBJECT
private slots:
    void excelDynamicArraysRead();
    void dynamicArraysWrittenAsExcelWrites();
    void blockedSpillIsSavedAsSpillError();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
};

namespace {

QByteArray dynamicArrayMetadata()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<metadata xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\""
        " xmlns:xda=\"http://schemas.microsoft.com/office/spreadsheetml/2017/dynamicarray\">"
        "<metadataTypes count=\"1\"><metadataType name=\"XLDAPR\" minSupportedVersion=\"120000\" copy=\"1\""
        " pasteAll=\"1\" pasteValues=\"1\" merge=\"1\" splitFirst=\"1\" rowColShift=\"1\" clearFormats=\"1\""
        " clearComments=\"1\" assign=\"1\" coerce=\"1\" cellMeta=\"1\"/></metadataTypes>"
        "<futureMetadata name=\"XLDAPR\" count=\"1\"><bk><extLst><ext uri=\"{bdbb8cdc-fa1e-496e-a857-3c3f30c029c3}\">"
        "<xda:dynamicArrayProperties fDynamic=\"1\" fCollapsed=\"0\"/></ext></extLst></bk></futureMetadata>"
        "<cellMetadata count=\"1\"><bk><rc t=\"1\" v=\"0\"/></bk></cellMetadata></metadata>");
}

// A1:A4 holds 3, 1, 2, 1; B1 spills UNIQUE over it, C1 spills SORT, and D1
// is a legacy (pre-dynamic) formula that must stay one.
ExcelWorkbook arrayBook()
{
    ExcelWorkbook book;
    ExcelSheet sheet;
    sheet.name = QStringLiteral("Arrays");
    sheet.sheetData = QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>3</v></c>"
        "<c r=\"B1\" cm=\"1\"><f t=\"array\" ref=\"B1:B3\">_xlfn.UNIQUE(A1:A4)</f><v>3</v></c>"
        "<c r=\"C1\" cm=\"1\"><f t=\"array\" ref=\"C1:C4\">_xlfn._xlws.SORT(A1:A4)</f><v>1</v></c>"
        "<c r=\"D1\"><f>SUM(A1:A4)</f><v>7</v></c></row>"
        "<row r=\"2\"><c r=\"A2\"><v>1</v></c><c r=\"B2\"><v>1</v></c><c r=\"C2\"><v>1</v></c></row>"
        "<row r=\"3\"><c r=\"A3\"><v>2</v></c><c r=\"B3\"><v>2</v></c><c r=\"C3\"><v>2</v></c></row>"
        "<row r=\"4\"><c r=\"A4\"><v>1</v></c><c r=\"C4\"><v>3</v></c></row>");
    book.sheets = {sheet};
    book.parts.append({QStringLiteral("xl/metadata.xml"), dynamicArrayMetadata(), smlType("sheetMetadata+xml"), {}});
    book.workbookRels.append({QStringLiteral("rIdMeta"), officeRel("sheetMetadata"), QStringLiteral("xl/metadata.xml")});
    return book;
}

struct ArrayCell {
    QString cm;
    QString formulaType;
    QString ref;
    QString formula;
    QString type;
    QString value;
};

QHash<QString, ArrayCell> arrayCells(const Package &package, const QString &part)
{
    QHash<QString, ArrayCell> out;
    const auto root = package.xml(part);
    const XmlElement *data = root ? root->child(sml(), u"sheetData") : nullptr;
    for (const XmlElement *row : data != nullptr ? data->elements(sml(), u"row") : QList<const XmlElement *>()) {
        for (const XmlElement *c : row->elements(sml(), u"c")) {
            ArrayCell cell;
            cell.cm = c->attr(u"cm");
            cell.type = c->attr(u"t");
            if (const XmlElement *f = c->child(sml(), u"f")) {
                cell.formulaType = f->attr(u"t");
                cell.ref = f->attr(u"ref");
                cell.formula = f->text();
            }
            if (const XmlElement *v = c->child(sml(), u"v")) {
                cell.value = v->text();
            }
            out.insert(c->attr(u"r"), cell);
        }
    }
    return out;
}

// The cm value a dynamic-array cell must carry, from the saved metadata.
QString dynamicCm(const Package &package)
{
    const QStringList parts = partsByType(package, QStringLiteral("xl/workbook.xml"), officeRel("sheetMetadata"));
    if (parts.size() != 1) {
        return {};
    }
    const QByteArray xml = package.data(parts.first());
    if (!xml.contains("name=\"XLDAPR\"") || !xml.contains("fDynamic=\"1\"") || !xml.contains("<rc t=\"1\" v=\"0\"/>")) {
        return {};
    }
    return QStringLiteral("1");
}

} // namespace

void CalcIoArraysTest::excelDynamicArraysRead()
{
    const QString source = path(QStringLiteral("arrays.xlsx"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(arrayBook().build());
    }
    QQO_VERIFY_XLSX_SCHEMA(source);
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    // UNIQUE(3,1,2,1) = 3,1,2 ; SORT = 1,1,2,3 — computed, not just cached.
    QCOMPARE(wb.cellValue(0, 0, 1), Value(3.0));
    QCOMPARE(wb.cellValue(0, 2, 1), Value(2.0));
    QVERIFY(isBlank(wb.cellValue(0, 3, 1)));
    QCOMPARE(wb.cellValue(0, 3, 2), Value(3.0));
    // An edit flows through: the spill grows by one.
    QVERIFY(wb.setCellInput(0, {3, 0}, QStringLiteral("9")));
    QCOMPARE(wb.cellValue(0, 3, 1), Value(9.0));
    QCOMPARE(wb.cellValue(0, 3, 2), Value(9.0));
    QCOMPARE(wb.cellValue(0, 0, 3), Value(15.0));
}

void CalcIoArraysTest::dynamicArraysWrittenAsExcelWrites()
{
    const QString source = path(QStringLiteral("arrays2.xlsx"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(arrayBook().build());
    }
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    QVERIFY(wb.setCellInput(0, {3, 0}, QStringLiteral("9"))); // UNIQUE now spills four
    // A formula typed in the app spills too (every typed formula is a
    // dynamic-array formula, as in Excel 365).
    QVERIFY(wb.setCellInput(0, {0, 5}, QStringLiteral("=SEQUENCE(2,2)")));
    const QString saved = path(QStringLiteral("arrays-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QindaOffice::Test::keepForInspection(saved);
    const auto package = Package::load(saved, &error);
    QVERIFY2(package, qPrintable(error));
    QVERIFY(brokenWiring(*package).isEmpty());
    const QString cm = dynamicCm(*package);
    QVERIFY2(!cm.isEmpty(), "xl/metadata.xml with the XLDAPR block is missing");
    const QHash<QString, ArrayCell> cells = arrayCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    const ArrayCell unique = cells.value(QStringLiteral("B1"));
    QCOMPARE(unique.cm, cm);
    QCOMPARE(unique.formulaType, QStringLiteral("array"));
    QCOMPARE(unique.ref, QStringLiteral("B1:B4"));
    QCOMPARE(unique.formula, QStringLiteral("_xlfn.UNIQUE(A1:A4)"));
    const ArrayCell sorted = cells.value(QStringLiteral("C1"));
    QCOMPARE(sorted.cm, cm);
    QCOMPARE(sorted.ref, QStringLiteral("C1:C4"));
    QCOMPARE(sorted.formula, QStringLiteral("_xlfn._xlws.SORT(A1:A4)"));
    // Spilled cells are plain values, as Excel writes them.
    QVERIFY(cells.value(QStringLiteral("B4")).formula.isEmpty());
    QCOMPARE(cells.value(QStringLiteral("B4")).value, QStringLiteral("9"));
    QCOMPARE(cells.value(QStringLiteral("C4")).value, QStringLiteral("9"));
    const ArrayCell typed = cells.value(QStringLiteral("F1"));
    QCOMPARE(typed.cm, cm);
    QCOMPARE(typed.ref, QStringLiteral("F1:G2"));
    QCOMPARE(typed.formula, QStringLiteral("_xlfn.SEQUENCE(2,2)"));
    QCOMPARE(cells.value(QStringLiteral("G2")).value, QStringLiteral("4"));
    // The legacy formula stays an ordinary one.
    const ArrayCell legacy = cells.value(QStringLiteral("D1"));
    QVERIFY(legacy.cm.isEmpty());
    QVERIFY(legacy.formulaType.isEmpty() || legacy.formulaType == QLatin1String("normal"));
}

void CalcIoArraysTest::blockedSpillIsSavedAsSpillError()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Blocked"));
    QVERIFY(wb.setCellInput(0, {1, 0}, QStringLiteral("x")));
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("=SEQUENCE(3)")));
    const QString saved = path(QStringLiteral("blocked.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QindaOffice::Test::keepForInspection(saved);
    const auto package = Package::load(saved, &error);
    QVERIFY2(package, qPrintable(error));
    const QHash<QString, ArrayCell> cells = arrayCells(*package, QStringLiteral("xl/worksheets/sheet1.xml"));
    const ArrayCell anchor = cells.value(QStringLiteral("A1"));
    QCOMPARE(anchor.type, QStringLiteral("e"));
    QCOMPARE(anchor.value, QStringLiteral("#SPILL!"));
    QCOMPARE(anchor.formulaType, QStringLiteral("array"));
    QCOMPARE(anchor.ref, QStringLiteral("A1"));
    QCOMPARE(anchor.cm, dynamicCm(*package));
    // The blocker is untouched (a shared string, resolved).
    QCOMPARE(savedCells(*package, QStringLiteral("xl/worksheets/sheet1.xml")).value(QStringLiteral("A2")).value,
             QStringLiteral("x"));
}

QTEST_GUILESS_MAIN(CalcIoArraysTest)
#include "tst_calc_io_arrays.moc"
