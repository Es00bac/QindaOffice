// SPDX-License-Identifier: GPL-3.0-or-later
// M46: bank statement import — CSV (signed amount; debit/credit columns
// with dd/MM/yyyy and decimal commas), OFX 1.x SGML, QFX/OFX 2.x XML, QIF;
// exact amounts; then the engine's dedup on re-import.
#include "books_io_fixture.h"

#include "bank_import.h"

#include <QFile>
#include <QTemporaryDir>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(BOOKS_IO_FIXTURES) + QLatin1Char('/') + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

qint64 sum(const QList<BankLine> &lines)
{
    qint64 total = 0;
    for (const BankLine &l : lines) {
        total += l.amount;
    }
    return total;
}

} // namespace

class TestBooksIoBankImport : public QObject {
    Q_OBJECT

private slots:
    void amounts_data();
    void amounts();
    void dateFormats();
    void csvSignedAmount();
    void csvDebitCreditColumns();
    void csvExplicitMappingAndInvertedSign();
    void ofxSgml();
    void qfxXml();
    void qif();
    void parseFileSniffs();
    void importTwiceAddsNothing();
};

void TestBooksIoBankImport::amounts_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("exponent");
    QTest::addColumn<bool>("comma");
    QTest::addColumn<qint64>("expected");
    QTest::addColumn<bool>("valid");
    QTest::newRow("plain") << "12.34" << 2 << false << qint64(1234) << true;
    QTest::newRow("grouping") << "1,234.56" << 2 << false << qint64(123456) << true;
    QTest::newRow("thousands only") << "1,234" << 2 << false << qint64(123400) << true;
    QTest::newRow("parentheses") << "($1,084.37)" << 2 << false << qint64(-108437) << true;
    QTest::newRow("euro comma") << "-€3,50" << 2 << false << qint64(-350) << true;
    QTest::newRow("european grouping") << "1.234,56" << 2 << true << qint64(123456) << true;
    QTest::newRow("dots grouping") << "1.234.567" << 2 << false << qint64(123456700) << true;
    QTest::newRow("trailing minus") << "12.50-" << 2 << false << qint64(-1250) << true;
    QTest::newRow("DR suffix") << "40.00 DR" << 2 << false << qint64(-4000) << true;
    QTest::newRow("code") << "USD 99.95" << 2 << false << qint64(9995) << true;
    QTest::newRow("yen") << "¥1,200" << 0 << false << qint64(1200) << true;
    QTest::newRow("dinar") << "3.125" << 3 << false << qint64(3125) << true;
    QTest::newRow("extra zero decimals") << "5.500" << 2 << false << qint64(550) << true;
    QTest::newRow("too many decimals") << "5.555" << 2 << false << qint64(0) << false;
    QTest::newRow("date") << "01/15/2026" << 2 << false << qint64(0) << false;
    QTest::newRow("empty") << "" << 2 << false << qint64(0) << false;
    QTest::newRow("huge") << "92233720368547758.08" << 2 << false << qint64(0) << false;
}

void TestBooksIoBankImport::amounts()
{
    QFETCH(QString, text);
    QFETCH(int, exponent);
    QFETCH(bool, comma);
    QFETCH(qint64, expected);
    QFETCH(bool, valid);
    const std::optional<qint64> value = BankImport::parseAmount(text, exponent, comma);
    QCOMPARE(value.has_value(), valid);
    if (valid) {
        QCOMPARE(*value, expected);
    }
}

void TestBooksIoBankImport::dateFormats()
{
    using BankImport::detectDateFormat;
    QCOMPARE(detectDateFormat({QStringLiteral("2026-01-15"), QStringLiteral("2026-02-01 09:30:00")}),
             QStringLiteral("yyyy-MM-dd"));
    QCOMPARE(detectDateFormat({QStringLiteral("01/02/2026"), QStringLiteral("01/15/2026")}), QStringLiteral("MM/dd/yyyy"));
    // A day > 12 settles day-first even when month-first is preferred.
    QCOMPARE(detectDateFormat({QStringLiteral("03/02/2026"), QStringLiteral("13/02/2026")}), QStringLiteral("dd/MM/yyyy"));
    // Ambiguous: the preference decides.
    QCOMPARE(detectDateFormat({QStringLiteral("03/02/2026")}, true), QStringLiteral("dd/MM/yyyy"));
    QCOMPARE(detectDateFormat({QStringLiteral("15.01.2026")}), QStringLiteral("dd.MM.yyyy"));
    QCOMPARE(detectDateFormat({QStringLiteral("20260115")}), QStringLiteral("yyyyMMdd"));
    QCOMPARE(BankImport::parseDate(QStringLiteral("15 Jan 2026"), detectDateFormat({QStringLiteral("15 Jan 2026")})),
             QDate(2026, 1, 15));
    QCOMPARE(BankImport::parseDate(QStringLiteral("Jan 5, 2026"), detectDateFormat({QStringLiteral("Jan 5, 2026")})),
             QDate(2026, 1, 5));
    QCOMPARE(BankImport::parseDate(QStringLiteral("1/5/26"), detectDateFormat({QStringLiteral("1/5/26")})),
             QDate(2026, 1, 5));
    QVERIFY(detectDateFormat({QStringLiteral("not a date")}).isEmpty());
}

void TestBooksIoBankImport::csvSignedAmount()
{
    const BankParseResult r = BankImport::parseCsv(fixture(QStringLiteral("bank_signed.csv")), 2);
    QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.value(0).message));
    QCOMPARE(r.mapping.dateColumn, 1);
    QCOMPARE(r.mapping.payeeColumn, 2);
    QCOMPARE(r.mapping.amountColumn, 3);
    QCOMPARE(r.mapping.checkColumn, 6);
    QCOMPARE(r.lines.size(), 8);
    QCOMPARE(r.lines[0].date, QDate(2026, 1, 2));
    QCOMPARE(r.lines[0].amount, qint64(-5999));
    QCOMPARE(r.lines[0].payee, QStringLiteral("ADOBE  *CREATIVE CLOUD 408-536-6000 CA"));
    QCOMPARE(r.lines[1].amount, qint64(245000));
    QCOMPARE(r.lines[2].checkNumber, QStringLiteral("1043"));
    QCOMPARE(r.lines[5].amount, qint64(12900)); // a positive DEBIT_CARD refund stays positive
    QCOMPARE(r.lines[6].amount, qint64(-108437));
    QCOMPARE(sum(r.lines), qint64(245000 + 12900 - 5999 - 120000 - 450 - 450 - 108437 - 1500));
}

void TestBooksIoBankImport::csvDebitCreditColumns()
{
    const BankParseResult r = BankImport::parseCsv(fixture(QStringLiteral("bank_debit_credit.csv")), 2);
    QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.value(0).message));
    QCOMPARE(r.mapping.delimiter, QLatin1Char(';'));
    QVERIFY(r.mapping.decimalComma);
    QCOMPARE(r.mapping.dateFormat, QStringLiteral("dd/MM/yyyy"));
    QCOMPARE(r.mapping.debitColumn, 2);
    QCOMPARE(r.mapping.creditColumn, 3);
    QCOMPARE(r.lines.size(), 5);
    QCOMPARE(r.lines[0].date, QDate(2026, 2, 3));
    QCOMPARE(r.lines[0].amount, qint64(-145000));
    QCOMPARE(r.lines[1].amount, qint64(320000));
    QCOMPARE(r.lines[2].amount, qint64(-1200));
    QCOMPARE(r.lines[3].amount, qint64(485));
    QCOMPARE(r.lines[3].payee, QStringLiteral("CARD REFUND - TFL TRAVEL"));
}

void TestBooksIoBankImport::csvExplicitMappingAndInvertedSign()
{
    // A card export without a header where charges are positive.
    const QByteArray data = "2026-03-01,COFFEE ROASTERS,18.50\n2026-03-04,PAYMENT RECEIVED,-500.00\n";
    const CsvMapping detected = BankImport::detectCsvMapping(data);
    QVERIFY(!detected.hasHeader);
    QCOMPARE(detected.dateColumn, 0);
    QCOMPARE(detected.amountColumn, 2);
    QCOMPARE(detected.payeeColumn, 1);
    CsvMapping mapping = detected;
    mapping.invertSign = true;
    const BankParseResult r = BankImport::parseCsv(data, mapping, 2);
    QCOMPARE(r.lines.size(), 2);
    QCOMPARE(r.lines[0].amount, qint64(-1850)); // a charge: money out
    QCOMPARE(r.lines[1].amount, qint64(50000)); // a payment onto the card
    // Unreadable rows are reported with their line number, not dropped silently.
    const BankParseResult bad =
        BankImport::parseCsv("Date,Description,Amount\n2026-03-01,OK,1.00\nsoon,BAD DATE,2.00\n2026-03-02,BAD,abc\n", 2);
    QCOMPARE(bad.lines.size(), 1);
    QCOMPARE(bad.errors.size(), 2);
    QCOMPARE(bad.errors[0].row, 3);
    QCOMPARE(bad.errors[1].row, 4);
}

void TestBooksIoBankImport::ofxSgml()
{
    const BankParseResult r = BankImport::parseOfx(fixture(QStringLiteral("statement.ofx")), 2);
    QVERIFY(r.errors.isEmpty());
    QCOMPARE(r.accountId, QStringLiteral("4432019876"));
    QCOMPARE(r.currency, QStringLiteral("USD"));
    QVERIFY(!r.creditCard);
    QCOMPARE(r.ledgerBalance.value_or(0), qint64(1842153));
    QCOMPARE(r.ledgerBalanceDate, QDate(2026, 2, 28));
    QCOMPARE(r.lines.size(), 4);
    QCOMPARE(r.lines[0].fitid, QStringLiteral("202602030001"));
    QCOMPARE(r.lines[0].payee, QStringLiteral("ACME PUBLISHING & MEDIA")); // &amp; decoded
    QCOMPARE(r.lines[0].memo, QStringLiteral("INV-1001 PAYMENT"));
    QCOMPARE(r.lines[0].date, QDate(2026, 2, 3));
    QCOMPARE(r.lines[0].amount, qint64(320000));
    QCOMPARE(r.lines[1].checkNumber, QStringLiteral("1044"));
    QCOMPARE(r.lines[2].amount, qint64(-2347));
    QCOMPARE(sum(r.lines), qint64(320000 - 85000 - 2347 - 1500));
}

void TestBooksIoBankImport::qfxXml()
{
    const BankParseResult r = BankImport::parseOfx(fixture(QStringLiteral("statement.qfx")), 2);
    QVERIFY(r.errors.isEmpty());
    QVERIFY(r.creditCard);
    QCOMPARE(r.accountId, QStringLiteral("XXXXXXXXXXXX4417"));
    QCOMPARE(r.lines.size(), 3);
    QCOMPARE(r.lines[0].amount, qint64(-21240)); // a charge is money out of the card account
    QCOMPARE(r.lines[0].date, QDate(2026, 2, 4));
    QCOMPARE(r.lines[0].payee, QStringLiteral("DELTA AIR LINES"));
    QCOMPARE(r.lines[2].amount, qint64(50000));
    QCOMPARE(r.ledgerBalance.value_or(0), qint64(-127739));
}

void TestBooksIoBankImport::qif()
{
    const BankParseResult r = BankImport::parseQif(fixture(QStringLiteral("statement.qif")), 2);
    QVERIFY(r.errors.isEmpty());
    QCOMPARE(r.lines.size(), 3);
    QCOMPARE(r.lines[0].date, QDate(2026, 1, 15));
    QCOMPARE(r.lines[0].amount, qint64(-125000));
    QCOMPARE(r.lines[0].checkNumber, QStringLiteral("1045"));
    QCOMPARE(r.lines[0].payee, QStringLiteral("Studio Landlord LLC"));
    QCOMPARE(r.lines[0].memo, QStringLiteral("February rent"));
    QCOMPARE(r.lines[1].amount, qint64(480000));
    QCOMPARE(r.lines[2].date, QDate(2026, 1, 28));
    QCOMPARE(r.lines[2].amount, qint64(-3900));
    // Day-first hint for dotted / European files.
    const BankParseResult eu = BankImport::parseQif("!Type:Bank\nD05/02/2026\nT-10.00\nPX\n^\n", 2, true);
    QCOMPARE(eu.lines.value(0).date, QDate(2026, 2, 5));
    QVERIFY(!BankImport::parseQif("D1/1/26\nT1\n^\n", 2).errors.isEmpty());
}

void TestBooksIoBankImport::parseFileSniffs()
{
    const QString dir = QStringLiteral(BOOKS_IO_FIXTURES);
    QCOMPARE(BankImport::parseFile(dir + QStringLiteral("/statement.ofx"), 2).format, QStringLiteral("ofx"));
    QCOMPARE(BankImport::parseFile(dir + QStringLiteral("/statement.qfx"), 2).format, QStringLiteral("ofx"));
    QCOMPARE(BankImport::parseFile(dir + QStringLiteral("/statement.qif"), 2).format, QStringLiteral("qif"));
    QCOMPARE(BankImport::parseFile(dir + QStringLiteral("/bank_signed.csv"), 2).format, QStringLiteral("csv"));
}

void TestBooksIoBankImport::importTwiceAddsNothing()
{
    QTemporaryDir dir;
    Company c = build(dir.filePath(QStringLiteral("books.qbooks")));
    Banking banking(*c.file);
    const QString fixtures = QStringLiteral(BOOKS_IO_FIXTURES);
    for (const QString &name : {QStringLiteral("bank_signed.csv"), QStringLiteral("statement.ofx"),
                                QStringLiteral("statement.qif"), QStringLiteral("bank_debit_credit.csv")}) {
        const BankParseResult parsed = BankImport::parseFile(fixtures + QLatin1Char('/') + name, 2);
        QVERIFY(!parsed.lines.isEmpty());
        const ImportSummary first = banking.importLines(c.checking, parsed.lines, name);
        QCOMPARE(first.added, parsed.lines.size()); // the two identical coffees both survive
        QCOMPARE(first.duplicates, 0);
        const ImportSummary second = banking.importLines(c.checking, parsed.lines, name);
        QCOMPARE(second.added, 0);
        QCOMPARE(second.duplicates, parsed.lines.size());
    }
    QCOMPARE(banking.feed(c.checking).size(), 8 + 4 + 3 + 5);
}

QTEST_MAIN(TestBooksIoBankImport)
#include "tst_books_io_bank_import.moc"
