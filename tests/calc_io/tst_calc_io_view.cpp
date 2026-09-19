// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io view-state gates (M37): frozen panes, hidden sheets, the active
// tab, print areas/titles and page setup round-trip through xlsx — the
// state that makes a workbook open the way its author left it.

#include "xlsx_bridge.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <KArchiveFile>
#include <KZip>

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcIoViewTest : public QObject {
    Q_OBJECT
private slots:
    void viewStateRoundTrips();
    void printNamesAreLocalDefinedNames();
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

void CalcIoViewTest::viewStateRoundTrips()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("First"));
    wb.addSheet(QStringLiteral("Second"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("1"));
    wb.setCellInput(1, {0, 0}, QStringLiteral("2"));
    wb.sheetAt(0).setFreeze(1, 2);
    wb.sheetAt(0).setCursor({5, 3});
    wb.sheetAt(1).setHidden(true);
    Sheet::PageSetup setup;
    setup.orientation = 1;
    setup.fitToWidth = 1;
    wb.sheetAt(0).setPageSetup(setup);
    wb.setActiveTab(0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/view.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString sheetXml = readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheetXml.contains(QStringLiteral("state=\"frozen\"")));
    QVERIFY(sheetXml.contains(QStringLiteral("ySplit=\"1\"")));
    QVERIFY(sheetXml.contains(QStringLiteral("xSplit=\"2\"")));
    QVERIFY(sheetXml.contains(QStringLiteral("pageSetup")));
    QVERIFY(sheetXml.contains(QStringLiteral("landscape")));
    const QString workbookXml = readPart(path, QStringLiteral("xl/workbook.xml"));
    QVERIFY(workbookXml.contains(QStringLiteral("state=\"hidden\"")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).freezeRows(), 1);
    QCOMPARE(back.sheetAt(0).freezeCols(), 2);
    QCOMPARE(back.sheetAt(0).cursor().row, 5);
    QCOMPARE(back.sheetAt(0).cursor().col, 3);
    QVERIFY(!back.sheetAt(0).isHidden());
    QVERIFY(back.sheetAt(1).isHidden());
    QCOMPARE(back.sheetAt(0).pageSetup().orientation, 1);
    QCOMPARE(back.sheetAt(0).pageSetup().fitToWidth, 1);
    QCOMPARE(back.activeTab(), 0);
}

void CalcIoViewTest::printNamesAreLocalDefinedNames()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Report"));
    for (int r = 0; r < 20; ++r) {
        wb.setCellInput(0, {r, 0}, QString::number(r));
    }
    wb.sheetAt(0).setPrintArea(CellRange{{0, 0}, {9, 3}});
    wb.sheetAt(0).setPrintTitles(CellRange{{0, 0}, {0, Sheet::kMaxCols - 1}}, std::nullopt);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/print.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString workbookXml = readPart(path, QStringLiteral("xl/workbook.xml"));
    QVERIFY(workbookXml.contains(QStringLiteral("_xlnm.Print_Area")));
    QVERIFY(workbookXml.contains(QStringLiteral("localSheetId=\"0\"")));
    QVERIFY(workbookXml.contains(QStringLiteral("_xlnm.Print_Titles")));
    QVERIFY(workbookXml.contains(QStringLiteral("Report!$A$1:$D$10")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    QVERIFY(back.sheetAt(0).printArea().has_value());
    QCOMPARE(back.sheetAt(0).printArea()->bottomRight.row, 9);
    QCOMPARE(back.sheetAt(0).printArea()->bottomRight.col, 3);
    QVERIFY(back.sheetAt(0).printTitleRows().has_value());
    QCOMPARE(back.sheetAt(0).printTitleRows()->topLeft.row, 0);
    QVERIFY(!back.sheetAt(0).printTitleCols().has_value());
    // Built-ins are NOT user names: the formula name table stays clean.
    QVERIFY(!back.namedRange(QStringLiteral("_xlnm.Print_Area")).has_value());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoViewTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_view.moc"
