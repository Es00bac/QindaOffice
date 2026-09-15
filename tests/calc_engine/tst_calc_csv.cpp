// SPDX-License-Identifier: GPL-3.0-or-later
// RFC 4180 CSV reader/writer (plan section 7.2): quotes, doubled quotes,
// embedded newlines, CRLF, BOM, separator auto-detection and quoting on
// export only when needed.

#include "csv.h"

#include <QCoreApplication>
#include <QtTest>

using namespace QindaOffice;

class CalcCsvTest : public QObject {
    Q_OBJECT
private slots:
    void basicRoundTrip();
    void quotingAndEmbeddedNewlines();
    void crlfAndBom();
    void separatorDetection();
    void exportQuotingOnlyWhenNeeded();
    void excelCompatibleExport();
};

void CalcCsvTest::basicRoundTrip()
{
    const Csv::Table table = Csv::parse("a,b,c\n1,2,3\n");
    QCOMPARE(table.rows.size(), 2);
    QCOMPARE(table.rows.at(0), QStringList({"a", "b", "c"}));
    QCOMPARE(table.separator, QStringLiteral(","));
    QCOMPARE(table.cellAt(1, 2), QStringLiteral("3"));
    QCOMPARE(table.cellAt(9, 9), QString());
}

void CalcCsvTest::quotingAndEmbeddedNewlines()
{
    const QByteArray data = "name,note\n\"say \"\"hi\"\"\",\"line1\nline2\"\n";
    const Csv::Table table = Csv::parse(data);
    QCOMPARE(table.rows.size(), 2);
    QCOMPARE(table.cellAt(1, 0), QStringLiteral("say \"hi\""));
    QCOMPARE(table.cellAt(1, 1), QStringLiteral("line1\nline2"));
}

void CalcCsvTest::crlfAndBom()
{
    const QByteArray data = QByteArray::fromHex("efbbbf") + "a,b\r\n1,2\r\n";
    const Csv::Table table = Csv::parse(data);
    QCOMPARE(table.rows.size(), 2);
    QCOMPARE(table.cellAt(0, 0), QStringLiteral("a"));
    QCOMPARE(table.cellAt(1, 1), QStringLiteral("2"));
}

void CalcCsvTest::separatorDetection()
{
    const Csv::Table semi = Csv::parse("a;b;c\n1;2;3\n");
    QCOMPARE(semi.separator, QStringLiteral(";"));
    QCOMPARE(semi.decimal, QStringLiteral(","));
    const Csv::Table tab = Csv::parse("a\tb\n1\t2\n");
    QCOMPARE(tab.separator, QStringLiteral("\t"));
    const Csv::Table pipe = Csv::parse("a|b\n1|2\n");
    QCOMPARE(pipe.separator, QStringLiteral("|"));
}

void CalcCsvTest::exportQuotingOnlyWhenNeeded()
{
    Csv::Table table;
    table.rows = {{"plain", "with,comma"}, {"with\"quote", "multi\nline"}};
    const QByteArray out = Csv::serialize(table);
    QCOMPARE(QString::fromUtf8(out), QStringLiteral("plain,\"with,comma\"\r\n"
                                                     "\"with\"\"quote\",\"multi\nline\"\r\n"));
    const Csv::Table back = Csv::parse(out);
    QCOMPARE(back.rows, table.rows);
}

void CalcCsvTest::excelCompatibleExport()
{
    Csv::Table table;
    table.rows = {{"1", "2"}};
    const QByteArray out = Csv::serialize(table, {QStringLiteral(","), true});
    QVERIFY(out.startsWith(QByteArray::fromHex("efbbbf")));
    QVERIFY(out.contains("1;2"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcCsvTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_csv.moc"
