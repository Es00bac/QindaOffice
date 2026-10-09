// SPDX-License-Identifier: GPL-3.0-or-later
// Payroll in the ledger (M71, D-081): paychecks post balanced and exactly
// what books_payroll computed, the year to date carries forward, a void
// and an adjustment restore it, paycheck rows never change, paychecks go
// through the check print queue, liabilities fall due by period and clear
// when paid, identity numbers stay out of the audit trail, and a version-4
// file migrates to version 5.
//
// The fixture (books_payroll_fixture.h): Jane Doe, an owner-employee of an
// S corporation, $78,000 a year biweekly → $3,000.00 a check, 2026 W-4
// single, no DR 0004, working in Denver; a small employer (FAMLI employer
// share 0 %), introductory UI rate, monthly depositor. One check
// (tst_payroll_paycheck's ordinary check, worked there line by line):
// FIT 320.38, social security 186.00 + 186.00, Medicare 43.50 + 43.50,
// Colorado 123, FAMLI 13.20 (employee only), Denver OPT 5.75 + 4.00, FUTA
// 18.00, UI 91.50 → net 2,308.17, employer taxes 343.00.
#include "books_payroll_fixture.h"

#include "check_queue.h"
#include "documents.h"
#include "payroll_liabilities.h"
#include "payroll_reports.h"
#include "schema_migrations.h"
#include "sql_util.h"

#include <QFileInfo>
#include <QSqlDatabase>

using namespace BooksTest;

class TstBooksPayroll : public QObject {
    Q_OBJECT
private slots:
    void accountsAreCreatedOnce()
    {
        PayrollCompany c;
        const PayrollSetup s = c.setup();
        for (const PayrollAccountSpec &spec : payrollAccountRoles()) {
            QVERIFY2(ChartOfAccounts(c.f()).account(s.account(spec.role)), qPrintable(spec.role));
        }
        // The template's own accounts are reused, not duplicated.
        QCOMPARE(ChartOfAccounts(c.f()).find(QStringLiteral("Owner Draws")), s.account(PayrollRole::OwnerDraws));
        const qint64 before = ChartOfAccounts(c.f()).accounts(true).size();
        QVERIFY(PayrollRecords(c.f()).ensurePayrollAccounts().ok);
        QCOMPARE(ChartOfAccounts(c.f()).accounts(true).size(), before);
        QCOMPARE(maskedEin(s.ein), QStringLiteral("••-•••0000"));
    }

    void paycheckPostsWhatWasComputed()
    {
        PayrollCompany c;
        const qint64 id = c.pay(QDate(2026, 10, 9));
        QVERIFY(id != 0);
        const std::optional<PaycheckRecord> r = PayrollLedger(c.f()).paycheck(id);
        QVERIFY(r);
        QCOMPARE(r->employeeName, QStringLiteral("Jane Doe"));
        QCOMPARE(r->taxes.gross, usd(300000));
        QCOMPARE(r->taxes.federalIncomeTax, usd(32038));
        QCOMPARE(r->taxes.coloradoIncomeTax, usd(12300));
        QCOMPARE(r->taxes.famliEmployee, usd(1320));
        QCOMPARE(r->taxes.famliEmployer, usd(0));
        QCOMPARE(r->taxes.netPay, usd(230817));
        QCOMPARE(r->taxes.employerTaxes, usd(34300));
        // The ledger: officer wages and payroll taxes in, every liability
        // and the bank out.
        QCOMPARE(c.roleBalance(PayrollRole::OfficerWages), 300000);
        QCOMPARE(c.roleBalance(PayrollRole::EmployerTaxes), 34300);
        QCOMPARE(c.roleBalance(PayrollRole::FederalWithholding), 32038);
        QCOMPARE(c.roleBalance(PayrollRole::SocialSecurity), 37200);
        QCOMPARE(c.roleBalance(PayrollRole::Medicare), 8700);
        QCOMPARE(c.roleBalance(PayrollRole::Futa), 1800);
        QCOMPARE(c.roleBalance(PayrollRole::ColoradoWithholding), 12300);
        QCOMPARE(c.roleBalance(PayrollRole::ColoradoUi), 9150);
        QCOMPARE(c.roleBalance(PayrollRole::Famli), 1320);
        QCOMPARE(c.roleBalance(PayrollRole::LocalOpt), 975);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.bank).minor(), -230817);
        QVERIFY(c.f().integrityProblems().isEmpty());
        const std::optional<BusinessDocument> doc = Documents(c.f()).load(id);
        QCOMPARE(doc->kind, DocKind::Paycheck);
        QCOMPARE(doc->total, usd(230817));
    }

    // The second check sees the first: Denver's OPT is charged once in
    // October; FUTA stops at $7,000 on the third check (1,000 × 0.6 % =
    // 6.00) and the November check owes the OPT again.
    void yearToDateCarriesForward()
    {
        PayrollCompany c;
        c.pay(QDate(2026, 10, 9));
        const qint64 second = c.pay(QDate(2026, 10, 23));
        const qint64 third = c.pay(QDate(2026, 11, 6));
        PayrollLedger ledger(c.f());
        QCOMPARE(ledger.paycheck(second)->taxes.optEmployee, usd(0));
        QCOMPARE(ledger.paycheck(third)->taxes.optEmployee, usd(575));
        QCOMPARE(ledger.paycheck(third)->taxes.futaWages, usd(100000));
        QCOMPARE(ledger.paycheck(third)->taxes.futa, usd(600));
        const PayrollYearToDate y = ledger.yearToDate(c.jane, QDate(2026, 12, 31));
        QCOMPARE(y.gross, usd(900000));
        QCOMPARE(y.ytd.futaWages, usd(700000));
        QCOMPARE(c.roleBalance(PayrollRole::Futa), 4200); // 0.6 % of 7,000
    }

    void voidAndAdjustRestoreTheYear()
    {
        PayrollCompany c;
        const qint64 first = c.pay(QDate(2026, 10, 9));
        const qint64 second = c.pay(QDate(2026, 10, 23));
        PayrollLedger ledger(c.f());
        QVERIFY(ledger.voidPaycheck(second, QStringLiteral("Paid twice")).ok);
        QCOMPARE(ledger.yearToDate(c.jane, QDate(2026, 12, 31)).gross, usd(300000));
        QCOMPARE(c.roleBalance(PayrollRole::FederalWithholding), 32038);
        // Adjust the first to $2,800: voided and rewritten in one step.
        PaycheckRequest corrected = c.request(QDate(2026, 10, 9));
        corrected.regular = usd(280000);
        const Result adjusted = ledger.replacePaycheck(first, corrected, QStringLiteral("Unpaid leave"));
        QVERIFY2(adjusted.ok, qPrintable(adjusted.error));
        QVERIFY(ledger.paycheck(first)->voided);
        QCOMPARE(ledger.paycheck(adjusted.id)->replacesDocId, first);
        QCOMPARE(ledger.yearToDate(c.jane, QDate(2026, 12, 31)).gross, usd(280000));
        QCOMPARE(ledger.paychecks({}, {}, 0, true).size(), 3);
        QCOMPARE(ledger.paychecks().size(), 1);
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    void paycheckRowsNeverChange()
    {
        PayrollCompany c;
        c.pay(QDate(2026, 10, 9));
        QVERIFY(!Sql::exec(c.f().database(), QStringLiteral("UPDATE paychecks SET net = 1")));
        QVERIFY(!Sql::exec(c.f().database(), QStringLiteral("DELETE FROM paychecks")));
        QCOMPARE(PayrollLedger(c.f()).paychecks().first().taxes.netPay, usd(230817));
    }

    // A paycheck waits in M68's check queue and is numbered when it prints;
    // the renumbering keeps the document, so its payroll row stays live.
    void paychecksPrintThroughTheQueue()
    {
        PayrollCompany c;
        PaycheckRequest r = c.request(QDate(2026, 10, 9));
        r.toPrint = true;
        r.checkNumber = QStringLiteral("555"); // ignored while it waits
        const qint64 id = PayrollLedger(c.f()).createPaycheck(r).id;
        CheckQueue queue(c.f());
        QCOMPARE(queue.queue(c.bank).size(), 1);
        QCOMPARE(queue.queue(c.bank).first().kind, DocKind::Paycheck);
        QVERIFY(queue.assignNumbers(c.bank, {id}, 1001).ok);
        const std::optional<PaycheckRecord> printed = PayrollLedger(c.f()).paycheck(id);
        QCOMPARE(printed->number, QStringLiteral("1001"));
        QVERIFY(!printed->voided);
        QCOMPARE(queue.nextNumber(c.bank), 1002);
        QCOMPARE(PayrollLedger(c.f()).yearToDate(c.jane, QDate(2026, 12, 31)).gross, usd(300000));
    }

    // Monthly depositor, an October 9 payday: the 941 taxes are due Monday
    // November 16 (the 15th is a Sunday); Colorado's quarterly items and
    // FUTA (18.00, under $500) with Form 940 on Monday February 1, 2027.
    void liabilitiesFallDueAndClear()
    {
        PayrollCompany c;
        c.pay(QDate(2026, 10, 9));
        PayrollLiabilities liabilities(c.f());
        const QList<LiabilityDue> due = liabilities.liabilities(QDate(2026, 1, 1), QDate(2026, 12, 31));
        const auto find = [&](const QString &tax) {
            for (const LiabilityDue &d : due) {
                if (d.tax == tax) {
                    return d;
                }
            }
            return LiabilityDue{};
        };
        const LiabilityDue federal = find(QStringLiteral("federal941"));
        QCOMPARE(federal.balance, usd(77938)); // 320.38 + 372.00 + 87.00
        QCOMPARE(federal.start, QDate(2026, 10, 1));
        QCOMPARE(federal.due, QDate(2026, 11, 16));
        QVERIFY(federal.notes.join(QLatin1Char(' ')).contains(QStringLiteral("Form 941"))); // under $2,500
        QCOMPARE(find(QStringLiteral("coWithholding")).due, QDate(2027, 2, 1));
        QCOMPARE(find(QStringLiteral("coUi")).balance, usd(9150));
        QCOMPARE(find(QStringLiteral("coFamli")).balance, usd(1320));
        QCOMPARE(find(QStringLiteral("opt:denver")).balance, usd(975));
        const LiabilityDue futa = find(QStringLiteral("futa"));
        QCOMPARE(futa.due, QDate(2027, 2, 1));
        QVERIFY(futa.notes.join(QLatin1Char(' ')).contains(QStringLiteral("Form 940")));

        LiabilityPaymentRequest pay{QStringLiteral("federal941"), federal.start, federal.end, 0, QDate(2026, 11, 10), {},
                                    QStringLiteral("EFTPS 0001"), {}};
        const Result paid = liabilities.pay(pay);
        QVERIFY2(paid.ok, qPrintable(paid.error));
        QCOMPARE(c.roleBalance(PayrollRole::FederalWithholding), 0);
        QCOMPARE(c.roleBalance(PayrollRole::SocialSecurity), 0);
        QVERIFY(!liabilities.liability(QStringLiteral("federal941"), federal.start, federal.end)->balance.isPositive());
        // A cent over: the rounding goes to Payroll Taxes, the liability clears.
        const LiabilityDue ui = find(QStringLiteral("coUi"));
        QVERIFY(liabilities.pay({QStringLiteral("coUi"), ui.start, ui.end, 0, QDate(2027, 1, 20), usd(9151), {}, {}}).ok);
        QCOMPARE(c.roleBalance(PayrollRole::ColoradoUi), 0);
        QCOMPARE(c.roleBalance(PayrollRole::EmployerTaxes), 34301);
        // Part of it; more than owed is refused.
        const LiabilityDue co = find(QStringLiteral("coWithholding"));
        QVERIFY(liabilities.pay({QStringLiteral("coWithholding"), co.start, co.end, 0, QDate(2027, 1, 20), usd(10000), {}, {}}).ok);
        QCOMPARE(c.roleBalance(PayrollRole::ColoradoWithholding), 2300);
        const LiabilityDue famli = find(QStringLiteral("coFamli"));
        QCOMPARE(liabilities.pay({QStringLiteral("coFamli"), famli.start, famli.end, 0, QDate(2027, 1, 20), usd(2000), {}, {}}).code,
                 ErrorCode::OverApplied);
        // Voiding the deposit puts the balance back.
        QVERIFY(liabilities.voidPayment(paid.id, QStringLiteral("Bounced")).ok);
        QCOMPARE(liabilities.liability(QStringLiteral("federal941"), federal.start, federal.end)->balance, usd(77938));
        QCOMPARE(liabilities.payments().size(), 2);
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    // The SSN is stored, masked everywhere else, and never in the audit.
    void identityNumbersStayOutOfTheAudit()
    {
        PayrollCompany c;
        const EmployeePayroll e = PayrollRecords(c.f()).employee(c.jane);
        QVERIFY2(e.ssn == QStringLiteral("900000001"), "the SSN is stored as nine digits"); // never printed
        QCOMPARE(maskedSsn(e.ssn), QStringLiteral("•••-••-0001"));
        for (const AuditEntry &a : Ledger(c.f()).auditTrail()) {
            QVERIFY2(!a.afterJson.contains(QStringLiteral("900000001")) && !a.afterJson.contains(QStringLiteral("900-00")),
                     qPrintable(a.entity));
            QVERIFY(!a.afterJson.contains(QStringLiteral("000000000")));
        }
        EmployeePayroll bad = e;
        bad.ssn = QStringLiteral("12345");
        QCOMPARE(PayrollRecords(c.f()).saveEmployee(bad).code, ErrorCode::Invalid);
    }

    void reportsAddUp()
    {
        PayrollCompany c;
        c.pay(QDate(2026, 10, 9));
        const qint64 second = c.pay(QDate(2026, 10, 23));
        PayrollLedger(c.f()).voidPaycheck(second, QStringLiteral("Mistake"));
        PayrollReports reports(c.f());
        const ReportTable summary = reports.payrollSummary(QDate(2026, 10, 1), QDate(2026, 12, 31));
        QCOMPARE(summary.money(QStringLiteral("total:gross"), QStringLiteral("total")), 300000);
        QCOMPARE(summary.money(QStringLiteral("total:net"), QStringLiteral("total")), 230817);
        const ReportTable history = reports.paycheckHistory(QDate(2026, 10, 1), QDate(2026, 12, 31));
        QCOMPARE(history.rows.size(), 3); // two checks, one void, and the total
        QCOMPARE(history.text(QStringLiteral("doc:%1").arg(second), QStringLiteral("status")), QStringLiteral("Void"));
        QCOMPARE(history.money(QStringLiteral("total:all"), QStringLiteral("net")), 230817);
        const ReportTable earnings = reports.employeeEarnings(QDate(2026, 1, 1), QDate(2026, 12, 31));
        QCOMPARE(earnings.money(QStringLiteral("employee:%1").arg(c.jane), QStringLiteral("fit")), 32038);
    }

    // M71: a version-4 file (M68: employees, time) opens as version 5 with
    // its employee intact and the payroll tables empty.
    void migratesAVersion4File()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v4.qbooks"));
        QString error;
        QVERIFY2(CompanyFile::createBareAtVersion(path, 4, &error), qPrintable(error));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v4"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QCOMPARE(Schema::readUserVersion(db), 4);
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO settings(key, value) VALUES('legal_name', 'Four Co')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO settings(key, value) VALUES('currency', 'USD')")));
            QVERIFY(Sql::exec(db, QStringLiteral("INSERT INTO employees(id, name, active, created_at, updated_at)"
                                                 " VALUES(3, 'Sam Field', 1, 'now', 'now')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v4"));
        auto file = CompanyFile::open(path, &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->schemaVersion(), 5);
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".pre-v5.bak")));
        QCOMPARE(Employees(*file).employees().size(), 1);
        QCOMPARE(Sql::scalarInt(file->database(), QStringLiteral("SELECT COUNT(*) FROM employee_payroll")), 0);
        QCOMPARE(Sql::scalarInt(file->database(), QStringLiteral("SELECT COUNT(*) FROM paychecks")), 0);
        const EmployeePayroll sam = PayrollRecords(*file).employee(3);
        QVERIFY(!sam.exists);
        QCOMPARE(sam.firstName, QStringLiteral("Sam"));
        QVERIFY(file->integrityProblems().isEmpty());
    }

    void refusals()
    {
        PayrollCompany c;
        PayrollLedger ledger(c.f());
        QString error;
        PaycheckRequest next = c.request(QDate(2027, 1, 8));
        QVERIFY(!ledger.createPaycheck(next).ok);
        QVERIFY(!ledger.preview(next, &error).taxes.ok);
        QVERIFY2(error.contains(QStringLiteral("no 2027 payroll tax tables")), qPrintable(error));
        Employee other;
        other.name = QStringLiteral("No Setup");
        const qint64 plain = Employees(c.f()).create(other).id;
        QVERIFY(!ledger.preview(c.request(QDate(2026, 10, 9), plain), &error).taxes.ok);
        QVERIFY(error.contains(QStringLiteral("payroll first")));
        // A sole proprietor or a partner is paid by draw only;
        // "not sure" keeps both ways open.
        PayrollRecords records(c.f());
        for (const EntityTax entity : {EntityTax::ScheduleC, EntityTax::Partnership}) {
            PayrollSetup s = records.setup();
            s.entity = entity;
            QVERIFY(records.saveSetup(s).ok);
            error.clear();
            QVERIFY(!ledger.preview(c.request(QDate(2026, 10, 9)), &error).taxes.ok);
            QVERIFY2(error.contains(QStringLiteral("owner's draw")), qPrintable(error));
        }
        PayrollSetup s = records.setup();
        s.entity = EntityTax::Unsure;
        QVERIFY(records.saveSetup(s).ok);
        error.clear();
        QVERIFY2(ledger.preview(c.request(QDate(2026, 10, 9)), &error).taxes.ok, qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(TstBooksPayroll)
#include "tst_books_payroll.moc"
