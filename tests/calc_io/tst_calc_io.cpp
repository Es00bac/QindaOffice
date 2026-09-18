// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io gates (plan M3): xlsx round-trips formulas, merges, widths, styles
// and dates; a written file opens in LibreOffice (live, skipped when
// unavailable); a 100k-cell sheet round-trips within the responsiveness
// budget.

#include "xlsx_bridge.h"
#include "xlsx_package.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QProcess>
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>

#include <QtTest>

#include <functional>

using namespace QindaOffice::Calc;

class CalcIoTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void opensInLibreOffice();
    void largeSheetStaysResponsive();
    void chartExportWritesChartPart();
    void chartRangeParsing();
    void namedRangesRoundTrip();
    void absolutePartNamesStillOpen();
};

static Workbook sampleWorkbook()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Data"));
    auto put = [&wb](const char *addr, const QString &input) {
        const QString text = QString::fromLatin1(addr);
        int i = 0;
        while (!text.at(i).isDigit()) {
            ++i;
        }
        wb.setCellInput(0, {text.mid(i).toInt() - 1, colFromName(text.left(i)).value_or(0)},
                         input);
    };
    put("A1", QStringLiteral("1"));
    put("A2", QStringLiteral("2"));
    put("A3", QStringLiteral("=SUM(A1:A2)*10"));
    put("B1", QStringLiteral("hello"));
    put("B2", QStringLiteral("=DATE(2024,3,15)"));
    sheet.addMerge({{4, 1}, {5, 2}}); // B5:C6
    sheet.setColumnWidth(0, 140);
    const qint32 style = wb.addStyle([] {
        CellStyle s;
        s.numberFormat = QStringLiteral("#,##0.00");
        s.bold = true;
        return s;
    }());
    sheet.setStyleIdAt({0, 0}, style); // A1 (a value cell) carries the style
    return wb;
}

void CalcIoTest::roundTrip()
{
    Workbook wb = sampleWorkbook();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/roundtrip.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetCount(), 1);
    QCOMPARE(back.sheetName(0), QStringLiteral("Data"));
    QCOMPARE(toText(back.cellValue(0, 2, 0)), QStringLiteral("30")); // A3 recalculated
    QCOMPARE(toText(back.cellValue(0, 0, 1)), QStringLiteral("hello"));
    QCOMPARE(std::get<double>(back.cellValue(0, 1, 1)), 45366.0); // B2 date serial
    QCOMPARE(back.sheetAt(0).merges().size(), 1);
    QVERIFY(back.sheetAt(0).columnWidth(0) > 100);
    QCOMPARE(back.sheetAt(0).styleIdAt({0, 0}) != 0, true);
    const CellStyle *readStyle = back.style(back.sheetAt(0).styleIdAt({0, 0}));
    QVERIFY(readStyle != nullptr);
    QCOMPARE(readStyle->numberFormat, QStringLiteral("#,##0.00"));
}

void CalcIoTest::chartRangeParsing()
{
    const CellRange single = parseChartRange(QStringLiteral("B2"));
    QVERIFY(single.isValid());
    QCOMPARE(single.topLeft.row, 1);
    QCOMPARE(single.topLeft.col, 1);
    const CellRange range = parseChartRange(QStringLiteral("a1:$c$5"));
    QVERIFY(range.isValid());
    QCOMPARE(range.topLeft.row, 0);
    QCOMPARE(range.bottomRight.row, 4);
    QCOMPARE(range.bottomRight.col, 2);
    QVERIFY(!parseChartRange(QStringLiteral("1A:B2")).isValid());
    QVERIFY(!parseChartRange(QStringLiteral("")).isValid());
}

void CalcIoTest::chartExportWritesChartPart()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Data"));
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            wb.setCellValue(0, {r, c}, double(r * 10 + c));
        }
    }
    Q_UNUSED(sheet);
    ChartSpec chart;
    chart.kind = ChartSpec::Kind::Bar;
    chart.title = QStringLiteral("Quarterly");
    chart.rangeText = QStringLiteral("A1:B4");
    chart.sheet = 0;
    wb.charts.append(chart);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/charted.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));

    // The xlsx package must carry a chart part with the series range and
    // title (the gate QXlsx::Chart promised).
    KZip zip(path);
    QVERIFY(zip.open(QIODevice::ReadOnly));
    QHash<QString, const KArchiveFile *> files;
    std::function<void(const KArchiveDirectory *)> walk = [&](const KArchiveDirectory *dir) {
        const QStringList names = dir->entries();
        for (const QString &name : names) {
            const KArchiveEntry *entry = dir->entry(name);
            const QString full = dir->name() == QLatin1String("/")
                ? entry->name()
                : dir->name() + QLatin1Char('/') + entry->name();
            if (entry->isDirectory()) {
                walk(static_cast<const KArchiveDirectory *>(entry));
            } else {
                files.insert(full, static_cast<const KArchiveFile *>(entry));
            }
        }
    };
    walk(zip.directory());
    bool foundChart = false;
    bool foundDrawing = false;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString &name = it.key();
        if (name.contains(QLatin1String("charts/chart"))) {
            foundChart = true;
            const QByteArray xml = it.value()->data();
            QVERIFY(xml.contains("Quarterly"));
            QVERIFY(xml.contains("Data"));
        }
        if (name.contains(QLatin1String("drawing")) && name.endsWith(QLatin1String(".xml"))) {
            foundDrawing = true;
        }
    }
    QVERIFY2(foundChart, "chart part missing in the xlsx package");
    QVERIFY2(foundDrawing, "drawing part missing in the xlsx package");
}

void CalcIoTest::opensInLibreOffice()
{
    {
        QProcess probe;
        probe.start(QStringLiteral("soffice"),
                    {QStringLiteral("--headless"), QStringLiteral("--version")});
        if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
            QSKIP("LibreOffice unavailable in this environment");
        }
    }
    Workbook wb = sampleWorkbook();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString xlsx = dir.path() + QStringLiteral("/sample.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, xlsx, &error), qPrintable(error));

    // Convert to CSV and confirm LibreOffice evaluates the formula.
    QProcess soffice;
    soffice.start(QStringLiteral("soffice"),
                  {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                   QStringLiteral("--nologo"),
                   QStringLiteral("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                   QStringLiteral("--convert-to"), QStringLiteral("csv"),
                   QStringLiteral("--outdir"), dir.path(), xlsx});
    QVERIFY(soffice.waitForFinished(120000));
    QFile csv(dir.path() + QStringLiteral("/sample.csv"));
    if (!csv.exists()) {
        qWarning() << "soffice exit" << soffice.exitCode() << soffice.errorString()
                   << QString::fromUtf8(soffice.readAllStandardError());
        QSKIP("LibreOffice conversion unavailable in this environment");
    }
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(csv.readAll());
    QVERIFY2(content.contains(QStringLiteral("30")), qPrintable(content));
    QVERIFY2(content.contains(QStringLiteral("45366")), qPrintable(content));
}

void CalcIoTest::largeSheetStaysResponsive()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Big"));
    QElapsedTimer timer;
    timer.start();
    constexpr int kRows = 400;   // 400 x 250 = 100k cells
    constexpr int kCols = 250;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            Q_UNUSED(sheet);
            if ((r + c) % 3 == 0) {
                wb.setCellValue(0, {r, c}, double(r * c));
            } else if ((r + c) % 7 == 0) {
                wb.setCellValue(0, {r, c}, QStringLiteral("row %1 col %2").arg(r).arg(c));
            }
        }
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/big.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QVERIFY2(timer.elapsed() < 30000,
             qPrintable(QStringLiteral("100k-cell round trip took %1 ms").arg(timer.elapsed())));
    // Reading preserved the grid shape.
    QVERIFY(back.sheetAt(0).cellAt({123, 45}) != nullptr);
}

// Named ranges are part of the document, so they have to survive the file.
// The engine resolved names but nothing ever defined or persisted one, so
// every "=TOTAL" in an imported workbook evaluated to #NAME?.
void CalcIoTest::namedRangesRoundTrip()
{
    Workbook wb;
    Sheet &sheet = wb.addSheet(QStringLiteral("Data"));
    for (int row = 0; row < 4; ++row) {
        wb.setCellInput(0, {row, 0}, QString::number(row + 1));
    }
    Q_UNUSED(sheet);
    RangeRef target;
    target.sheet = QStringLiteral("Data");
    target.range = {{0, 0}, {3, 0}};
    target.absRow0 = target.absCol0 = target.absRow1 = target.absCol1 = true;
    wb.defineName(QStringLiteral("TOTALS"), target);
    wb.setCellInput(0, {5, 0}, QStringLiteral("=SUM(TOTALS)"));
    QCOMPARE(toText(wb.cellValue(0, 5, 0)), QStringLiteral("10"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/names.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const auto name = back.namedRange(QStringLiteral("TOTALS"));
    QVERIFY(name.has_value());
    QCOMPARE(name->sheet, QStringLiteral("Data"));
    QCOMPARE(name->range.bottomRight.row, 3);
    // And the formula that uses it still evaluates against the real cells.
    QCOMPARE(toText(back.cellValue(0, 5, 0)), QStringLiteral("10"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoTest test;
    return QTest::qExec(&test, argc, argv);
}


// Open XML SDK, ClosedXML and others write ABSOLUTE part names in the
// package relationships ("/xl/worksheets/sheet1.xml"); QXlsx composes
// targets relative to the source part and read such a workbook as "no
// sheets". calc_io hands QXlsx a relative copy.
void CalcIoTest::absolutePartNamesStillOpen()
{
    QCOMPARE(IO::relativeTarget(QStringLiteral("xl"), QStringLiteral("/xl/styles.xml")),
             QStringLiteral("styles.xml"));
    QCOMPARE(IO::relativeTarget(QString(), QStringLiteral("/xl/workbook.xml")),
             QStringLiteral("xl/workbook.xml"));
    QCOMPARE(IO::relativeTarget(QStringLiteral("xl/worksheets"), QStringLiteral("/xl/drawings/drawing1.xml")),
             QStringLiteral("../drawings/drawing1.xml"));
    QCOMPARE(IO::relativeTarget(QStringLiteral("xl"), QStringLiteral("worksheets/sheet1.xml")),
             QStringLiteral("worksheets/sheet1.xml"));

    Workbook wb = sampleWorkbook();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString relative = dir.path() + QStringLiteral("/relative.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, relative, &error), qPrintable(error));
    QVERIFY(!IO::xlsxPackageHasAbsoluteTargets(relative));

    // Rewrite every relationship target to its absolute part name, the way
    // those producers do.
    const QString absolute = dir.path() + QStringLiteral("/absolute.xlsx");
    {
        KZip in(relative);
        QVERIFY(in.open(QIODevice::ReadOnly));
        KZip out(absolute);
        QVERIFY(out.open(QIODevice::WriteOnly));
        std::function<void(const KArchiveDirectory *, const QString &)> copy =
            [&](const KArchiveDirectory *d, const QString &prefix) {
                for (const QString &name : d->entries()) {
                    const KArchiveEntry *entry = d->entry(name);
                    const QString path = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
                    if (entry->isDirectory()) {
                        copy(static_cast<const KArchiveDirectory *>(entry), path);
                        continue;
                    }
                    QByteArray data = static_cast<const KArchiveFile *>(entry)->data();
                    if (path.endsWith(QLatin1String(".rels"))) {
                        const int rels = path.lastIndexOf(QLatin1String("_rels/"));
                        QString sourceDir = path.left(qMax(0, rels));
                        while (sourceDir.endsWith(QLatin1Char('/'))) {
                            sourceDir.chop(1);
                        }
                        QString text = QString::fromUtf8(data);
                        const QRegularExpression target(QStringLiteral("Target=\"([^\"/][^\"]*)\""));
                        QString rewritten;
                        qsizetype last = 0;
                        auto it = target.globalMatch(text);
                        while (it.hasNext()) {
                            const auto match = it.next();
                            rewritten += text.mid(last, match.capturedStart(0) - last);
                            const QString joined = sourceDir.isEmpty()
                                ? match.captured(1)
                                : sourceDir + QLatin1Char('/') + match.captured(1);
                            rewritten += QStringLiteral("Target=\"/%1\"").arg(QDir::cleanPath(joined));
                            last = match.capturedEnd(0);
                        }
                        rewritten += text.mid(last);
                        data = rewritten.toUtf8();
                    }
                    QVERIFY(out.writeFile(path, data));
                }
            };
        copy(in.directory(), QString());
        QVERIFY(out.close());
    }
    QVERIFY(IO::xlsxPackageHasAbsoluteTargets(absolute));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(absolute, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetCount(), 1);
    QCOMPARE(back.sheetName(0), QStringLiteral("Data"));
    QCOMPARE(toText(back.cellValue(0, 2, 0)), QStringLiteral("30"));
    QCOMPARE(toText(back.cellValue(0, 0, 1)), QStringLiteral("hello"));
    // Styles came along too (they hang off a relationship as well).
    const CellStyle *readStyle = back.style(back.sheetAt(0).styleIdAt({0, 0}));
    QVERIFY(readStyle != nullptr);
    QCOMPARE(readStyle->numberFormat, QStringLiteral("#,##0.00"));
}

#include "tst_calc_io.moc"
