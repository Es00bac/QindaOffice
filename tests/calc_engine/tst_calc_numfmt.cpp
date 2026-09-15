// SPDX-License-Identifier: GPL-3.0-or-later
// Number-format compiler/formatter (plan section 7.2), fixture-driven from
// tests/fixtures/numfmt.tsv plus date/colour/text-section cases.

#include "number_format.h"

#include <QCoreApplication>
#include <QFile>
#include <QtTest>

using namespace QindaOffice::Calc;

class CalcNumfmtTest : public QObject {
    Q_OBJECT
private slots:
    void fixture();
    void generalForm();
    void sectionsAndColour();
    void datesAndTimes();
    void builtinTable();
};

void CalcNumfmtTest::fixture()
{
    QFile file(QStringLiteral(SOURCE_DIR "/../fixtures/numfmt.tsv"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    int rows = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("# ")) {
            continue;
        }
        const QList<QByteArray> cols = line.split('\t');
        QVERIFY2(cols.size() == 3, "numfmt row must be code<TAB>input<TAB>expected");
        const QString code = QString::fromUtf8(cols.at(0));
        const QString input = QString::fromUtf8(cols.at(1));
        const QString expected = QString::fromUtf8(cols.at(2));
        bool ok = false;
        const double num = input.toDouble(&ok);
        const Value value = ok ? Value(num) : Value(input);
        QCOMPARE(formatWithCode(value, code).text, expected);
        ++rows;
    }
    QVERIFY(rows >= 20);
}

void CalcNumfmtTest::generalForm()
{
    NumberFormat general;
    QVERIFY(general.isGeneral());
    QCOMPARE(general.formatNumber(42).text, QStringLiteral("42"));
    QCOMPARE(general.formatNumber(-3.5).text, QStringLiteral("-3.5"));
    QCOMPARE(general.formatNumber(123456789012.0).text,
             QStringLiteral("1.2345678901e+11")); // >11 significant -> scientific
}

void CalcNumfmtTest::sectionsAndColour()
{
    NumberFormat fmt(QStringLiteral("0.0;[Red]-0.0;\"zero\""));
    QCOMPARE(fmt.formatNumber(3.25).text, QStringLiteral("3.3"));
    const FormattedText neg = fmt.formatNumber(-3.25);
    QCOMPARE(neg.text, QStringLiteral("-3.3"));
    QVERIFY(neg.colour.has_value());
    QCOMPARE(fmt.formatNumber(0).text, QStringLiteral("zero"));

    NumberFormat text(QStringLiteral("@ items"));
    QCOMPARE(text.formatText(QStringLiteral("apple")).text, QStringLiteral("apple items"));

    NumberFormat thousands(QStringLiteral("#,##0"));
    QCOMPARE(thousands.formatNumber(1234567).text, QStringLiteral("1,234,567"));
    QCOMPARE(thousands.formatNumber(999).text, QStringLiteral("999"));
}

void CalcNumfmtTest::datesAndTimes()
{
    // Serial 45366 = 2024-03-15 (Excel 1900 system).
    const Value date(45366.0);
    QCOMPARE(formatWithCode(date, QStringLiteral("yyyy-MM-dd")).text,
             QStringLiteral("2024-03-15"));
    QCOMPARE(formatWithCode(date, QStringLiteral("d-mmm-yy")).text,
             QStringLiteral("15-Mar-24"));
    QCOMPARE(formatWithCode(date, QStringLiteral("dddd")).text,
             QStringLiteral("Friday"));
    QCOMPARE(formatWithCode(Value(0.5), QStringLiteral("h:mm")).text,
             QStringLiteral("12:00"));
    QCOMPARE(formatWithCode(Value(0.5), QStringLiteral("h:mm AM/PM")).text,
             QStringLiteral("12:00 PM"));
    QCOMPARE(formatWithCode(Value(1.25), QStringLiteral("[h]:mm")).text,
             QStringLiteral("30:00"));
}

void CalcNumfmtTest::builtinTable()
{
    QCOMPARE(NumberFormat::builtinCode(14), QStringLiteral("mm-dd-yy"));
    QCOMPARE(NumberFormat::builtinCode(22), QStringLiteral("m/d/yy h:mm"));
    QCOMPARE(NumberFormat::builtinCode(0), QStringLiteral("General"));
    QCOMPARE(NumberFormat::builtinCode(9999), QStringLiteral("General"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcNumfmtTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_numfmt.moc"
