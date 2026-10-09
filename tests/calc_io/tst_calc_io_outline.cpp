// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075): row and column grouping as Excel writes it — outlineLevel,
// collapsed and hidden on <row> and <col>, the sheet's outline depth in
// <sheetFormatPr outlineLevelRow/outlineLevelCol>, and where the summary
// rows and columns sit in <sheetPr><outlinePr summaryBelow summaryRight>.
// Before M66 every level, every hidden row and the summary settings were
// lost on save, and hidden rows came back visible.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;
using QindaOffice::Test::ExcelSheet;
using QindaOffice::Test::ExcelWorkbook;
using QindaOffice::Test::sml;

class CalcIoOutlineTest : public QObject {
    Q_OBJECT
private slots:
    void excelOutlineSurvivesSave();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
};

namespace {

// A two-level row outline over rows 2-7 with the summary ABOVE the detail
// (summaryBelow="0"): rows 3-4 are a collapsed level-2 group whose summary
// row 2 carries collapsed="1"; columns B:C form a collapsed level-1 group
// with summary column A (summaryRight="0"). Exactly Excel's spelling.
ExcelWorkbook outlineBook()
{
    ExcelWorkbook book;
    ExcelSheet sheet;
    sheet.name = QStringLiteral("Budget");
    sheet.rootAttributes = QByteArrayLiteral(
        " xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" mc:Ignorable=\"x14ac\""
        " xmlns:x14ac=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/ac\"");
    sheet.beforeData = QByteArrayLiteral(
        "<sheetPr><outlinePr summaryBelow=\"0\" summaryRight=\"0\"/></sheetPr>"
        "<dimension ref=\"A1:D7\"/>"
        "<sheetViews><sheetView workbookViewId=\"0\"/></sheetViews>"
        "<sheetFormatPr defaultRowHeight=\"15\" outlineLevelRow=\"2\" outlineLevelCol=\"1\" x14ac:dyDescent=\"0.25\"/>"
        "<cols><col min=\"1\" max=\"1\" width=\"12.7109375\" collapsed=\"1\" customWidth=\"1\"/>"
        "<col min=\"2\" max=\"3\" width=\"9.140625\" hidden=\"1\" outlineLevel=\"1\"/></cols>");
    sheet.sheetData = QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\"><v>1</v></c><c r=\"D1\"><v>10</v></c></row>"
        "<row r=\"2\" collapsed=\"1\" outlineLevel=\"1\"><c r=\"A2\"><v>2</v></c></row>"
        "<row r=\"3\" hidden=\"1\" outlineLevel=\"2\"><c r=\"A3\"><v>3</v></c></row>"
        "<row r=\"4\" hidden=\"1\" outlineLevel=\"2\"><c r=\"A4\"><v>4</v></c></row>"
        "<row r=\"5\" outlineLevel=\"1\"><c r=\"A5\"><v>5</v></c></row>"
        "<row r=\"6\" outlineLevel=\"1\"><c r=\"A6\"><v>6</v></c></row>"
        "<row r=\"7\" outlineLevel=\"1\" x14ac:dyDescent=\"0.25\"/>");
    sheet.afterData = QByteArrayLiteral(
        "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>");
    book.sheets = {sheet};
    return book;
}

struct SavedLine {
    QString level;
    QString hidden;
    QString collapsed;
};

QHash<int, SavedLine> savedRows(const Package &package, const QString &part)
{
    QHash<int, SavedLine> out;
    const QindaOffice::Ooxml::XmlElementPtr root = package.xml(part);
    const XmlElement *data = root ? root->child(sml(), u"sheetData") : nullptr;
    if (data == nullptr) {
        return out;
    }
    for (const XmlElement *row : data->elements(sml(), u"row")) {
        out.insert(row->attr(u"r").toInt(), {row->attr(u"outlineLevel"), row->attr(u"hidden"), row->attr(u"collapsed")});
    }
    return out;
}

// Columns 1-based, one entry per column of every <col> span.
QHash<int, SavedLine> savedColumns(const Package &package, const QString &part)
{
    QHash<int, SavedLine> out;
    const QindaOffice::Ooxml::XmlElementPtr root = package.xml(part);
    const XmlElement *cols = root ? root->child(sml(), u"cols") : nullptr;
    if (cols == nullptr) {
        return out;
    }
    for (const XmlElement *col : cols->elements(sml(), u"col")) {
        for (int c = col->attr(u"min").toInt(); c <= col->attr(u"max").toInt(); ++c) {
            out.insert(c, {col->attr(u"outlineLevel"), col->attr(u"hidden"), col->attr(u"collapsed")});
        }
    }
    return out;
}

bool truthy(const QString &flag)
{
    return flag == QLatin1String("1") || flag == QLatin1String("true");
}

} // namespace

void CalcIoOutlineTest::excelOutlineSurvivesSave()
{
    const QString source = path(QStringLiteral("outline.xlsx"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(outlineBook().build());
    }
    QQO_VERIFY_XLSX_SCHEMA(source);
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    const QString saved = path(QStringLiteral("outline-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QindaOffice::Test::keepForInspection(saved);
    const auto package = Package::load(saved, &error);
    QVERIFY2(package, qPrintable(error));
    const QString part = QStringLiteral("xl/worksheets/sheet1.xml");

    const QHash<int, SavedLine> rows = savedRows(*package, part);
    QCOMPARE(rows.value(2).level, QStringLiteral("1"));
    QVERIFY(truthy(rows.value(2).collapsed));
    QVERIFY(!truthy(rows.value(2).hidden));
    for (const int r : {3, 4}) {
        QCOMPARE(rows.value(r).level, QStringLiteral("2"));
        QVERIFY2(truthy(rows.value(r).hidden), qPrintable(QStringLiteral("row %1 hidden").arg(r)));
    }
    for (const int r : {5, 6, 7}) {
        // Row 7 is an empty grouped row: it must still be written.
        QCOMPARE(rows.value(r).level, QStringLiteral("1"));
        QVERIFY(!truthy(rows.value(r).hidden));
    }
    QVERIFY(rows.value(1).level.isEmpty() || rows.value(1).level == QLatin1String("0"));

    const QHash<int, SavedLine> cols = savedColumns(*package, part);
    QVERIFY(truthy(cols.value(1).collapsed));
    for (const int c : {2, 3}) {
        QCOMPARE(cols.value(c).level, QStringLiteral("1"));
        QVERIFY(truthy(cols.value(c).hidden));
    }

    const QindaOffice::Ooxml::XmlElementPtr root = package->xml(part);
    QVERIFY(root);
    const XmlElement *format = root->child(sml(), u"sheetFormatPr");
    QVERIFY(format != nullptr);
    QCOMPARE(format->attr(u"outlineLevelRow"), QStringLiteral("2"));
    QCOMPARE(format->attr(u"outlineLevelCol"), QStringLiteral("1"));
    const XmlElement *sheetPr = root->child(sml(), u"sheetPr");
    QVERIFY(sheetPr != nullptr);
    const XmlElement *outlinePr = sheetPr->child(sml(), u"outlinePr");
    QVERIFY(outlinePr != nullptr);
    QCOMPARE(outlinePr->attr(u"summaryBelow"), QStringLiteral("0"));
    QCOMPARE(outlinePr->attr(u"summaryRight"), QStringLiteral("0"));
}

QTEST_GUILESS_MAIN(CalcIoOutlineTest)
#include "tst_calc_io_outline.moc"
