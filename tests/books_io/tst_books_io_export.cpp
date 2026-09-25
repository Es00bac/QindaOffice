// SPDX-License-Identifier: GPL-3.0-or-later
// M46: general journal CSV, list CSV export → import round trip into a
// second company, QuickBooks IIF, timestamped backups.
#include "books_io_fixture.h"

#include "backups.h"
#include "csv_util.h"
#include "ledger_export.h"

#include <QTemporaryDir>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

class TestBooksIoExport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void journalCsvBalances();
    void listsRoundTripIntoAnotherCompany();
    void importReportsBadRows();
    void iifListsAndBalancedEntries();
    void timestampedBackupsKeepTheNewest();

private:
    QTemporaryDir m_dir;
    Company m_company;
};

void TestBooksIoExport::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_company = build(m_dir.filePath(QStringLiteral("northwind.qbooks")));
}

void TestBooksIoExport::journalCsvBalances()
{
    const QByteArray csv = LedgerExport::journalCsv(*m_company.file);
    const QList<QStringList> rows = Csv::parse(QString::fromUtf8(csv), QLatin1Char(','));
    QCOMPARE(rows.first(), (QStringList{QStringLiteral("Date"), QStringLiteral("Type"), QStringLiteral("Num"),
                                        QStringLiteral("Name"), QStringLiteral("Account"), QStringLiteral("Debit"),
                                        QStringLiteral("Credit"), QStringLiteral("Memo")}));
    qint64 debit = 0;
    qint64 credit = 0;
    bool sawInvoice = false;
    for (qsizetype i = 1; i < rows.size(); ++i) {
        debit += Money::parse(rows[i][5].isEmpty() ? QStringLiteral("0") : rows[i][5], QStringLiteral("USD"))->minor();
        credit += Money::parse(rows[i][6].isEmpty() ? QStringLiteral("0") : rows[i][6], QStringLiteral("USD"))->minor();
        sawInvoice = sawInvoice || (rows[i][1] == QLatin1String("Invoice") && rows[i][4] == QLatin1String("Accounts Receivable"));
    }
    QVERIFY(debit > 0);
    QCOMPARE(debit, credit);
    QVERIFY(sawInvoice);
    // A date range narrows it: nothing before the opening balance date.
    const QList<QStringList> none =
        Csv::parse(QString::fromUtf8(LedgerExport::journalCsv(*m_company.file, QDate(2020, 1, 1), QDate(2020, 12, 31))),
                   QLatin1Char(','));
    QCOMPARE(none.size(), 1);
}

void TestBooksIoExport::listsRoundTripIntoAnotherCompany()
{
    CompanyFile &source = *m_company.file;
    CompanySettings settings;
    settings.legalName = QStringLiteral("Fresh Books Inc.");
    QString error;
    // Into a company started from ANOTHER template: the creative chart's
    // own accounts arrive, shared names are skipped, and accounts whose
    // number the target already uses for something else are reported
    // (never renumbered or merged silently).
    std::unique_ptr<CompanyFile> general =
        CompanyFile::create(m_dir.filePath(QStringLiteral("general.qbooks")), settings, QStringLiteral("general"), &error);
    QVERIFY2(general, qPrintable(error));
    const QByteArray chart = LedgerExport::listCsv(source, ListKind::Accounts);
    const ImportReport accounts = ListImport::importCsv(*general, ListKind::Accounts, chart);
    QCOMPARE(accounts.added + accounts.skipped + int(accounts.errors.size()),
             int(ChartOfAccounts(source).accounts(true).size()));
    QVERIFY(accounts.added >= 8); // Payment Processor Clearing, Camera and Audio Gear …
    QVERIFY(accounts.skipped > 0); // Checking, Accounts Receivable … exist in both
    QVERIFY(!accounts.errors.isEmpty());
    for (const QString &e : accounts.errors) {
        QVERIFY2(e.contains(QStringLiteral("is already used")), qPrintable(e));
    }
    QVERIFY(ChartOfAccounts(*general).find(QStringLiteral("Payment Processor Clearing")) != 0);
    QVERIFY(ChartOfAccounts(*general).find(QStringLiteral("Owner's Equity:Owner Draws")) != 0);

    // Into a company on the SAME chart: every account is already there,
    // and the name lists round-trip byte for byte.
    std::unique_ptr<CompanyFile> target =
        CompanyFile::create(m_dir.filePath(QStringLiteral("target.qbooks")), settings, QStringLiteral("creative"), &error);
    QVERIFY2(target, qPrintable(error));
    const ImportReport same = ListImport::importCsv(*target, ListKind::Accounts, chart);
    QVERIFY2(same.errors.isEmpty(), qPrintable(same.errors.join(QLatin1Char('\n'))));
    QCOMPARE(same.added, 0);
    QCOMPARE(same.skipped, int(ChartOfAccounts(source).accounts(true).size()));

    for (ListKind kind : {ListKind::Customers, ListKind::Vendors, ListKind::Items}) {
        const QByteArray exported = LedgerExport::listCsv(source, kind);
        const ImportReport report = ListImport::importCsv(*target, kind, exported);
        QVERIFY2(report.errors.isEmpty(), qPrintable(report.errors.join(QLatin1Char('\n'))));
        QVERIFY(report.added > 0);
        QCOMPARE(LedgerExport::listCsv(*target, kind), exported); // identical round trip
        const ImportReport again = ListImport::importCsv(*target, kind, exported);
        QCOMPARE(again.added, 0);
        QCOMPARE(again.skipped, report.added);
    }
    const std::optional<Item> print = Lists(*target).item(Lists(*target).findItem(QStringLiteral("Printed menus")));
    QVERIFY(print);
    QCOMPARE(print->price.minor(), qint64(185));
    QVERIFY(print->taxable);
    QCOMPARE(print->kind, ItemKind::NonInventory);
}

void TestBooksIoExport::importReportsBadRows()
{
    CompanyFile &f = *m_company.file;
    const QByteArray accounts = "Name,Type,Number\nTravel:Per Diem,expense,6810\nMystery,Assets,\nNope:Child,expense,\n"
                                "Studio Equipment Reserve,Other Current Asset,1490\n";
    const ImportReport r = ListImport::importCsv(f, ListKind::Accounts, accounts);
    QCOMPARE(r.added, 2);
    QCOMPARE(r.errors.size(), 2);
    QVERIFY(r.errors[0].startsWith(QStringLiteral("row 3:")));
    QVERIFY(r.errors[1].contains(QStringLiteral("parent")));
    QVERIFY(ChartOfAccounts(f).find(QStringLiteral("Travel:Per Diem")) != 0);
    // QuickBooks-style customer headers are understood.
    const QByteArray customers = "Display Name,E-mail,Phone Number,Billing Address\n"
                                 "\"Lumen Yoga, LLC\",hi@lumen.yoga,555-0101,\"9 Pine St\nBend, OR\"\n";
    const ImportReport c = ListImport::importCsv(f, ListKind::Customers, customers);
    QCOMPARE(c.added, 1);
    const std::optional<Party> p =
        Lists(f).party(Lists(f).findParty(PartyKind::Customer, QStringLiteral("Lumen Yoga, LLC")));
    QVERIFY(p);
    QCOMPARE(p->email, QStringLiteral("hi@lumen.yoga"));
    QCOMPARE(p->billingAddress, QStringLiteral("9 Pine St\nBend, OR"));
    QCOMPARE(ListImport::importCsv(f, ListKind::Items, "Price\n1\n").errors.size(), 1); // no Name column
}

void TestBooksIoExport::iifListsAndBalancedEntries()
{
    const QByteArray iif = LedgerExport::iif(*m_company.file);
    const QStringList lines = QString::fromUtf8(iif).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QVERIFY(lines.first().startsWith(QStringLiteral("!ACCNT\tNAME\tACCNTTYPE")));
    QVERIFY(lines.contains(QStringLiteral("ACCNT\tChecking\tBANK\t\t1000")));
    QVERIFY(lines.contains(QStringLiteral("ACCNT\tAccounts Receivable\tAR\t\t1100")));
    QVERIFY(lines.contains(QStringLiteral("ACCNT\tOwner's Equity:Owner Draws\tEQUITY\t\t3120")));
    for (const QString &header : {QStringLiteral("!CUST\t"), QStringLiteral("!VEND\t"), QStringLiteral("!INVITEM\t"),
                                  QStringLiteral("!TRNS\t"), QStringLiteral("!SPL\t")}) {
        QVERIFY2(std::any_of(lines.cbegin(), lines.cend(), [&](const QString &l) { return l.startsWith(header); }),
                 qPrintable(header));
    }
    QVERIFY(std::any_of(lines.cbegin(), lines.cend(),
                        [](const QString &l) { return l.startsWith(QStringLiteral("CUST\tHarbor & Pine Coffee Co.\t")); }));
    QVERIFY(std::any_of(lines.cbegin(), lines.cend(), [](const QString &l) {
        return l.startsWith(QStringLiteral("VEND\tPixel Print Works\t")) && l.contains(QStringLiteral("\tY\t"));
    }));
    // Every TRNS … ENDTRNS block balances to zero; one block per live
    // transaction (opening balance, invoice, payment, expense).
    int blocks = 0;
    qint64 balance = 0;
    bool inBlock = false;
    for (const QString &line : lines) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.first() == QLatin1String("TRNS")) {
            QVERIFY(!inBlock);
            inBlock = true;
            balance = 0;
            QCOMPARE(f.value(2), QStringLiteral("GENERAL JOURNAL"));
            QVERIFY(QDate::fromString(f.value(3), QStringLiteral("MM/dd/yyyy")).isValid());
        }
        if (f.first() == QLatin1String("TRNS") || f.first() == QLatin1String("SPL")) {
            QVERIFY(!f.value(4).isEmpty()); // account
            balance += Money::parse(f.value(7), QStringLiteral("USD")).value_or(Money()).minor();
        }
        if (f.first() == QLatin1String("ENDTRNS")) {
            QVERIFY(inBlock);
            QCOMPARE(balance, qint64(0));
            inBlock = false;
            ++blocks;
        }
    }
    QVERIFY(!inBlock);
    QCOMPARE(blocks, 4);
    // Lists only / journal only.
    QVERIFY(!LedgerExport::iif(*m_company.file, {true, false, {}, {}}).contains("!TRNS"));
    QVERIFY(!LedgerExport::iif(*m_company.file, {false, true, {}, {}}).contains("!ACCNT"));
}

void TestBooksIoExport::timestampedBackupsKeepTheNewest()
{
    CompanyFile &f = *m_company.file;
    const QString dir = m_dir.filePath(QStringLiteral("backups"));
    QStringList made;
    for (int i = 0; i < 3; ++i) {
        QString path;
        const Result r = Backups::createTimestamped(f, dir, 2, &path);
        QVERIFY2(r.ok, qPrintable(r.error));
        made << path;
    }
    const QStringList kept = Backups::list(f, dir);
    QCOMPARE(kept.size(), 2);
    QCOMPARE(kept, made.mid(1)); // the oldest went
    QString error;
    std::unique_ptr<CompanyFile> copy = CompanyFile::open(kept.last(), &error);
    QVERIFY2(copy, qPrintable(error));
    QCOMPARE(copy->settings().legalName, QStringLiteral("Northwind Creative Studio LLC"));
    QVERIFY(copy->integrityProblems().isEmpty());
}

QTEST_MAIN(TestBooksIoExport)
#include "tst_books_io_export.moc"
