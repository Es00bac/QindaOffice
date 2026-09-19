// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io chart gates (M35): kinds and grouping read from chart parts,
// multi-series cat/val references read, StackedBar exports grouping, and a
// series-authored chart survives the whole write→read trip.

#include "xlsx_bridge.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <KArchiveFile>
#include <KZip>

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcIoChartsTest : public QObject {
    Q_OBJECT
private slots:
    void chartImportReadsKindsAndGrouping();
    void stackedBarExportWritesGrouping();
    void chartsSurviveRoundTrip();
};

static QString readPart(const QString &path, const QString &part)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return {};
    }
    const KArchiveFile *file = zip.directory()->file(part);
    return file != nullptr ? QString::fromUtf8(file->data()) : QString();
}

static Workbook chartedWorkbook(bool stacked)
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    for (int r = 0; r < 4; ++r) {
        wb.setCellInput(0, {r, 0}, QStringLiteral("Q%1").arg(r + 1));
        wb.setCellInput(0, {r, 1}, QString::number((r + 1) * 10));
        wb.setCellInput(0, {r, 2}, QString::number((r + 1) * 4));
    }
    ChartSpec chart;
    chart.kind = stacked ? ChartSpec::Kind::StackedBar : ChartSpec::Kind::Line;
    chart.title = QStringLiteral("Split");
    chart.sheet = 0;
    chart.anchorRow = 6;
    chart.anchorCol = 1;
    chart.widthPx = 480;
    chart.heightPx = 288;
    chart.series.append(ChartSeriesSpec{QStringLiteral("First"),
                                        QStringLiteral("'Data'!$A$1:$A$4"),
                                        QStringLiteral("'Data'!$B$1:$B$4")});
    chart.series.append(ChartSeriesSpec{QStringLiteral("Second"),
                                        QStringLiteral("'Data'!$A$1:$A$4"),
                                        QStringLiteral("'Data'!$C$1:$C$4")});
    wb.charts.append(chart);
    return wb;
}

void CalcIoChartsTest::chartImportReadsKindsAndGrouping()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/stacked.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(chartedWorkbook(true), path, &error), qPrintable(error));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(back.charts.size(), 1);
    const ChartSpec &chart = back.charts.at(0);
    QCOMPARE(chart.kind, ChartSpec::Kind::StackedBar);
    QCOMPARE(chart.title, QStringLiteral("Split"));
    QCOMPARE(chart.sheet, 0);
    QCOMPARE(chart.anchorRow, 6);
    QCOMPARE(chart.anchorCol, 1);
    QCOMPARE(chart.series.size(), 2);
    QCOMPARE(chart.series.at(0).name, QStringLiteral("First"));
    QCOMPARE(chart.series.at(0).catRange, QStringLiteral("'Data'!$A$1:$A$4"));
    QCOMPARE(chart.series.at(0).valRange, QStringLiteral("'Data'!$B$1:$B$4"));
    QCOMPARE(chart.series.at(1).valRange, QStringLiteral("'Data'!$C$1:$C$4"));
}

void CalcIoChartsTest::stackedBarExportWritesGrouping()
{
    // The rangeText path (QXlsx writes the part) gets its grouping in the
    // post-pass as well.
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    for (int r = 0; r < 4; ++r) {
        wb.setCellInput(0, {r, 0}, QString::number((r + 1) * 10));
        wb.setCellInput(0, {r, 1}, QString::number((r + 1) * 4));
    }
    ChartSpec chart;
    chart.kind = ChartSpec::Kind::StackedBar;
    chart.title = QStringLiteral("Stacked");
    chart.rangeText = QStringLiteral("A1:B4");
    chart.sheet = 0;
    wb.charts.append(chart);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/grouped.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString chartXml = readPart(path, QStringLiteral("xl/charts/chart1.xml"));
    QVERIFY2(chartXml.contains(QStringLiteral("<c:grouping val=\"stacked\"/>")), qPrintable(chartXml.left(400)));
}

void CalcIoChartsTest::chartsSurviveRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/line.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(chartedWorkbook(false), path, &error), qPrintable(error));

    // The generated part has the shape an external consumer reads.
    const QString chartXml = readPart(path, QStringLiteral("xl/charts/chart1.xml"));
    QVERIFY(chartXml.contains(QStringLiteral("<c:lineChart>")));
    QVERIFY(chartXml.contains(QStringLiteral("'Data'!$B$1:$B$4")));
    QVERIFY(chartXml.contains(QStringLiteral("Split")));
    const QString contentTypes = readPart(path, QStringLiteral("[Content_Types].xml"));
    QVERIFY(contentTypes.contains(QStringLiteral("chart1.xml")));
    const QString sheetXml = readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheetXml.contains(QStringLiteral("<drawing r:id=")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(back.charts.size(), 1);
    QCOMPARE(back.charts.at(0).kind, ChartSpec::Kind::Line);
    QCOMPARE(back.charts.at(0).series.size(), 2);
    QCOMPARE(back.charts.at(0).title, QStringLiteral("Split"));
    // Series values resolve against the live cells after reimport.
    QCOMPARE(toNumber(back.cellValue(0, 3, 1)).value_or(0.0), 40.0);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoChartsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_charts.moc"
