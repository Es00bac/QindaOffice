// SPDX-License-Identifier: GPL-3.0-or-later
// M90 (D-098): sparklines through a save. A package built the way Excel
// 365 writes them — a line group (markers, high and low points, theme
// colours with tints, an axis, gaps, the same maximum for all), a column
// group with a custom minimum and an rgb negative colour on data from
// another sheet, a win/loss group with a date axis, beside an Excel 2010
// conditional format in the same extLst — reads into the model; a save
// writes them back as Excel's ext after the rules' (schema-valid, every
// relationship resolving) and re-reads equal. Inserting rows before the
// save moves them (the M66 carry dropped sparklines then), and sparklines
// made in QindaCalc are written the same way.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include "sheet.h"
#include "sparkline.h"
#include "structure.h"
#include "workbook.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;

namespace {

QByteArray rows()
{
    QByteArray out;
    for (int r = 1; r <= 4; ++r) {
        out += "<row r=\"" + QByteArray::number(r) + "\">";
        for (int c = 0; c < 6; ++c) {
            out += "<c r=\"" + QByteArray(1, char('A' + c)) + QByteArray::number(r) + "\"><v>"
                + QByteArray::number((r * 7 + c * 3) % 11 - 3) + "</v></c>";
        }
        out += "</row>";
    }
    return out;
}

const QByteArray kExt = QByteArrayLiteral(
    "<extLst>"
    "<ext uri=\"{78C0D931-6437-407d-A8EE-F0AAD7539E65}\" xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
    "<x14:conditionalFormattings><x14:conditionalFormatting xmlns:xm=\"http://schemas.microsoft.com/office/excel/2006/main\">"
    "<x14:cfRule type=\"dataBar\" id=\"{11111111-2222-4333-8444-555555555555}\"><x14:dataBar minLength=\"0\" maxLength=\"100\">"
    "<x14:cfvo type=\"autoMin\"/><x14:cfvo type=\"autoMax\"/><x14:negativeFillColor rgb=\"FFFF0000\"/>"
    "<x14:axisColor rgb=\"FF000000\"/></x14:dataBar></x14:cfRule><xm:sqref>A1:A4</xm:sqref>"
    "</x14:conditionalFormatting></x14:conditionalFormattings></ext>"
    "<ext uri=\"{05C60535-1F16-4fd2-B633-F4F36F0B64E0}\" xmlns:x14=\"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main\">"
    "<x14:sparklineGroups xmlns:xm=\"http://schemas.microsoft.com/office/excel/2006/main\">"
    "<x14:sparklineGroup displayEmptyCellsAs=\"gap\" markers=\"1\" high=\"1\" low=\"1\" displayXAxis=\"1\" maxAxisType=\"group\">"
    "<x14:colorSeries theme=\"4\" tint=\"-0.499984740745262\"/><x14:colorNegative theme=\"5\"/>"
    "<x14:colorAxis rgb=\"FF000000\"/><x14:colorMarkers theme=\"4\" tint=\"-0.499984740745262\"/>"
    "<x14:colorFirst theme=\"4\" tint=\"0.39997558519241921\"/><x14:colorLast theme=\"4\" tint=\"0.39997558519241921\"/>"
    "<x14:colorHigh theme=\"4\"/><x14:colorLow theme=\"4\"/>"
    "<x14:sparklines><x14:sparkline><xm:f>Data!A1:F1</xm:f><xm:sqref>H1</xm:sqref></x14:sparkline>"
    "<x14:sparkline><xm:f>Data!A2:F2</xm:f><xm:sqref>H2</xm:sqref></x14:sparkline></x14:sparklines>"
    "</x14:sparklineGroup>"
    "<x14:sparklineGroup manualMin=\"-5\" type=\"column\" negative=\"1\" minAxisType=\"custom\">"
    "<x14:colorSeries rgb=\"FF376092\"/><x14:colorNegative rgb=\"FFD00000\"/>"
    "<x14:sparklines><x14:sparkline><xm:f>'More Data'!A1:A5</xm:f><xm:sqref>H3</xm:sqref></x14:sparkline></x14:sparklines>"
    "</x14:sparklineGroup>"
    "<x14:sparklineGroup type=\"stacked\" dateAxis=\"1\" displayHidden=\"1\">"
    "<x14:colorSeries theme=\"5\"/><x14:colorNegative theme=\"6\" tint=\"-0.249977111117893\"/>"
    "<xm:f>Data!A4:F4</xm:f>"
    "<x14:sparklines><x14:sparkline><xm:f>Data!A3:F3</xm:f><xm:sqref>H4</xm:sqref></x14:sparkline></x14:sparklines>"
    "</x14:sparklineGroup>"
    "</x14:sparklineGroups></ext>"
    "</extLst>");

ExcelWorkbook excelSparklines()
{
    ExcelWorkbook book;
    ExcelSheet data(QStringLiteral("Data"), rows());
    data.afterData = QByteArrayLiteral("<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>")
        + kExt;
    ExcelSheet more(QStringLiteral("More Data"), rows());
    book.sheets = {data, more};
    return book;
}

void checkGroups(const QList<SparklineGroup> &groups)
{
    QCOMPARE(groups.size(), 3);
    const SparklineGroup &line = groups.at(0);
    QCOMPARE(line.type, SparklineGroup::Type::Line);
    QCOMPARE(line.empty, SparklineGroup::Empty::Gap);
    QVERIFY(line.markers && line.high && line.low && line.displayXAxis);
    QVERIFY(!line.first && !line.last && !line.negative);
    QCOMPARE(line.maxAxis, SparklineGroup::AxisScale::Group);
    QCOMPARE(line.minAxis, SparklineGroup::AxisScale::Individual);
    QCOMPARE(line.series.theme, 4);
    QCOMPARE(line.series.tint, -0.499984740745262);
    QCOMPARE(*line.axisColor.rgb, (Rgb{0, 0, 0}));
    QCOMPARE(line.sparklines.size(), 2);
    QCOMPARE(line.sparklines.at(1).location, (CellAddr{1, 7}));
    QCOMPARE(line.sparklines.at(1).data, QStringLiteral("Data!A2:F2"));
    const SparklineGroup &column = groups.at(1);
    QCOMPARE(column.type, SparklineGroup::Type::Column);
    QCOMPARE(column.empty, SparklineGroup::Empty::Zero); // the schema's default when absent
    QCOMPARE(column.minAxis, SparklineGroup::AxisScale::Custom);
    QCOMPARE(column.manualMin, -5.0);
    QVERIFY(column.negative);
    QCOMPARE(*column.series.rgb, (Rgb{0x37, 0x60, 0x92}));
    QCOMPARE(column.sparklines.first().data, QStringLiteral("'More Data'!A1:A5"));
    const SparklineGroup &winLoss = groups.at(2);
    QCOMPARE(winLoss.type, SparklineGroup::Type::WinLoss);
    QVERIFY(winLoss.dateAxis && winLoss.displayHidden);
    QCOMPARE(winLoss.dateRange, QStringLiteral("Data!A4:F4"));
    QCOMPARE(winLoss.negativeColor.theme, 6);
}

} // namespace

class CalcIoSparklinesTest : public QObject {
    Q_OBJECT
private slots:
    void excelSparklinesReadIntoTheModel();
    void sparklinesSurviveASaveAndAnEdit();
    void sparklinesMadeHereAreExcels();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool open(const QByteArray &bytes, const QString &name, Workbook *wb)
    {
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(bytes);
        file.close();
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("package %s is not schema-valid: %s", qPrintable(name), qPrintable(schema));
            return false;
        }
        QString error;
        return IO::readXlsxFile(path(name), wb, &error);
    }
    QByteArray bytes(const QString &name) const
    {
        QFile file(path(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name, QStringList *warnings = nullptr)
    {
        QString error;
        if (!IO::writeXlsxFile(wb, path(name), &error, warnings)) {
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

void CalcIoSparklinesTest::excelSparklinesReadIntoTheModel()
{
    Workbook wb;
    QVERIFY(open(excelSparklines().build(), QStringLiteral("sparklines.xlsx"), &wb));
    checkGroups(wb.sheetAt(0).sparklineGroups());
    QVERIFY(wb.sheetAt(1).sparklineGroups().isEmpty());
}

void CalcIoSparklinesTest::sparklinesSurviveASaveAndAnEdit()
{
    Workbook wb;
    QVERIFY(open(excelSparklines().build(), QStringLiteral("sparklines.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("sparklines-saved.xlsx"));
    QVERIFY(package);
    const QString sheet = QString::fromUtf8(package->data(QStringLiteral("xl/worksheets/sheet1.xml")));
    // One sparkline ext, after the Excel 2010 rules' ext.
    QCOMPARE(sheet.count(QStringLiteral("{05C60535-1F16-4fd2-B633-F4F36F0B64E0}")), 1);
    QVERIFY(sheet.indexOf(QStringLiteral("{78C0D931-6437-407d-A8EE-F0AAD7539E65}")) < sheet.indexOf(QStringLiteral("{05C60535")));
    QVERIFY(sheet.contains(QStringLiteral("<x14:colorSeries theme=\"4\" tint=\"-0.499984740745262\"/>")));
    QVERIFY(sheet.contains(QStringLiteral("<xm:f>'More Data'!A1:A5</xm:f><xm:sqref>H3</xm:sqref>")));
    Workbook again;
    QVERIFY(open(bytes(QStringLiteral("sparklines-saved.xlsx")), QStringLiteral("again.xlsx"), &again));
    QCOMPARE(again.sheetAt(0).sparklineGroups(), wb.sheetAt(0).sparklineGroups());
    // Rows inserted before the save: the sparklines move, nothing is dropped.
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    QStringList warnings;
    QVERIFY(save(wb, QStringLiteral("sparklines-moved.xlsx"), &warnings));
    QVERIFY2(warnings.filter(QStringLiteral("parkline")).isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
    Workbook moved;
    QVERIFY(open(bytes(QStringLiteral("sparklines-moved.xlsx")), QStringLiteral("moved.xlsx"), &moved));
    const QList<SparklineGroup> groups = moved.sheetAt(0).sparklineGroups();
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.at(0).sparklines.at(0).location, (CellAddr{2, 7}));
    QCOMPARE(groups.at(0).sparklines.at(0).data, QStringLiteral("Data!A3:F3"));
    QCOMPARE(groups.at(1).sparklines.at(0).data, QStringLiteral("'More Data'!A1:A5")); // another sheet: unmoved
    QCOMPARE(groups.at(2).dateRange, QStringLiteral("Data!A6:F6"));
}

void CalcIoSparklinesTest::sparklinesMadeHereAreExcels()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Sheet1"));
    for (qint32 c = 0; c < 5; ++c) {
        wb.setCellInput(0, {0, c}, QString::number(c * c - 4));
    }
    QString error;
    const auto lines = sparklinesFor(wb, 0, QStringLiteral("A1:E1"), QStringLiteral("G1"), &error);
    QVERIFY2(lines.has_value(), qPrintable(error));
    SparklineGroup group = defaultSparklineGroup(SparklineGroup::Type::Column);
    group.sparklines = *lines;
    group.negative = true;
    group.lineWeight = 1.5;
    wb.sheetAt(0).setSparklineGroups({group});
    const auto package = save(wb, QStringLiteral("made.xlsx"));
    QVERIFY(package);
    const QString sheet = QString::fromUtf8(package->data(QStringLiteral("xl/worksheets/sheet1.xml")));
    QVERIFY2(sheet.contains(QStringLiteral("<x14:sparklineGroup lineWeight=\"1.5\" type=\"column\" displayEmptyCellsAs=\"gap\" negative=\"1\">")),
             qPrintable(sheet));
    QVERIFY(sheet.contains(QStringLiteral("<xm:f>Sheet1!A1:E1</xm:f><xm:sqref>G1</xm:sqref>")));
    Workbook again;
    QVERIFY(open(bytes(QStringLiteral("made.xlsx")), QStringLiteral("made-again.xlsx"), &again));
    QCOMPARE(again.sheetAt(0).sparklineGroups(), wb.sheetAt(0).sparklineGroups());
}

QTEST_GUILESS_MAIN(CalcIoSparklinesTest)
#include "tst_calc_io_sparklines.moc"
