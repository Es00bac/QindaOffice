// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): chart sheets. A workbook whose second sheet is a chart sheet
// (xl/chartsheets/, its drawing's absolute anchor, the chart and Excel's
// chart style part) opens with the chart sheet in its place, showing its
// chart, and saves with the chartsheet part, the drawing, the chart and the
// style copied byte for byte, every relationship resolving and every sheet
// in its order — before M69 the chart sheet came back an empty worksheet.
// When the data sheet is renamed or gains rows, the chart's references
// follow, and only they change. The packages are built by hand the way
// Excel writes them; every saved one validates against the schemas.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_package.h"
#include "m69_fixtures.h"

#include "structure.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;

class CalcIoChartSheetsTest : public QObject {
    Q_OBJECT
private slots:
    void opensWithItsChart();
    void savesTheChartSheetVerbatim();
    void chartReferencesFollowEdits();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool open(const QString &name, Workbook *wb);
    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name);
};

namespace {

// The saved part a workbook relationship of `type` leads to, by sheet name.
QString sheetPartOf(const Package &package, const QString &name, QString *type)
{
    const auto root = package.xml(QStringLiteral("xl/workbook.xml"));
    const auto *sheets = root ? root->child(sml(), u"sheets") : nullptr;
    for (const auto *sheet : sheets != nullptr ? sheets->elements(sml(), u"sheet")
                                               : QList<const QindaOffice::Ooxml::XmlElement *>()) {
        if (sheet->attr(u"name") != name) {
            continue;
        }
        const QString id = sheet->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id");
        const auto rel = package.relationship(QStringLiteral("xl/workbook.xml"), id);
        if (!rel) {
            return {};
        }
        *type = rel->type;
        return package.targetPart(QStringLiteral("xl/workbook.xml"), *rel);
    }
    return {};
}

QStringList sheetOrder(const Package &package)
{
    QStringList names;
    const auto root = package.xml(QStringLiteral("xl/workbook.xml"));
    const auto *sheets = root ? root->child(sml(), u"sheets") : nullptr;
    for (const auto *sheet : sheets != nullptr ? sheets->elements(sml(), u"sheet")
                                               : QList<const QindaOffice::Ooxml::XmlElement *>()) {
        names.append(sheet->attr(u"name"));
    }
    return names;
}

} // namespace

bool CalcIoChartSheetsTest::open(const QString &name, Workbook *wb)
{
    QFile file(path(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(chartBook().build());
    file.close();
    QStringList warnings;
    QString error;
    const bool ok = IO::readXlsxFile(path(name), wb, &error, &warnings);
    if (!warnings.isEmpty()) {
        qWarning("open warnings: %s", qPrintable(warnings.join(QLatin1Char(' '))));
    }
    return ok && warnings.isEmpty();
}

std::shared_ptr<const Package> CalcIoChartSheetsTest::save(const Workbook &wb, const QString &name)
{
    QString error;
    QStringList warnings;
    if (!IO::writeXlsxFile(wb, path(name), &error, &warnings) || !warnings.isEmpty()) {
        qWarning("write: %s %s", qPrintable(error), qPrintable(warnings.join(QLatin1Char(' '))));
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

void CalcIoChartSheetsTest::opensWithItsChart()
{
    Workbook wb;
    QVERIFY(open(QStringLiteral("open.xlsx"), &wb));
    QCOMPARE(wb.sheetCount(), 3);
    QCOMPARE(wb.sheetName(1), QStringLiteral("Sales Chart"));
    QVERIFY(wb.sheetAt(1).isChartSheet());
    QVERIFY(wb.sheetAt(1).cells().isEmpty());
    QVERIFY(!wb.sheetAt(0).isChartSheet());
    QCOMPARE(wb.charts.size(), 1);
    const ChartSpec &chart = wb.charts.first();
    QCOMPARE(chart.sheet, 1);
    QCOMPARE(chart.title, QStringLiteral("Quarter sales"));
    QCOMPARE(chart.series.size(), 1);
    QCOMPARE(chart.series.first().valRange, QStringLiteral("'Q1 Data'!$B$2:$B$4"));
    QCOMPARE(chart.widthPx, 910); // the absolute anchor's 8667750 EMU
    QCOMPARE(wb.cellValue(2, 0, 0), Value(60.0));
}

void CalcIoChartSheetsTest::savesTheChartSheetVerbatim()
{
    Workbook wb;
    QVERIFY(open(QStringLiteral("verbatim-in.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("verbatim-out.xlsx"));
    QVERIFY(package);
    QVERIFY2(brokenWiring(*package).isEmpty(), qPrintable(brokenWiring(*package).join(QLatin1Char('\n'))));
    QCOMPARE(sheetOrder(*package), (QStringList{QStringLiteral("Q1 Data"), QStringLiteral("Sales Chart"),
                                                 QStringLiteral("Summary")}));
    QString type;
    const QString chartSheet = sheetPartOf(*package, QStringLiteral("Sales Chart"), &type);
    QCOMPARE(type, officeRel("chartsheet"));
    QCOMPARE(package->data(chartSheet), kChartSheet);
    QCOMPARE(package->contentType(chartSheet), smlType("chartsheet+xml"));
    const QStringList drawings = partsByType(*package, chartSheet, officeRel("drawing"));
    QCOMPARE(drawings.size(), 1);
    QCOMPARE(package->data(drawings.first()), kDrawing);
    const QStringList charts = partsByType(*package, drawings.first(), officeRel("chart"));
    QCOMPARE(charts.size(), 1);
    QCOMPARE(package->data(charts.first()), chartXml());
    const QStringList styles = partsByType(*package, charts.first(),
                                           QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/chartStyle"));
    QCOMPARE(styles.size(), 1);
    QCOMPARE(package->data(styles.first()), kChartStyle);
    // No worksheet was left in the chart sheet's place (nor its content
    // type), and the other sheets kept their positions (the scoped name
    // still means Summary).
    static const QRegularExpression kWorksheet(QStringLiteral("^xl/worksheets/sheet\\d+\\.xml$"));
    QCOMPARE(package->partNames().filter(kWorksheet).size(), 2);
    static const QRegularExpression kOverride(QStringLiteral("PartName=\"/([^\"]+)\""));
    auto overrides = kOverride.globalMatch(QString::fromUtf8(package->data(QStringLiteral("[Content_Types].xml"))));
    while (overrides.hasNext()) {
        const QString part = overrides.next().captured(1);
        QVERIFY2(package->contains(part), qPrintable(part));
    }
    const QList<SavedName> names = savedDefinedNames(*package);
    QCOMPARE(names.size(), 1);
    QCOMPARE(names.first().localSheetId, QStringLiteral("2"));
    // It opens again as a chart sheet with its chart.
    Workbook back;
    QStringList warnings;
    QVERIFY(IO::readXlsxFile(path(QStringLiteral("verbatim-out.xlsx")), &back, nullptr, &warnings));
    QVERIFY(warnings.isEmpty());
    QVERIFY(back.sheetAt(1).isChartSheet());
    QCOMPARE(back.charts.size(), 1);
    QCOMPARE(back.charts.first().sheet, 1);
}

void CalcIoChartSheetsTest::chartReferencesFollowEdits()
{
    Workbook wb;
    QVERIFY(open(QStringLiteral("edit-in.xlsx"), &wb));
    QVERIFY(renameSheet(wb, 0, QStringLiteral("Data")));
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    // The chart the window shows follows at once.
    QCOMPARE(wb.charts.first().series.first().valRange, QStringLiteral("Data!$B$4:$B$6"));
    const auto package = save(wb, QStringLiteral("edit-out.xlsx"));
    QVERIFY(package);
    QString type;
    const QString chartSheet = sheetPartOf(*package, QStringLiteral("Sales Chart"), &type);
    QCOMPARE(package->data(chartSheet), kChartSheet);
    const QStringList drawings = partsByType(*package, chartSheet, officeRel("drawing"));
    QCOMPARE(drawings.size(), 1);
    const QStringList charts = partsByType(*package, drawings.first(), officeRel("chart"));
    QCOMPARE(charts.size(), 1);
    QByteArray expected = chartXml();
    expected.replace("'Q1 Data'!$B$1", "Data!$B$3");
    expected.replace("'Q1 Data'!$A$2:$A$4", "Data!$A$4:$A$6");
    expected.replace("'Q1 Data'!$B$2:$B$4", "Data!$B$4:$B$6");
    QCOMPARE(package->data(charts.first()), expected);
    // A removed data sheet leaves #REF! in the chart, the chart sheet stays.
    QVERIFY(removeSheet(wb, 0));
    const auto removed = save(wb, QStringLiteral("removed-out.xlsx"));
    QVERIFY(removed);
    const QString chartSheet2 = sheetPartOf(*removed, QStringLiteral("Sales Chart"), &type);
    const QStringList charts2 = partsByType(*removed, partsByType(*removed, chartSheet2, officeRel("drawing")).value(0),
                                            officeRel("chart"));
    QCOMPARE(charts2.size(), 1);
    QVERIFY(removed->data(charts2.first()).contains("<c:f>#REF!</c:f>"));
}

QTEST_GUILESS_MAIN(CalcIoChartSheetsTest)
#include "tst_calc_io_chartsheets.moc"
