// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io table gates (M36): structured tables read from table parts
// (names, ranges, style info, columns), round-trip through the post-pass,
// and follow structural edits. The banding itself is engine + paint level.

#include "xlsx_bridge.h"

#include "structure.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <KArchiveFile>
#include <KZip>

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcIoTablesTest : public QObject {
    Q_OBJECT
private slots:
    void tablesRoundTrip();
    void tablesFollowStructuralEdits();
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

static Workbook tabledWorkbook()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Ledger"));
    for (int r = 0; r < 6; ++r) {
        wb.setCellInput(0, {r, 0}, QStringLiteral("H%1").arg(r));
        wb.setCellInput(0, {r, 1}, QString::number(r * 10));
    }
    TableSpec table;
    table.name = QStringLiteral("OrderLedger");
    table.range = {{1, 0}, {5, 3}};
    table.styleName = QStringLiteral("TableStyleMedium2");
    table.columnNames = {QStringLiteral("Month"), QStringLiteral("Revenue"),
                         QStringLiteral("Cost"), QStringLiteral("Profit")};
    wb.sheetAt(0).setTables({table});
    return wb;
}

void CalcIoTablesTest::tablesRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/table.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(tabledWorkbook(), path, &error), qPrintable(error));

    // The package shape an external consumer reads.
    const QString tableXml = readPart(path, QStringLiteral("xl/tables/table1.xml"));
    QVERIFY(tableXml.contains(QStringLiteral("displayName=\"OrderLedger\"")));
    QVERIFY(tableXml.contains(QStringLiteral("ref=\"A2:D6\"")));
    QVERIFY(tableXml.contains(QStringLiteral("TableStyleMedium2")));
    QVERIFY(tableXml.contains(QStringLiteral("name=\"Profit\"")));
    const QString contentTypes = readPart(path, QStringLiteral("[Content_Types].xml"));
    QVERIFY(contentTypes.contains(QStringLiteral("tables/table1.xml")));
    const QString sheetXml = readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheetXml.contains(QStringLiteral("<tableParts count=\"1\">")));
    QVERIFY(sheetXml.indexOf(QStringLiteral("</sheetData>"))
            < sheetXml.indexOf(QStringLiteral("<tableParts")));
    QVERIFY(sheetXml.indexOf(QStringLiteral("<tableParts"))
            < sheetXml.indexOf(QStringLiteral("</worksheet>")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const QList<TableSpec> tables = back.sheetAt(0).tables();
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables.at(0).name, QStringLiteral("OrderLedger"));
    QCOMPARE(tables.at(0).range.topLeft.row, 1);
    QCOMPARE(tables.at(0).range.bottomRight.row, 5);
    QCOMPARE(tables.at(0).range.bottomRight.col, 3);
    QCOMPARE(tables.at(0).styleName, QStringLiteral("TableStyleMedium2"));
    QVERIFY(tables.at(0).headerRow);
    QVERIFY(!tables.at(0).totalsRow);
    QCOMPARE(tables.at(0).columnNames.size(), 4);
    QCOMPARE(tables.at(0).columnNames.at(3), QStringLiteral("Profit"));
}

void CalcIoTablesTest::tablesFollowStructuralEdits()
{
    Workbook wb = tabledWorkbook();
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    const TableSpec &moved = wb.sheetAt(0).tables().at(0);
    QCOMPARE(moved.range.topLeft.row, 3);
    QCOMPARE(moved.range.bottomRight.row, 7);

    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 5, 1}); // inside the table
    QCOMPARE(wb.sheetAt(0).tables().at(0).range.bottomRight.row, 8);

    // Removing every table row drops the table.
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 3, 6});
    QCOMPARE(wb.sheetAt(0).tables().size(), 0);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoTablesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_tables.moc"
