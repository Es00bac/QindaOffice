// SPDX-License-Identifier: GPL-3.0-or-later
// Company file: create/open, schema versioning (migrate forward, refuse
// newer, refuse foreign files), settings, backups, integrity, chart
// templates and the chart of accounts rules.
#include "books_fixture.h"
#include "chart_templates.h"
#include "reports.h"
#include "schema_migrations.h"
#include "sql_util.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>

using namespace BooksTest;

class TstBooksCompany : public QObject {
    Q_OBJECT
private slots:
    void createAndReopen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("studio.qbooks"));
        CompanySettings s;
        s.legalName = QStringLiteral("Example Studio LLC");
        s.currency = QStringLiteral("eur");
        s.fiscalYearStartMonth = 7;
        {
            QString error;
            auto file = CompanyFile::create(path, s, QStringLiteral("creative"), &error);
            QVERIFY2(file, qPrintable(error));
            QCOMPARE(file->currency(), QStringLiteral("EUR"));
            QCOMPARE(file->schemaVersion(), Schema::kCurrentVersion);
            QVERIFY(file->integrityProblems().isEmpty());
            QVERIFY(file->settings().defaultTermsId != 0);
        }
        // A single file: DELETE journal mode leaves no -wal/-shm behind.
        QVERIFY(!QFileInfo::exists(path + QStringLiteral("-wal")));
        ErrorCode code = ErrorCode::None;
        auto reopened = CompanyFile::open(path, nullptr, &code);
        QVERIFY(reopened);
        QCOMPARE(reopened->settings().legalName, QStringLiteral("Example Studio LLC"));
        QCOMPARE(reopened->settings().fiscalYearStartMonth, 7);
        QCOMPARE(reopened->settings().fiscalYearStart(QDate(2026, 3, 1)), QDate(2025, 7, 1));
        QCOMPARE(Sql::scalar(reopened->database(), QStringLiteral("PRAGMA journal_mode")).toString(),
                 QStringLiteral("delete"));
        QCOMPARE(Sql::scalarInt(reopened->database(), QStringLiteral("PRAGMA foreign_keys")), 1);
        // Creating over an existing file is refused.
        QVERIFY(!CompanyFile::create(path, s, {}, nullptr, &code));
        QCOMPARE(code, ErrorCode::Duplicate);
    }

    void refusesForeignAndNewerFiles()
    {
        QTemporaryDir dir;
        const QString plain = dir.filePath(QStringLiteral("notes.txt"));
        QFile f(plain);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello, not a database");
        f.close();
        ErrorCode code = ErrorCode::None;
        QString error;
        QVERIFY(!CompanyFile::open(plain, &error, &code));
        QCOMPARE(code, ErrorCode::NotACompanyFile);

        // A newer schema version: refused with a clear message.
        const QString newer = dir.filePath(QStringLiteral("newer.qbooks"));
        {
            TestCompany c;
            QVERIFY(c.f().backupTo(newer).ok);
        }
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("bump"));
            db.setDatabaseName(newer);
            QVERIFY(db.open());
            QVERIFY(Sql::exec(db, QStringLiteral("PRAGMA user_version = 99")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("bump"));
        QVERIFY(!CompanyFile::open(newer, &error, &code));
        QCOMPARE(code, ErrorCode::NewerSchema);
        QVERIFY2(error.contains(QStringLiteral("newer version")), qPrintable(error));
        QVERIFY(!CompanyFile::open(dir.filePath(QStringLiteral("missing.qbooks")), &error, &code));
        QCOMPARE(code, ErrorCode::NotFound);
    }

    void migratesForward()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("old.qbooks"));
        QString error;
        QVERIFY2(CompanyFile::createBareAtVersion(path, 1, &error), qPrintable(error));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QCOMPARE(Schema::readUserVersion(db), 1);
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO settings(key, value) VALUES('legal_name', 'Old Co')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO settings(key, value) VALUES('currency', 'USD')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));
        auto file = CompanyFile::open(path, &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->schemaVersion(), Schema::kCurrentVersion);
        QCOMPARE(file->settings().legalName, QStringLiteral("Old Co"));
        // v2 added budgets, v3 the connection tables; the pre-migration
        // copy sits next to the file.
        QCOMPARE(Sql::scalarInt(file->database(), QStringLiteral("SELECT COUNT(*) FROM connections")), 0);
        QVERIFY(Sql::exec(file->database(),
                          QStringLiteral("INSERT INTO accounts(name, type, created_at) VALUES('Rent', 14, 'now')")));
        QVERIFY(Budgets(*file).set(1, 2026, 1, 1000).ok);
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".pre-v%1.bak").arg(Schema::kCurrentVersion)));
        QCOMPARE(Ledger(*file).auditTrail(QStringLiteral("schema")).size(), 1);
    }

    // M68 (D-077): a version-3 file with books in it opens at the current
    // version (4 then; 5 since M71's payroll) — its documents and lines
    // intact, the new columns at their defaults, the time table empty and
    // the purchase-order counter seeded.
    void migratesAVersion3FileWithBooksInIt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v3.qbooks"));
        QString error;
        QVERIFY2(CompanyFile::createBareAtVersion(path, 3, &error), qPrintable(error));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v3"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QCOMPARE(Schema::readUserVersion(db), 3);
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO settings(key, value) VALUES('legal_name', 'Three Co')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO settings(key, value) VALUES('currency', 'USD')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO accounts(id, name, type, created_at) VALUES(1, 'Rent', 14, 'now')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO documents(id, kind, state, number, date, total, created_at,"
                                                 " updated_at) VALUES(7, 8, 1, 'B-7', '2026-01-05', 1500, 'now', 'now')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO document_lines(id, doc_id, line_no, account_id, description,"
                                                 " quantity, amount) VALUES(70, 7, 1, 1, 'Rent', '1', 1500)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v3"));
        auto file = CompanyFile::open(path, &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->schemaVersion(), Schema::kCurrentVersion);
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".pre-v%1.bak").arg(Schema::kCurrentVersion)));
        const std::optional<BusinessDocument> bill = Documents(*file).load(7);
        QVERIFY(bill);
        QCOMPARE(bill->number, QStringLiteral("B-7"));
        QCOMPARE(bill->total.minor(), 1500);
        QVERIFY(!bill->toPrint);
        QVERIFY(bill->shipTo.isEmpty());
        QCOMPARE(bill->lines.size(), 1);
        QCOMPARE(bill->lines.first().id, 70);
        QCOMPARE(bill->lines.first().poLineId, 0);
        QVERIFY(!bill->lines.first().closed);
        QCOMPARE(Sql::scalarInt(file->database(), QStringLiteral("SELECT COUNT(*) FROM time_entries")), 0);
        QCOMPARE(Sql::scalarInt(file->database(), QStringLiteral("SELECT COUNT(*) FROM employees")), 0);
        QCOMPARE(file->takeNextNumber(DocKind::PurchaseOrder), QStringLiteral("PO-1001"));
        QCOMPARE(file->settings().nextPurchaseOrderNumber, 1002);
        QVERIFY(file->integrityProblems().isEmpty());
    }

    void backupAndIntegrity()
    {
        TestCompany c;
        JournalEntryInput je{QDate(2026, 1, 1), {}, {},
                             {debit(c.account(QStringLiteral("1000")), 5000), credit(c.account(QStringLiteral("3100")), 5000)}};
        QVERIFY(Ledger(c.f()).postJournalEntry(je).ok);
        const QString copy = c.dir.filePath(QStringLiteral("backup.qbooks"));
        QVERIFY(c.f().backupTo(copy).ok);
        QCOMPARE(c.f().backupTo(copy).code, ErrorCode::Duplicate); // never overwrites
        auto restored = CompanyFile::open(copy);
        QVERIFY(restored);
        QCOMPARE(ChartOfAccounts(*restored).balance(ChartOfAccounts(*restored).find(QStringLiteral("1000"))).minor(), 5000);
        QVERIFY(restored->integrityProblems().isEmpty());
    }

    void settingsAndCounters()
    {
        TestCompany c;
        QSignalSpy changed(c.file.get(), &CompanyFile::changed);
        CompanySettings s = c.f().settings();
        s.invoicePrefix = QStringLiteral("SL-");
        s.nextInvoiceNumber = 42;
        s.accentColor = QStringLiteral("#AA3300");
        s.logo = QByteArray("\x89PNG fake", 9);
        QVERIFY(c.f().setSettings(s).ok);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(c.f().peekNextNumber(DocKind::Invoice), QStringLiteral("SL-42"));
        QCOMPARE(c.f().takeNextNumber(DocKind::Invoice), QStringLiteral("SL-42"));
        QCOMPARE(c.f().takeNextNumber(DocKind::Invoice), QStringLiteral("SL-43"));
        QCOMPARE(c.f().takeNextNumber(DocKind::Estimate), QStringLiteral("EST-1001"));
        QCOMPARE(c.f().settings().logo.size(), 9);
        s.legalName.clear();
        QCOMPARE(c.f().setSettings(s).code, ErrorCode::Invalid);
        // The currency is fixed once anything has posted.
        JournalEntryInput je{QDate(2026, 1, 1), {}, {},
                             {debit(c.account(QStringLiteral("1000")), 100), credit(c.account(QStringLiteral("3100")), 100)}};
        QVERIFY(Ledger(c.f()).postJournalEntry(je).ok);
        s = c.f().settings();
        s.currency = QStringLiteral("EUR");
        QCOMPARE(c.f().setSettings(s).code, ErrorCode::CurrencyMismatch);
    }

    void templatesLoadAndBalance()
    {
        const QList<ChartTemplateInfo> templates = chartTemplates();
        QCOMPARE(templates.size(), 4);
        for (const ChartTemplateInfo &info : templates) {
            QVERIFY(!info.title.isEmpty() && !info.description.isEmpty());
            TestCompany c(info.id);
            ChartOfAccounts chart(c.f());
            // Every system role is taken by a template account, not a fallback.
            for (const SystemRole::RoleSpec &spec : SystemRole::all()) {
                const std::optional<Account> a = chart.account(chart.idForRole(spec.role));
                QVERIFY2(a, qPrintable(info.id + QLatin1Char(' ') + spec.role));
                QVERIFY2(!a->number.isEmpty(), qPrintable(info.id + QStringLiteral(": role without template number ") + spec.role));
                QCOMPARE(a->type, spec.type);
            }
            // Every template account made it in; the empty books balance.
            const QJsonArray rows =
                QJsonDocument::fromJson(chartTemplateJson(info.id)).object().value(QStringLiteral("accounts")).toArray();
            QCOMPARE(chart.accounts(true).size(), rows.size());
            const ReportTable tb = Reports(c.f()).trialBalance(QDate(2026, 12, 31));
            QCOMPARE(tb.money(QStringLiteral("total"), QStringLiteral("debit")),
                     tb.money(QStringLiteral("total"), QStringLiteral("credit")));
            // Opening balances keep them balanced too.
            QVERIFY(chart.setOpeningBalance(c.account(QStringLiteral("1000")), usd(250000), QDate(2025, 12, 31)).ok);
            const ReportTable tb2 = Reports(c.f()).trialBalance(QDate(2026, 12, 31));
            QCOMPARE(tb2.money(QStringLiteral("total"), QStringLiteral("debit")), 250000);
            QCOMPARE(tb2.money(QStringLiteral("total"), QStringLiteral("credit")), 250000);
        }
        TestCompany np(QStringLiteral("nonprofit"));
        const std::optional<Account> fundraising = ChartOfAccounts(np.f()).account(np.account(QStringLiteral("9020")));
        QVERIFY(fundraising);
        QCOMPARE(fundraising->functionalGroup, QStringLiteral("fundraising"));
        QCOMPARE(fundraising->fullName, QStringLiteral("Fundraising:Event Expenses"));
        QCOMPARE(ChartOfAccounts(np.f()).account(np.role(SystemRole::RetainedEarnings))->name,
                 QStringLiteral("Net Assets Without Donor Restrictions"));
    }

    void chartRules()
    {
        TestCompany c;
        ChartOfAccounts chart(c.f());
        Account a;
        a.name = QStringLiteral("Studio Gear");
        a.type = AccountType::Expense;
        a.parentId = c.account(QStringLiteral("6400"));
        const Result created = chart.create(a);
        QVERIFY(created.ok);
        QCOMPARE(chart.account(created.id)->fullName, QStringLiteral("Office Supplies and Software:Studio Gear"));
        QCOMPARE(chart.create(a).code, ErrorCode::Duplicate);
        Account wrong = a;
        wrong.name = QStringLiteral("Bad Child");
        wrong.type = AccountType::Bank; // asset under an expense parent
        QCOMPARE(chart.create(wrong).code, ErrorCode::Invalid);
        Account numbered;
        numbered.name = QStringLiteral("Petty Cash");
        numbered.number = QStringLiteral("1000"); // taken by Checking
        numbered.type = AccountType::Bank;
        QCOMPARE(chart.create(numbered).code, ErrorCode::Duplicate);
        // Delete with postings → refused; deactivate instead (after the
        // balance is moved out).
        JournalEntryInput je{QDate(2026, 1, 2), {}, {}, {debit(created.id, 700), credit(c.account(QStringLiteral("1000")), 700)}};
        QVERIFY(Ledger(c.f()).postJournalEntry(je).ok);
        QCOMPARE(chart.remove(created.id).code, ErrorCode::HasPostings);
        QCOMPARE(chart.setActive(created.id, false).code, ErrorCode::Invalid); // balance 7.00
        JournalEntryInput move{QDate(2026, 1, 3), {}, {}, {debit(c.account(QStringLiteral("6400")), 700), credit(created.id, 700)}};
        QVERIFY(Ledger(c.f()).postJournalEntry(move).ok);
        QVERIFY(chart.setActive(created.id, false).ok);
        QVERIFY(!chart.account(created.id)->active);
        QCOMPARE(chart.setActive(c.role(SystemRole::AccountsReceivable), false).code, ErrorCode::Invalid);
        // Posting to an inactive account is refused.
        JournalEntryInput again{QDate(2026, 1, 4), {}, {}, {debit(created.id, 1), credit(c.account(QStringLiteral("1000")), 1)}};
        QCOMPARE(Ledger(c.f()).postJournalEntry(again).code, ErrorCode::InactiveAccount);
        // An unused account can be deleted.
        Account spare;
        spare.name = QStringLiteral("Spare");
        spare.type = AccountType::Expense;
        const Result s = chart.create(spare);
        QVERIFY(chart.remove(s.id).ok);
        QVERIFY(!chart.account(s.id));
    }
};

QTEST_GUILESS_MAIN(TstBooksCompany)
#include "tst_books_company.moc"
