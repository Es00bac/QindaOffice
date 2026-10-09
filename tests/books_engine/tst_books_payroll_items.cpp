// SPDX-License-Identifier: GPL-3.0-or-later
// A company's own payroll tax items (payroll_items.h, D-081 amendment),
// for a business outside Colorado with three made-up items: an employer
// unemployment tax (2.7 % up to $10,000), an employee disability tax
// (1 % up to $5,000) and state income tax withholding whose 4 % + $5.00
// the employee carries. Each paycheck computes them, credits their own
// liability accounts, owes them to their agency on their due dates, shows
// the employee's on the stub with the year to date, and everything comes
// back after the company file is closed and opened again. Colorado's taxes
// and accounts appear nowhere. Every expected value is worked by hand.
#include "books_payroll_fixture.h"

#include "payroll_items.h"
#include "payroll_liabilities.h"
#include "payroll_reports.h"
#include "payroll_stub.h"
#include "sql_util.h"

using namespace BooksTest;

namespace {

Decimal pct(const char *text)
{
    return *Decimal::parse(QString::fromLatin1(text));
}

// The payroll company set up outside Colorado (Form 940 state TX), with
// three made-up items of its own.
struct ItemsCompany : PayrollCompany {
    qint64 ui = 0;
    qint64 sdi = 0;
    qint64 sit = 0;

    ItemsCompany()
    {
        PayrollRecords records(f());
        PayrollSetup s = records.setup();
        s.colorado = false;
        s.stateCode = QStringLiteral("TX");
        s.optCity.clear();
        records.saveSetup(s);
        PayrollItems items(f());
        PayrollItem a;
        a.name = QStringLiteral("Example state unemployment");
        a.side = Payroll::CustomItem::Side::Employer;
        a.rate = pct("0.027");
        a.wageBase = usd(1000000);
        a.agency = QStringLiteral("Example workforce agency");
        a.frequency = PayrollItem::Frequency::Quarterly;
        ui = items.saveItem(a).id;
        PayrollItem b;
        b.name = QStringLiteral("Example state disability");
        b.side = Payroll::CustomItem::Side::Employee;
        b.rate = pct("0.01");
        b.wageBase = usd(500000);
        b.agency = QStringLiteral("Example disability fund");
        b.frequency = PayrollItem::Frequency::Monthly;
        b.dueDay = 20;
        sdi = items.saveItem(b).id;
        PayrollItem w;
        w.name = QStringLiteral("Example state income tax");
        w.kind = Payroll::CustomItem::Kind::Withholding;
        w.agency = QStringLiteral("Example revenue department");
        w.frequency = PayrollItem::Frequency::Monthly;
        sit = items.saveItem(w).id;
        EmployeePayroll e = records.employee(jane);
        e.withholding.insert(sit, EmployeePayroll::CustomWithholding{pct("0.04"), usd(500)});
        records.saveEmployee(e);
    }
    qint64 itemAccount(qint64 id) { return PayrollItems(f()).item(id)->accountId; }
};

} // namespace

class TstBooksPayrollItems : public QObject {
    Q_OBJECT
private slots:
    void itemsAreValidated()
    {
        ItemsCompany c;
        PayrollItems items(c.f());
        QCOMPARE(items.items().size(), 3);
        // Each got its own account under Payroll Liabilities, named after it.
        const std::optional<Account> account = ChartOfAccounts(c.f()).account(c.itemAccount(c.sdi));
        QVERIFY(account.has_value());
        QCOMPARE(account->name, QStringLiteral("Example state disability"));
        QCOMPARE(account->type, AccountType::OtherCurrentLiability);
        PayrollItem bad;
        bad.name = QStringLiteral("example STATE disability"); // the same name
        bad.rate = pct("0.01");
        QCOMPARE(items.saveItem(bad).code, ErrorCode::Duplicate);
        bad.name = QStringLiteral("No rate");
        bad.rate = Decimal();
        QCOMPARE(items.saveItem(bad).code, ErrorCode::Invalid);
        bad.rate = pct("1.5"); // 150 %
        QCOMPARE(items.saveItem(bad).code, ErrorCode::Invalid);
        bad.rate = pct("0.01");
        bad.wageBase = usd(0);
        QCOMPARE(items.saveItem(bad).code, ErrorCode::Invalid);
        bad.wageBase.reset();
        bad.accountId = c.bank; // not a liability
        QCOMPARE(items.saveItem(bad).code, ErrorCode::UnknownAccount);
        // A withholding item is always the employee's, with no rate of its own.
        const PayrollItem w = *items.item(c.sit);
        QVERIFY(w.side == Payroll::CustomItem::Side::Employee);
        QVERIFY(w.rate.isZero());
        // Colorado's accounts were never made for this business.
        PayrollSetup s = PayrollRecords(c.f()).setup();
        s.accounts.clear();
        PayrollRecords(c.f()).saveSetup(s);
        QVERIFY(PayrollRecords(c.f()).ensurePayrollAccounts().ok);
        QCOMPARE(PayrollRecords(c.f()).setup().account(PayrollRole::ColoradoWithholding), 0);
        QCOMPARE(PayrollRecords(c.f()).setup().account(PayrollRole::Famli), 0);
    }

    // Two checks of $3,000 (Oct 9 and Oct 23):
    //  disability 1 % up to $5,000: 30.00, then 1 % of the 2,000 left = 20.00;
    //  unemployment 2.7 % up to $10,000: 81.00, 81.00;
    //  withholding 4 % of 3,000 + 5.00 = 125.00 each.
    //  Net, the first check: 3,000 − 320.38 − 186.00 − 43.50 − 30.00 −
    //  125.00 = 2,295.12; no Colorado tax, UI, FAMLI or OPT.
    void paychecksTakeTheItems()
    {
        ItemsCompany c;
        const qint64 first = c.pay(QDate(2026, 10, 9));
        const qint64 second = c.pay(QDate(2026, 10, 23));
        PayrollLedger ledger(c.f());
        const PaycheckRecord one = *ledger.paycheck(first);
        QCOMPARE(one.taxes.custom.size(), 3);
        QCOMPARE(one.taxes.customEmployee, usd(15500));
        QCOMPARE(one.taxes.customEmployer, usd(8100));
        QCOMPARE(one.taxes.coloradoIncomeTax + one.taxes.coloradoUi + one.taxes.famliEmployee + one.taxes.optEmployee,
                 usd(0));
        QCOMPARE(one.taxes.netPay, usd(229512));
        QCOMPARE(one.taxes.employeeTaxes, usd(70488)); // 320.38 + 186 + 43.50 + 155.00
        QCOMPARE(one.taxes.employerTaxes, usd(32850)); // 186 + 43.50 + 18 + 81
        const PaycheckRecord two = *ledger.paycheck(second);
        for (const Payroll::CustomItemResult &r : two.taxes.custom) {
            if (r.id == c.sdi) {
                QCOMPARE(r.wages, usd(200000));
                QCOMPARE(r.amount, usd(2000));
            }
        }
        // The books: each item's own account owes what the checks took (a
        // liability's balance reads in its normal, credit sign).
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.itemAccount(c.sdi)).minor(), 5000);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.itemAccount(c.ui)).minor(), 16200);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.itemAccount(c.sit)).minor(), 25000);
        QVERIFY(c.f().integrityProblems().isEmpty());
        // Voided, the second check leaves the year to date.
        QVERIFY(ledger.voidPaycheck(second, QStringLiteral("Wrong week")).ok);
        QCOMPARE(PayrollItems(c.f()).yearToDate(c.jane, QDate(2026, 12, 31)).value(c.sdi).wages, usd(300000));
    }

    // Monthly disability (due the 20th of next month: Nov 20), quarterly
    // unemployment (last day of the month after Q4: Jan 31, 2027 is a
    // Sunday → Feb 1), monthly withholding (last day: Nov 30); paying one
    // clears its account.
    void liabilitiesOweTheAgencies()
    {
        ItemsCompany c;
        c.pay(QDate(2026, 10, 9));
        PayrollLiabilities liabilities(c.f());
        const auto find = [&](qint64 id) {
            for (const LiabilityDue &d : liabilities.liabilities(QDate(2026, 1, 1), QDate(2026, 12, 31))) {
                if (d.tax == QStringLiteral("custom:%1").arg(id)) {
                    return d;
                }
            }
            return LiabilityDue{};
        };
        const LiabilityDue sdi = find(c.sdi);
        QCOMPARE(sdi.label, QStringLiteral("Example state disability"));
        QCOMPARE(sdi.agency, QStringLiteral("Example disability fund"));
        QCOMPARE(sdi.balance, usd(3000));
        QCOMPARE(sdi.due, QDate(2026, 11, 20));
        QCOMPARE(find(c.ui).due, QDate(2027, 2, 1));
        QCOMPARE(find(c.ui).balance, usd(8100));
        QCOMPARE(find(c.sit).due, QDate(2026, 11, 30));
        for (const LiabilityDue &d : liabilities.liabilities(QDate(2026, 1, 1), QDate(2026, 12, 31))) {
            QVERIFY2(!d.tax.startsWith(QStringLiteral("co")) && !d.tax.startsWith(QStringLiteral("opt:")), qPrintable(d.tax));
        }
        QVERIFY(liabilities.pay({sdi.tax, sdi.start, sdi.end, 0, QDate(2026, 11, 2), {}, QStringLiteral("SDI-1"), {}}).ok);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.itemAccount(c.sdi)).minor(), 0);
        QCOMPARE(liabilities.labelOf(sdi.tax), QStringLiteral("Example state disability"));
        // The payroll reports carry them too.
        const ReportTable summary = PayrollReports(c.f()).payrollSummary(QDate(2026, 10, 1), QDate(2026, 12, 31));
        QCOMPARE(summary.money(QStringLiteral("custom:%1").arg(c.sit), QStringLiteral("total")), 12500);
        QCOMPARE(summary.money(QStringLiteral("custom:%1").arg(c.ui), QStringLiteral("total")), 8100);
        QVERIFY(summary.row(QStringLiteral("coWh")) == nullptr);
    }

    // The stub (paper and screen): the employee's items with the year to
    // date, no Colorado lines; the employer's after them.
    void stubShowsTheItems()
    {
        ItemsCompany c;
        c.pay(QDate(2026, 10, 9));
        const qint64 second = c.pay(QDate(2026, 10, 23));
        QStringList labels;
        Money disabilityYtd;
        for (const StubLine &l : payStubLines(c.f(), *PayrollLedger(c.f()).paycheck(second))) {
            labels << l.label;
            if (l.label == QStringLiteral("Example state disability")) {
                QCOMPARE(l.current, usd(2000));
                disabilityYtd = l.yearToDate;
            }
        }
        QCOMPARE(disabilityYtd, usd(5000));
        QVERIFY(labels.contains(QStringLiteral("Example state income tax")));
        QVERIFY(labels.contains(QStringLiteral("Example state unemployment"))); // the employer's section
        QVERIFY(!labels.contains(QStringLiteral("Colorado income tax")));
        QVERIFY(!labels.contains(QStringLiteral("Colorado FAMLI")));
    }

    // Closed and opened again: the items, the employee's withholding and
    // what each paycheck took are all in the file.
    void roundTripThroughTheFile()
    {
        QString path;
        qint64 sit = 0;
        qint64 jane = 0;
        qint64 doc = 0;
        {
            ItemsCompany c;
            doc = c.pay(QDate(2026, 10, 9));
            sit = c.sit;
            jane = c.jane;
            const QString copy = c.dir.filePath(QStringLiteral("copy.qbooks"));
            QVERIFY(c.f().backupTo(copy).ok);
            path = copy;
            QString error;
            const std::unique_ptr<CompanyFile> reopened = CompanyFile::open(path, &error);
            QVERIFY2(reopened != nullptr, qPrintable(error));
            const QList<PayrollItem> items = PayrollItems(*reopened).items();
            QCOMPARE(items.size(), 3);
            const std::optional<PayrollItem> ui = PayrollItems(*reopened).item(c.ui);
            QCOMPARE(ui->rate.toString(), QStringLiteral("0.027"));
            QCOMPARE(*ui->wageBase, usd(1000000));
            QCOMPARE(ui->agency, QStringLiteral("Example workforce agency"));
            const EmployeePayroll e = PayrollRecords(*reopened).employee(jane);
            QCOMPARE(e.withholding.value(sit).percent.toString(), QStringLiteral("0.04"));
            QCOMPARE(e.withholding.value(sit).extra, usd(500));
            const PaycheckRecord r = *PayrollLedger(*reopened).paycheck(doc);
            QCOMPARE(r.taxes.customEmployee, usd(15500));
            QCOMPARE(r.taxes.netPay, usd(229512));
            QCOMPARE(Sql::scalarInt(reopened->database(), QStringLiteral("SELECT COUNT(*) FROM paycheck_items")), 3);
            QVERIFY(reopened->integrityProblems().isEmpty());
        }
    }

    // What a paycheck took never changes (triggers), and an item's account
    // cannot move once a paycheck used it.
    void paycheckItemsNeverChange()
    {
        ItemsCompany c;
        c.pay(QDate(2026, 10, 9));
        QString error;
        QVERIFY(!Sql::exec(c.f().database(), QStringLiteral("UPDATE paycheck_items SET amount = 1"), {}, &error));
        QVERIFY(!Sql::exec(c.f().database(), QStringLiteral("DELETE FROM paycheck_items"), {}, &error));
        PayrollItem moved = *PayrollItems(c.f()).item(c.sdi);
        Account other;
        other.name = QStringLiteral("Another liability");
        other.type = AccountType::OtherCurrentLiability;
        moved.accountId = ChartOfAccounts(c.f()).create(other).id;
        QCOMPARE(PayrollItems(c.f()).saveItem(moved).code, ErrorCode::Invalid);
        // Inactive, it stops applying to new paychecks.
        QVERIFY(PayrollItems(c.f()).setActive(c.sdi, false).ok);
        const qint64 next = c.pay(QDate(2026, 10, 23));
        const PaycheckRecord later = *PayrollLedger(c.f()).paycheck(next);
        QCOMPARE(later.taxes.custom.size(), 2);
        for (const Payroll::CustomItemResult &r : later.taxes.custom) {
            QVERIFY(r.id != c.sdi);
        }
    }
};

QTEST_GUILESS_MAIN(TstBooksPayrollItems)
#include "tst_books_payroll_items.moc"
