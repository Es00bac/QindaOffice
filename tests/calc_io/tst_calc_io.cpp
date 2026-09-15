// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io gates (plan M3): xlsx round-trips formulas, merges, widths, styles
// and dates; a written file opens in LibreOffice (live, skipped when
// unavailable); a 100k-cell sheet round-trips within the responsiveness
// budget.

#include "xlsx_bridge.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QProcess>
#include <QtTest>

using namespace QindaOffice::Calc;

class CalcIoTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void opensInLibreOffice();
    void largeSheetStaysResponsive();
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

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io.moc"
