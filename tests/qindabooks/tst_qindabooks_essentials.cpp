// SPDX-License-Identifier: GPL-3.0-or-later
// M63 (D-071): the bookkeeping the engine always had, now reachable through
// BooksService — list import by either key with every refused row reported,
// the company logo, customer statements (screen, PDF, QindaMail), recurring
// schedules (create, edit with re-base, pause, delete), budgets and
// sales-tax payments.

#include "books_app_test_support.h"

#include "printable_document.h"

#include <QImage>
#include <QSignalSpy>

using namespace BooksAppTest;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QUrl write(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    return writeFile(path, bytes) ? QUrl::fromLocalFile(path) : QUrl();
}

QVariantMap line(qint64 accountId, const char *amount, bool taxable = false)
{
    return {{QStringLiteral("description"), QStringLiteral("Work")},
            {QStringLiteral("accountId"), accountId},
            {QStringLiteral("amount"), QString::fromLatin1(amount)},
            {QStringLiteral("taxable"), taxable}};
}

QVariantMap schedule(qint64 customer, qint64 account, const char *frequency, const char *next, const char *amount)
{
    return {{QStringLiteral("name"), QStringLiteral("Retainer")},
            {QStringLiteral("frequency"), QString::fromLatin1(frequency)},
            {QStringLiteral("nextDate"), QString::fromLatin1(next)},
            {QStringLiteral("document"), QVariantMap{{QStringLiteral("partyId"), customer},
                                                     {QStringLiteral("lines"), QVariantList{line(account, amount)}}}}};
}

} // namespace

class QindaBooksEssentialsTest final : public QObject {
    Q_OBJECT

private slots:
    // The window sent "customer" and the service accepted only "customers":
    // every import failed and nobody was told. Both keys work now, and the
    // answer names the list, the counts and each refused row.
    void listImportTakesEitherKeyAndReportsEveryRow()
    {
        Company c;
        const QUrl customers = write(c.dir, str("customers.csv"),
                                     "Name,Email,Phone\nHarbor Coffee,ap@harbor.test,555-0100\n"
                                     "Riverbend Books,hello@riverbend.test,\n");
        const QVariantMap first = c.service.importListCsv(str("customer"), customers);
        QVERIFY2(ok(first), qPrintable(first.value(str("error")).toString()));
        QCOMPARE(first.value(str("kind")).toString(), str("customers"));
        QCOMPARE(first.value(str("added")).toInt(), 2);
        QCOMPARE(c.service.parties(str("customer")).size(), 2);
        const QVariantMap again = c.service.importListCsv(str("customers"), customers);
        QVERIFY(ok(again));
        QCOMPARE(again.value(str("added")).toInt(), 0);
        QCOMPARE(again.value(str("skipped")).toInt(), 2);

        // Vendors through the singular key, items through a synonym.
        const QVariantMap vendors =
            c.service.importListCsv(str("vendor"), write(c.dir, str("v.csv"), "Vendor,1099\nPioneer Print,yes\n"));
        QCOMPARE(vendors.value(str("added")).toInt(), 1);
        QVERIFY(find(c.service.parties(str("vendor")), str("name"), str("Pioneer Print")).value(str("is1099")).toBool());

        // A chart with one good row and two bad ones: the good row lands and
        // both refusals come back with their row numbers.
        const QVariantMap accounts = c.service.importListCsv(
            str("accounts"), write(c.dir, str("a.csv"),
                                   "Name,Type,Number\nStudio Rent,Expenses,6901\nMystery,Planets,6902\n"
                                   "Nowhere:Child,Expense,6903\n"));
        QVERIFY(ok(accounts));
        QCOMPARE(accounts.value(str("added")).toInt(), 1);
        const QStringList errors = accounts.value(str("errors")).toStringList();
        QCOMPARE(errors.size(), 2);
        QVERIFY2(errors.at(0).startsWith(str("row 3:")), qPrintable(errors.at(0)));
        QVERIFY2(errors.at(1).startsWith(str("row 4:")), qPrintable(errors.at(1)));

        // Nothing importable: not ok, with the reason.
        const QVariantMap empty = c.service.importListCsv(str("items"), write(c.dir, str("e.csv"), "Name\n"));
        QVERIFY(!ok(empty));
        QVERIFY(!empty.value(str("error")).toString().isEmpty());
        const QVariantMap missing =
            c.service.importListCsv(str("items"), QUrl::fromLocalFile(c.dir.filePath(str("absent.csv"))));
        QVERIFY(!ok(missing));
        QVERIFY(missing.value(str("error")).toString().contains(str("absent.csv")));
        QVERIFY(!ok(c.service.importListCsv(str("planets"), customers)));

        // Export takes the same keys.
        const QString out = c.dir.filePath(str("out.csv"));
        const QVariantMap exported = c.service.exportListCsv(str("vendor"), QUrl::fromLocalFile(out));
        QVERIFY(ok(exported));
        QCOMPARE(exported.value(str("kind")).toString(), str("vendors"));
        QFile file(out);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Pioneer Print"));
    }

    // File ▸ Company Logo: what prints is what is stored; a file that is not
    // a picture is refused; removal clears it.
    void logoIsStoredPreviewedPrintedAndRemoved()
    {
        Company c;
        QImage picture(48, 24, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        const QString png = c.dir.filePath(str("logo.png"));
        QVERIFY(picture.save(png));
        QVERIFY(c.service.companyLogo().isEmpty());
        QVERIFY(ok(c.service.setCompanyLogo(QUrl::fromLocalFile(png))));
        QVERIFY(c.service.settings().value(str("hasLogo")).toBool());
        QVERIFY(c.service.companyLogo().startsWith(str("data:image/png;base64,")));
        const qint64 acme = c.customer(str("Acme"));
        const qint64 inv = c.invoice(acme, str("100.00"));
        QindaOffice::Books::PrintableDocument printable;
        QVERIFY(QindaOffice::Books::buildPrintableDocument(*c.service.file(), inv, &printable));
        QVERIFY(!QImage::fromData(printable.logo).isNull());
        const QVariantMap refused = c.service.setCompanyLogo(write(c.dir, str("notes.txt"), "not a picture"));
        QVERIFY(!ok(refused));
        QVERIFY(refused.value(str("error")).toString().contains(str("notes.txt")));
        QVERIFY(c.service.settings().value(str("hasLogo")).toBool()); // unchanged by the refusal
        QVERIFY(ok(c.service.removeCompanyLogo()));
        QVERIFY(!c.service.settings().value(str("hasLogo")).toBool());
        QVERIFY(c.service.companyLogo().isEmpty());
    }

    // Sales ▸ Customer Statements: the numbers, the PDF, and the QindaMail
    // hand-off with the PDF attached.
    void statementsShowPrintAndMail()
    {
        Company c;
        const qint64 acme = c.customer(str("Acme Corp"), str("ap@acme.test"));
        c.invoice(acme, str("1000.00"), str("2026-01-10"));
        c.invoice(acme, str("250.00"), str("2026-03-05"));
        const qint64 checking = c.account(str("1000"));
        QVERIFY(ok(c.service.receivePayment({{str("customerId"), acme}, {str("date"), str("2026-02-01")},
                                             {str("amount"), str("400.00")}, {str("depositAccountId"), checking}})));
        const QVariantMap s = c.service.statement(acme, str("2026-02-01"), str("2026-03-31"));
        QVERIFY2(ok(s), qPrintable(s.value(str("error")).toString()));
        QCOMPARE(s.value(str("opening")).toLongLong(), 100000);  // the January invoice
        QCOMPARE(s.value(str("closing")).toLongLong(), 85000);   // 1000 − 400 + 250
        QCOMPARE(s.value(str("lines")).toList().size(), 2);
        QCOMPARE(s.value(str("lines")).toList().first().toMap().value(str("amount")).toLongLong(), -40000);
        QCOMPARE(s.value(str("aging")).toList().size(), 5);
        // An open period defaults to the fiscal year to date.
        QCOMPARE(c.service.statement(acme, {}, {}).value(str("from")).toString(), str("2026-01-01"));
        QVERIFY(!ok(c.service.statement(0, {}, {})));
        QVERIFY(!ok(c.service.statement(acme, str("2026-03-31"), str("2026-01-01"))));

        const QString pdf = c.dir.filePath(str("statement.pdf"));
        QVERIFY(ok(c.service.exportStatementPdf(acme, str("2026-02-01"), str("2026-03-31"), QUrl::fromLocalFile(pdf))));
        QVERIFY(QFileInfo(pdf).size() > 1000);

        QString program;
        QStringList arguments;
        c.service.setLauncher([&](const QString &p, const QStringList &a) {
            program = p;
            arguments = a;
            return true;
        });
        const QVariantMap mail = c.service.emailStatement(acme, str("2026-02-01"), str("2026-03-31"));
        QVERIFY2(ok(mail), qPrintable(mail.value(str("error")).toString()));
        QCOMPARE(program, str("qindamail"));
        QCOMPARE(arguments.size(), 3);
        QCOMPARE(arguments.at(0), str("--attach"));
        QVERIFY(QFileInfo(arguments.at(1)).size() > 1000);
        QVERIFY(arguments.at(2).startsWith(str("mailto:ap@acme.test?subject=Statement%20from%20Test%20Books%20LLC")));
        QVERIFY(QUrl::fromPercentEncoding(arguments.at(2).toUtf8()).contains(str("$850.00")));
    }

    // Sales ▸ Recurring Invoices: create, edit without touching the
    // schedule (no re-base), change the schedule (re-based from the next
    // date, never reaching back), pause, resume, delete.
    void recurringSchedulesAreEditedPausedAndDeleted()
    {
        Company c;
        const qint64 acme = c.customer(str("Acme"));
        const qint64 income = c.account(str("4100"));
        const QVariantMap created = c.service.saveRecurring(schedule(acme, income, "monthly", "2026-01-31", "250.00"));
        QVERIFY2(ok(created), qPrintable(created.value(str("error")).toString()));
        const qint64 id = idOf(created);
        QCOMPARE(c.service.generateDueRecurring(), 3); // Jan 31, Feb 28, Mar 31
        QVariantMap t = c.service.recurringTemplate(id);
        QCOMPARE(t.value(str("nextDate")).toString(), str("2026-04-30"));
        QCOMPARE(t.value(str("document")).toMap().value(str("lines")).toList().size(), 1);

        // Name and amount change; the schedule as shown is passed back.
        QVariantMap edit = schedule(acme, income, "monthly", "2026-04-30", "300.00");
        edit.insert(str("id"), id);
        edit.insert(str("name"), str("Retainer 2026"));
        QVERIFY(ok(c.service.saveRecurring(edit)));
        t = c.service.recurringTemplate(id);
        QCOMPARE(t.value(str("name")).toString(), str("Retainer 2026"));
        QCOMPARE(t.value(str("generated")).toInt(), 3);
        QCOMPARE(t.value(str("start")).toString(), str("2026-01-31")); // not re-based
        QCOMPARE(t.value(str("total")).toLongLong(), 30000);

        // A changed schedule may not reach back to an invoiced date.
        QVariantMap back = schedule(acme, income, "weekly", "2026-03-15", "300.00");
        back.insert(str("id"), id);
        const QVariantMap refused = c.service.saveRecurring(back);
        QVERIFY(!ok(refused));
        QVERIFY(refused.value(str("error")).toString().contains(str("2026-03-31")));
        // Weekly from April 6: re-based, nothing made twice.
        QVariantMap weekly = schedule(acme, income, "weekly", "2026-04-06", "300.00");
        weekly.insert(str("id"), id);
        QVERIFY(ok(c.service.saveRecurring(weekly)));
        t = c.service.recurringTemplate(id);
        QCOMPARE(t.value(str("nextDate")).toString(), str("2026-04-06"));
        QCOMPARE(t.value(str("generated")).toInt(), 0);
        c.service.setToday(QDate(2026, 4, 20));
        QCOMPARE(c.service.dueRecurringCount(), 3); // Apr 6, 13, 20

        QVERIFY(ok(c.service.setRecurringActive(id, false)));
        QCOMPARE(c.service.dueRecurringCount(), 0);
        QCOMPARE(c.service.recurringTemplates().first().toMap().value(str("statusText")).toString(), str("Paused"));
        QVERIFY(ok(c.service.setRecurringActive(id, true)));
        QCOMPARE(c.service.generateDueRecurring(), 3);
        QCOMPARE(c.service.documents({{str("kind"), str("invoice")}}).size(), 6);

        QVERIFY(ok(c.service.deleteRecurring(id)));
        QVERIFY(c.service.recurringTemplates().isEmpty());
        QCOMPARE(c.service.documents({{str("kind"), str("invoice")}}).size(), 6); // the invoices stay
        QVERIFY(!find(c.service.auditTrail(str("recurring"), id), str("action"), str("delete")).isEmpty());
        QVERIFY(!ok(c.service.deleteRecurring(id)));
    }

    // Accounting ▸ Budgets: exact splits, one save, and the numbers Budget
    // vs. Actual compares against.
    void budgetsFeedBudgetVsActual()
    {
        Company c;
        const qint64 rent = c.account(str("6500"));
        const QVariantMap year = c.service.budget(c.service.currentFiscalYear());
        QCOMPARE(year.value(str("fiscalYear")).toInt(), 2026);
        QCOMPARE(year.value(str("monthLabels")).toList().size(), 12);
        QVERIFY(!find(year.value(str("rows")).toList(), str("accountId"), rent).isEmpty());
        QVERIFY(find(year.value(str("rows")).toList(), str("accountId"), c.account(str("1000"))).isEmpty());

        const QVariantMap spread = c.service.budgetSpread(str("1000.00"));
        QVERIFY(ok(spread));
        const QVariantList months = spread.value(str("months")).toList();
        QCOMPARE(months.size(), 12);
        QCOMPARE(c.service.budgetTotal(months).value(str("totalText")).toString(), str("1000.00")); // exact
        QVERIFY(!ok(c.service.budgetSpread(str("12.345"))));

        const QVariantMap saved = c.service.saveBudget(
            {{str("fiscalYear"), 2026}, {str("rows"), QVariantList{QVariantMap{{str("accountId"), rent}, {str("months"), months}}}}});
        QVERIFY2(ok(saved), qPrintable(saved.value(str("error")).toString()));
        const QVariantMap reread = find(c.service.budget(2026).value(str("rows")).toList(), str("accountId"), rent);
        QCOMPARE(reread.value(str("annual")).toLongLong(), 100000);
        c.bill(c.vendor(str("Landlord")), str("400.00"), str("6500"), str("2026-02-01"));
        const auto report = c.service.buildReport(str("budget_vs_actual"), {{str("from"), str("2026-01-01")},
                                                                           {str("to"), str("2026-12-31")}});
        QVERIFY(report.has_value());
        QCOMPARE(report->money(str("account:%1").arg(rent), str("budget")), 100000);
        QCOMPARE(report->money(str("account:%1").arg(rent), str("actual")), 40000);

        // One bad amount saves nothing.
        QVariantList bad = months;
        bad[3] = str("lots");
        const QVariantMap refused = c.service.saveBudget(
            {{str("fiscalYear"), 2026}, {str("rows"), QVariantList{QVariantMap{{str("accountId"), rent}, {str("months"), bad}}}}});
        QVERIFY(!ok(refused));
        QVERIFY(refused.value(str("error")).toString().contains(str("month 4")));
        QCOMPARE(find(c.service.budget(2026).value(str("rows")).toList(), str("accountId"), rent).value(str("annual")).toLongLong(),
                 100000);
    }

    // Accounting ▸ Pay Sales Tax: what is owed per agency, and paying it.
    void salesTaxIsOwedAndPaid()
    {
        Company c;
        QVERIFY(ok(c.service.saveTaxRate({{str("name"), str("State")}, {str("percent"), str("10")},
                                          {str("agencyName"), str("State Revenue")}})));
        const qint64 rate = c.service.taxRates().first().toMap().value(str("id")).toLongLong();
        const qint64 acme = c.customer(str("Acme"));
        QVERIFY(ok(c.service.saveDocument({{str("kind"), str("invoice")}, {str("partyId"), acme},
                                           {str("date"), str("2026-03-02")}, {str("taxRateId"), rate},
                                           {str("lines"), QVariantList{line(c.account(str("4000")), "100.00", true)}}})));
        QVariantList owed = c.service.salesTaxOwed(str("2026-03-31"));
        QCOMPARE(owed.size(), 1);
        const QVariantMap agency = owed.first().toMap();
        QCOMPARE(agency.value(str("owed")).toLongLong(), 1000);
        QCOMPARE(agency.value(str("owedPlain")).toString(), str("10.00"));
        QCOMPARE(c.service.salesTaxOwed(str("2026-02-28")).first().toMap().value(str("owed")).toLongLong(), 0);

        QVERIFY(!ok(c.service.recordSalesTaxPayment({{str("agencyId"), agency.value(str("agencyId"))},
                                                     {str("bankAccountId"), c.account(str("6500"))},
                                                     {str("amount"), str("10.00")}})));
        const QVariantMap paid = c.service.recordSalesTaxPayment(
            {{str("agencyId"), agency.value(str("agencyId"))}, {str("bankAccountId"), c.account(str("1000"))},
             {str("amount"), str("10.00")}, {str("date"), str("2026-03-31")}, {str("memo"), str("Q1 return")}});
        QVERIFY2(ok(paid), qPrintable(paid.value(str("error")).toString()));
        owed = c.service.salesTaxOwed(str("2026-03-31"));
        QCOMPARE(owed.first().toMap().value(str("owed")).toLongLong(), 0);
        QCOMPARE(c.service.document(idOf(paid)).value(str("kind")).toString(), str("salesTaxPayment"));
    }

    // File ▸ Import from QuickBooks: the preview changes nothing; the import
    // makes a backup first, posts as one batch, and a second run adds nothing.
    void quickBooksIifPreviewThenImport()
    {
        Company c;
        QByteArray iif = "!ACCNT\tNAME\tACCNTTYPE\tACCNUM\r\nACCNT\tDesign Income\tINC\t4010\r\n"
                         "!CUST\tNAME\tEMAIL\r\nCUST\tBluebird Bakery\tap@bluebird.test\r\n"
                         "!TRNS\tTRNSTYPE\tDATE\tACCNT\tNAME\tAMOUNT\tDOCNUM\r\n"
                         "!SPL\tTRNSTYPE\tDATE\tACCNT\tNAME\tAMOUNT\tDOCNUM\r\n!ENDTRNS\r\n"
                         "TRNS\tINVOICE\t03/02/2026\tAccounts Receivable\tBluebird Bakery\t500.00\t77\r\n"
                         "SPL\tINVOICE\t03/02/2026\tDesign Income\tBluebird Bakery\t-500.00\t77\r\nENDTRNS\r\n"
                         "TRNS\tGENERAL JOURNAL\t03/03/2026\tPetty Cash\t\t20.00\t\r\n"
                         "SPL\tGENERAL JOURNAL\t03/03/2026\tChecking\t\t-20.00\t\r\nENDTRNS\r\n";
        const QUrl url = write(c.dir, str("studio.iif"), iif);
        const QVariantMap preview = c.service.previewIif(url, {});
        QVERIFY2(ok(preview), qPrintable(preview.value(str("error")).toString()));
        QVERIFY(!preview.value(str("committed")).toBool());
        QCOMPARE(preview.value(str("posted")).toInt(), 1);
        QCOMPARE(preview.value(str("refused")).toList().size(), 1);
        QCOMPARE(preview.value(str("unmapped")).toList().first().toMap().value(str("name")).toString(), str("Petty Cash"));
        QCOMPARE(find(preview.value(str("lists")).toList(), str("key"), str("customers")).value(str("added")).toInt(), 1);
        QVERIFY(c.service.parties(str("customer")).isEmpty()); // nothing changed
        // Strict import with a refusal: refused, nothing in.
        QVERIFY(!ok(c.service.importIif(url, {})));
        QVERIFY(c.service.parties(str("customer")).isEmpty());
        const QVariantMap mapped{{str("accountMap"), QVariantMap{{str("Petty Cash"), c.account(str("1010"))}}}};
        QSignalSpy notified(&c.service, &BooksService::notify);
        const QVariantMap imported = c.service.importIif(url, mapped);
        QVERIFY2(ok(imported), qPrintable(imported.value(str("error")).toString()));
        QVERIFY(imported.value(str("committed")).toBool());
        QCOMPARE(imported.value(str("posted")).toInt(), 2);
        QVERIFY(QFileInfo::exists(imported.value(str("backupPath")).toString()));
        QCOMPARE(imported.value(str("postedText")).toString(), str("$520.00"));
        QCOMPARE(notified.count(), 1);
        QCOMPARE(c.service.documents({{str("kind"), str("invoice")}}).size(), 1);
        QCOMPARE(c.service.importIif(url, mapped).value(str("posted")).toInt(), 0);
    }

    // Reports ▸ Export Report as Excel: every report, through the service.
    void reportsExportToExcel()
    {
        Company c;
        c.invoice(c.customer(str("Acme")), str("1000.00"));
        for (const QVariant &v : c.service.reportCatalog()) {
            const QString id = v.toMap().value(str("id")).toString();
            if (id == str("reconciliation")) {
                continue; // needs a finished reconciliation
            }
            const QString path = c.dir.filePath(id + str(".xlsx"));
            const QVariantMap r = c.service.exportReport(id, {}, QUrl::fromLocalFile(path), str("xlsx"));
            QVERIFY2(ok(r), qPrintable(id + str(": ") + r.value(str("error")).toString()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY2(file.read(2) == "PK", qPrintable(id)); // a zip package
        }
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksEssentialsTest)

#include "tst_qindabooks_essentials.moc"
