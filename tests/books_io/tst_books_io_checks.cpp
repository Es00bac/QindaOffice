// SPDX-License-Identifier: GPL-3.0-or-later
// M63 (D-071 item 7): check printing. The amount in words; a written check
// and a bill payment become PrintableChecks (payee, window-envelope
// address, figures, words, memo, the stub's lines); what is not a bank
// check is refused with the reason; the PDF has one check per page, the
// values in the check third where voucher stock expects them, and both
// stubs in full.

#include "books_io_fixture.h"

#include "check_print.h"
#include "journal.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

DocumentLine accountLine(qint64 account, const char *description, qint64 cents)
{
    DocumentLine l;
    l.accountId = account;
    l.description = str(description);
    l.amount = usd(cents);
    return l;
}

// A check to the fixture's vendor: two lines, a number and a printed memo.
qint64 writeCheck(Company &c, int lines = 2)
{
    ChartOfAccounts chart(*c.file);
    BusinessDocument check;
    check.kind = DocKind::Check;
    check.partyId = c.vendor;
    check.date = QDate(2026, 3, 9);
    check.accountId = c.checking;
    check.number = str("1042");
    check.message = str("Spring menu proofs");
    check.memo = str("internal: approved by Maya");
    check.lines = {accountLine(chart.find(str("Production Costs")), "Proof prints", 36000),
                   accountLine(chart.find(str("Office Supplies")), "Mounting board", 8999)};
    for (int i = 2; i < lines; ++i) {
        check.lines << accountLine(chart.find(str("Office Supplies")), "Sundries", 100);
    }
    const Result r = Purchases(*c.file).createExpense(check);
    if (!r.ok) {
        qFatal("check: %s", qPrintable(r.error));
    }
    return r.id;
}

qint64 bill(Company &c, const char *number, qint64 cents, const QDate &date)
{
    BusinessDocument b;
    b.kind = DocKind::Bill;
    b.partyId = c.vendor;
    b.date = date;
    b.number = str(number);
    b.lines = {accountLine(ChartOfAccounts(*c.file).find(str("Production Costs")), "Print run", cents)};
    return Purchases(*c.file).createBill(b).id;
}

// Text Poppler finds inside `band` (points from the sheet's top-left).
QString textIn(const QString &pdf, int page, const QRectF &band)
{
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(pdf);
    return doc && page < doc->numPages() ? doc->page(page)->text(band).simplified() : QString();
}

} // namespace

class TestBooksIoChecks final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void amountInWords_data()
    {
        QTest::addColumn<qint64>("minor");
        QTest::addColumn<QString>("currency");
        QTest::addColumn<QString>("words");
        QTest::newRow("thousands") << qint64(123456) << str("USD") << str("One thousand two hundred thirty-four and 56/100");
        QTest::newRow("cents only") << qint64(5) << str("USD") << str("Zero and 05/100");
        QTest::newRow("round hundred") << qint64(10000) << str("USD") << str("One hundred and 00/100");
        QTest::newRow("teens") << qint64(11500) << str("USD") << str("One hundred fifteen and 00/100");
        QTest::newRow("hyphen") << qint64(2100) << str("USD") << str("Twenty-one and 00/100");
        QTest::newRow("empty groups") << qint64(100000001) << str("USD") << str("One million and 01/100");
        QTest::newRow("mixed groups") << qint64(200501990) << str("USD")
                                      << str("Two million five thousand nineteen and 90/100");
        QTest::newRow("no minor units") << qint64(15000) << str("JPY") << str("Fifteen thousand");
        QTest::newRow("three digits") << qint64(1234) << str("KWD") << str("One and 234/1000");
        QTest::newRow("negative") << qint64(-500) << str("USD") << QString();
    }

    void amountInWords()
    {
        QFETCH(qint64, minor);
        QFETCH(QString, currency);
        QFETCH(QString, words);
        QCOMPARE(QindaOffice::Books::amountInWords(Money(minor, currency)), words);
    }

    void theLargestAmountStillReads()
    {
        const QString words = QindaOffice::Books::amountInWords(Money(std::numeric_limits<qint64>::max(), str("USD")));
        QVERIFY2(words.startsWith(str("Ninety-two quadrillion two hundred thirty-three trillion")), qPrintable(words));
        QVERIFY(words.endsWith(str("and 07/100")));
    }

    void aWrittenCheckPrints()
    {
        Company c = build(m_dir.filePath(str("written.qbooks")));
        PrintableCheck p;
        QString error;
        QVERIFY2(buildPrintableCheck(*c.file, writeCheck(c), &p, &error), qPrintable(error));
        QCOMPARE(p.number, str("1042"));
        QCOMPARE(p.date, str("03/09/2026"));
        QCOMPARE(p.payee, str("Pixel Print Works"));
        QCOMPARE(p.address, (QStringList{str("Pixel Print Works"), str("77 SE Water Ave"), str("Portland, OR 97214")}));
        QCOMPARE(p.amount, str("**449.99"));
        QCOMPARE(p.amountWords, str("Four hundred forty-nine and 99/100"));
        QCOMPARE(p.memo, str("Spring menu proofs")); // the printed note, never the internal memo
        QCOMPARE(p.bankAccount, str("1000 Checking"));
        QCOMPARE(p.total, str("$449.99"));
        QCOMPARE(p.companyName, str("Northwind Creative"));
        QCOMPARE(p.columns.size(), 3);
        QCOMPARE(p.rows.size(), 2);
        QCOMPARE(p.rows.at(0), (QStringList{str("5000 Production Costs"), str("Proof prints"), str("360.00")}));
        QCOMPARE(p.rows.at(1).at(2), str("89.99"));
    }

    void aBillPaymentPrintsTheBillsItPays()
    {
        Company c = build(m_dir.filePath(str("billpay.qbooks")));
        const qint64 b1 = bill(c, "PPW-2207", 120000, QDate(2026, 2, 20));
        const qint64 b2 = bill(c, "PPW-2231", 45050, QDate(2026, 3, 1));
        QList<qint64> ids;
        const Result paid = Purchases(*c.file).payBills(QDate(2026, 3, 20), c.checking,
                                                        {{b1, usd(120000)}, {b2, usd(20000)}}, str("1043"), {}, &ids);
        QVERIFY2(paid.ok, qPrintable(paid.error));
        QCOMPARE(ids.size(), 1);
        PrintableCheck p;
        QVERIFY(buildPrintableCheck(*c.file, ids.first(), &p));
        QCOMPARE(p.number, str("1043")); // a numeric reference is the check number
        QCOMPARE(p.amount, str("**1,400.00"));
        QCOMPARE(p.amountWords, str("One thousand four hundred and 00/100"));
        QCOMPARE(p.columns.size(), 4);
        QCOMPARE(p.rows.size(), 2);
        QCOMPARE(p.rows.at(0), (QStringList{str("02/20/2026"), str("PPW-2207"), str("1,200.00"), str("1,200.00")}));
        QCOMPARE(p.rows.at(1), (QStringList{str("03/01/2026"), str("PPW-2231"), str("450.50"), str("200.00")}));
    }

    void whatIsNotABankCheckIsRefused()
    {
        Company c = build(m_dir.filePath(str("refused.qbooks")));
        QString why;
        // The fixture's expense is a debit-card expense, the invoice a sale.
        const QList<BusinessDocument> expenses = Documents(*c.file).list(DocumentFilter{{DocKind::Expense}, 0, {}, {}, false, true});
        QCOMPARE(expenses.size(), 1);
        QVERIFY(!isPrintableCheck(*c.file, expenses.first().id, &why));
        QVERIFY(why.contains(str("only checks and bill payments")));
        QVERIFY(!isPrintableCheck(*c.file, c.invoice, &why));
        QVERIFY(!isPrintableCheck(*c.file, 999999, &why));
        QCOMPARE(why, str("no such document"));
        // A bill paid by credit card is not a check.
        const qint64 b = bill(c, "PPW-3001", 5000, QDate(2026, 3, 2));
        QList<qint64> ids;
        QVERIFY(Purchases(*c.file)
                    .payBills(QDate(2026, 3, 5), ChartOfAccounts(*c.file).find(str("Business Credit Card")),
                              {{b, usd(5000)}}, {}, {}, &ids)
                    .ok);
        QVERIFY(!isPrintableCheck(*c.file, ids.first(), &why));
        QVERIFY(why.contains(str("bank account")));
        // A void check is not printed.
        const qint64 check = writeCheck(c);
        QVERIFY(isPrintableCheck(*c.file, check));
        QVERIFY(Ledger(*c.file).voidDocument(check, str("printed twice")).ok);
        QVERIFY(!isPrintableCheck(*c.file, check, &why));
        QVERIFY(why.contains(str("void")));
        PrintableCheck p;
        QVERIFY(!buildPrintableCheck(*c.file, check, &p, &why));
        QVERIFY(!CheckRenderer::writeChecksPdf(*c.file, {check}, m_dir.filePath(str("void.pdf")), &why));
        QVERIFY(!QFileInfo::exists(m_dir.filePath(str("void.pdf"))));
    }

    void onePagePerCheckWithTheValuesWhereTheStockWantsThem()
    {
        Company c = build(m_dir.filePath(str("pdf.qbooks")));
        const qint64 first = writeCheck(c);
        const qint64 second = writeCheck(c, 30); // more lines than a stub holds
        const QString pdf = m_dir.filePath(str("checks.pdf"));
        QString error;
        QVERIFY2(CheckRenderer::writeChecksPdf(*c.file, {first, second}, pdf, &error), qPrintable(error));
        const QStringList pages = pdfPages(pdf);
        QCOMPARE(pages.size(), 2);
        // The check third (top 3.5 in = 252 pt), band by band.
        QVERIFY(textIn(pdf, 0, QRectF(430, 25, 182, 45)).contains(str("03/09/2026")));        // date, top right
        const QString payeeLine = textIn(pdf, 0, QRectF(60, 72, 552, 34));
        QVERIFY2(payeeLine.contains(str("Pixel Print Works")) && payeeLine.contains(str("**449.99")), qPrintable(payeeLine));
        const QString words = textIn(pdf, 0, QRectF(20, 100, 520, 30));
        QVERIFY2(words.contains(str("Four hundred forty-nine and 99/100 ***")), qPrintable(words)); // starred to the end
        const QString address = textIn(pdf, 0, QRectF(50, 128, 260, 72));
        QVERIFY2(address.contains(str("77 SE Water Ave")) && address.contains(str("Portland, OR 97214")), qPrintable(address));
        QVERIFY(textIn(pdf, 0, QRectF(40, 195, 300, 25)).contains(str("Spring menu proofs")));    // memo line
        QVERIFY(!pages.at(0).contains(str("internal: approved")));
        // Both stubs, in full.
        for (const qreal top : {252.0, 504.0}) {
            const QString stub = textIn(pdf, 0, QRectF(0, top, 612, 252));
            for (const char *expected : {"Pixel Print Works", "No. 1042", "5000 Production Costs", "Mounting board",
                                         "Check amount", "$449.99", "1000 Checking"}) {
                QVERIFY2(stub.contains(str(expected)), expected);
            }
        }
        // Page 2: thirty lines do not fit a stub; it says how many it left out.
        QVERIFY(QRegularExpression(str("\\+ \\d+ more")).match(textIn(pdf, 1, QRectF(0, 252, 612, 252))).hasMatch());
        QVERIFY(pages.at(1).contains(str("$477.99")));
        saveGalleryPng(pdf, str("books-check-voucher.png"));
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestBooksIoChecks)

#include "tst_books_io_checks.moc"
