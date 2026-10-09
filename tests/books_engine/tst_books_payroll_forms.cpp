// SPDX-License-Identifier: GPL-3.0-or-later
// Payroll form worksheets (M71; payroll_reports.h): Form 941 and Schedule
// B, Form 940, W-2 and W-3, DR 1094, DR 1093 and the UI and FAMLI wage
// reports, from paychecks whose every amount is known. Identity numbers
// show only when asked for.
//
// The year (books_payroll_fixture.h): Jane, $3,000.00 biweekly, paid
// October 9, October 23 and December 18, 2026 — each check FIT 320.38,
// social security 186.00 each side, Medicare 43.50 each side, Colorado
// $123, FAMLI 13.20 (employee only).
#include "books_payroll_fixture.h"

#include "payroll_liabilities.h"
#include "payroll_reports.h"

using namespace BooksTest;

namespace {

qint64 amount(const ReportTable &t, const QString &line)
{
    return t.money(QStringLiteral("line:") + line, QStringLiteral("amount"));
}

QString value(const ReportTable &t, const QString &line)
{
    return t.text(QStringLiteral("line:") + line, QStringLiteral("amount"));
}

struct Year : PayrollCompany {
    Year()
    {
        pay(QDate(2026, 10, 9));
        pay(QDate(2026, 10, 23));
        pay(QDate(2026, 12, 18)); // its period Dec 5–18 holds December 12
    }
};

} // namespace

class TstBooksPayrollForms : public QObject {
    Q_OBJECT
private slots:
    // Line 2 = 9,000; line 3 = 3 × 320.38 = 961.14; 5a = 9,000 × 0.124 =
    // 1,116.00; 5c = 9,000 × 0.029 = 261.00; 6 = 2,338.14; line 7 = the
    // employees' 688.50 withheld less 6.2 % and 1.45 % of 9,000 (558.00 +
    // 130.50) = 0; line 12 = 2,338.14 — under $2,500, so box 1; the months
    // 1,558.76 / 0 / 779.38 add up to line 12.
    void form941()
    {
        Year c;
        const ReportTable f = PayrollReports(c.f()).form941(2026, 4);
        QCOMPARE(value(f, QStringLiteral("1")), QStringLiteral("1"));
        QCOMPARE(amount(f, QStringLiteral("2")), 900000);
        QCOMPARE(amount(f, QStringLiteral("3")), 96114);
        QCOMPARE(c.f().currency(), QStringLiteral("USD"));
        QCOMPARE(f.money(QStringLiteral("line:5a"), QStringLiteral("column1")), 900000);
        QCOMPARE(amount(f, QStringLiteral("5a")), 111600);
        QCOMPARE(amount(f, QStringLiteral("5c")), 26100);
        QCOMPARE(amount(f, QStringLiteral("5d")), 0);
        QCOMPARE(amount(f, QStringLiteral("6")), 233814);
        QCOMPARE(amount(f, QStringLiteral("7")), 0);
        QCOMPARE(amount(f, QStringLiteral("12")), 233814);
        QCOMPARE(amount(f, QStringLiteral("14")), 233814);
        QVERIFY(value(f, QStringLiteral("16")).contains(QStringLiteral("☒")));
        QCOMPARE(amount(f, QStringLiteral("16 M1")), 155876);
        QCOMPARE(amount(f, QStringLiteral("16 M3")), 77938);
        QCOMPARE(amount(f, QStringLiteral("16 T")), 233814);
        QVERIFY(value(f, QStringLiteral("EIN")).endsWith(QStringLiteral("0000")));
        QVERIFY(!value(f, QStringLiteral("EIN")).contains(QStringLiteral("00-0000000")));
        // Deposits show on line 13 and lower the balance.
        PayrollLiabilities liabilities(c.f());
        QVERIFY(liabilities.pay({QStringLiteral("federal941"), QDate(2026, 10, 1), QDate(2026, 10, 31), 0,
                                 QDate(2026, 11, 13), {}, {}, {}}).ok);
        const ReportTable after = PayrollReports(c.f()).form941(2026, 4);
        QCOMPARE(amount(after, QStringLiteral("13")), 155876);
        QCOMPARE(amount(after, QStringLiteral("14")), 77938);
    }

    // Two checks of 1,234.57: each withholds 76.54 social security and
    // 17.90 Medicare. 5a = 0.124 × 2,469.14 = 306.17; 5c = 0.029 × 2,469.14
    // = 71.61; the employees' share of those is 153.09 + 35.80 against
    // 153.08 + 35.80 withheld → line 7 = −0.01, and line 12 equals what the
    // paychecks put on the books.
    void form941FractionsOfCents()
    {
        PayrollCompany c;
        for (const QDate &d : {QDate(2026, 10, 9), QDate(2026, 10, 23)}) {
            PaycheckRequest r = c.request(d);
            r.regular = usd(123457);
            QVERIFY(PayrollLedger(c.f()).createPaycheck(r).ok);
        }
        const ReportTable f = PayrollReports(c.f()).form941(2026, 4);
        QCOMPARE(amount(f, QStringLiteral("5a")), 30617);
        QCOMPARE(amount(f, QStringLiteral("5c")), 7161);
        QCOMPARE(amount(f, QStringLiteral("7")), -1);
        const qint64 books = c.roleBalance(PayrollRole::FederalWithholding) + c.roleBalance(PayrollRole::SocialSecurity)
            + c.roleBalance(PayrollRole::Medicare);
        QCOMPARE(amount(f, QStringLiteral("12")), books);
        QCOMPARE(amount(f, QStringLiteral("16 T")), books);
    }

    void scheduleB()
    {
        Year c;
        const ReportTable b = PayrollReports(c.f()).schedule941B(2026, 4);
        QCOMPARE(b.money(QStringLiteral("day:2026-10-09"), QStringLiteral("liability")), 77938);
        QCOMPARE(b.money(QStringLiteral("month:1"), QStringLiteral("liability")), 155876);
        QCOMPARE(b.money(QStringLiteral("total"), QStringLiteral("liability")), 233814);
    }

    // Line 3 = 9,000; line 5 = 9,000 − 7,000 = 2,000; line 7 = 7,000;
    // line 8 = 7,000 × 0.006 = 42.00 = line 12; not over $500: no Part 5.
    void form940()
    {
        Year c;
        const ReportTable f = PayrollReports(c.f()).form940(2026);
        QCOMPARE(value(f, QStringLiteral("1a")), QStringLiteral("CO"));
        QCOMPARE(amount(f, QStringLiteral("3")), 900000);
        QCOMPARE(amount(f, QStringLiteral("4")), 0);
        QCOMPARE(amount(f, QStringLiteral("5")), 200000);
        QCOMPARE(amount(f, QStringLiteral("7")), 700000);
        QCOMPARE(amount(f, QStringLiteral("8")), 4200);
        QCOMPARE(amount(f, QStringLiteral("12")), 4200);
        QCOMPARE(amount(f, QStringLiteral("14")), 4200);
        QCOMPARE(c.roleBalance(PayrollRole::Futa), 4200); // the books agree
        QVERIFY(f.row(QStringLiteral("line:16a")) == nullptr);
    }

    // Box 1 = 9,000; 2 = 961.14; 3 = 9,000; 4 = 558.00; 5 = 9,000; 6 =
    // 130.50; 16 = 9,000; 17 = 369.
    void w2AndW3()
    {
        Year c;
        PayrollReports reports(c.f());
        const ReportTable w2 = reports.formW2(2026, c.jane);
        QCOMPARE(amount(w2, QStringLiteral("1")), 900000);
        QCOMPARE(amount(w2, QStringLiteral("2")), 96114);
        QCOMPARE(amount(w2, QStringLiteral("4")), 55800);
        QCOMPARE(amount(w2, QStringLiteral("6")), 13050);
        QCOMPARE(amount(w2, QStringLiteral("16")), 900000);
        QCOMPARE(amount(w2, QStringLiteral("17")), 36900);
        QCOMPARE(value(w2, QStringLiteral("a")), QStringLiteral("•••-••-0001"));
        // Identity numbers are compared, never printed by a failure.
        QVERIFY2(value(reports.formW2(2026, c.jane, true), QStringLiteral("a")) == QStringLiteral("900-00-0001"),
                 "the W-2 for filing shows the SSN in full");
        const ReportTable w3 = reports.formW3(2026);
        QCOMPARE(value(w3, QStringLiteral("c")), QStringLiteral("1"));
        QCOMPARE(amount(w3, QStringLiteral("1")), 900000);
        QCOMPARE(value(w3, QStringLiteral("e")), QStringLiteral("••-•••0000"));
    }

    void coloradoForms()
    {
        Year c;
        PayrollReports reports(c.f());
        const ReportTable dr1094 = reports.dr1094(QDate(2026, 10, 1), QDate(2026, 12, 31));
        QCOMPARE(amount(dr1094, QStringLiteral("1")), 36900);
        QCOMPARE(amount(dr1094, QStringLiteral("6")), 36900);
        const ReportTable dr1093 = reports.dr1093(2026);
        QCOMPARE(value(dr1093, QStringLiteral("W-2s")), QStringLiteral("1"));
        QCOMPARE(amount(dr1093, QStringLiteral("3A")), 36900);
        // UI: 9,000 gross and chargeable; the premium by part 137.70 +
        // 15.30 + 121.50 = 274.50 (what the paychecks accrued too).
        const ReportTable ui = reports.uiWageReport(2026, 4);
        const QString key = QStringLiteral("employee:%1").arg(c.jane);
        QCOMPARE(ui.money(key, QStringLiteral("chargeable")), 900000);
        QCOMPARE(ui.text(key, QStringLiteral("officer")), QStringLiteral("Y"));
        // Paid for a period holding the 12th: October (Oct 10–23) and
        // December (Dec 5–18), not November.
        QCOMPARE(ui.text(key, QStringLiteral("months")), QStringLiteral("101"));
        QCOMPARE(ui.text(key, QStringLiteral("ssn")), QStringLiteral("•••-••-0001"));
        QVERIFY(ui.notes.join(QLatin1Char(' ')).contains(QStringLiteral("274.50")));
        QCOMPARE(c.roleBalance(PayrollRole::ColoradoUi), 27450);
        // FAMLI: 9,000 subject, the employee's 0.44 % = 39.60.
        const ReportTable famli = reports.famliWageReport(2026, 4, true);
        QCOMPARE(famli.money(key, QStringLiteral("subject")), 900000);
        QCOMPARE(famli.money(key, QStringLiteral("employee")), 3960);
        QVERIFY2(famli.text(key, QStringLiteral("ssn")) == QStringLiteral("900-00-0001"), "the FAMLI report for filing shows the SSN");
    }

    // A business outside Colorado: Form 940 line 1a is its own state, the
    // W-2 has no state boxes (another state's items are not on the form)
    // and says so.
    void outsideColoradoForms()
    {
        PayrollCompany c;
        PayrollSetup s = PayrollRecords(c.f()).setup();
        s.colorado = false;
        s.stateCode = QStringLiteral("tx");
        s.optCity.clear();
        QVERIFY(PayrollRecords(c.f()).saveSetup(s).ok);
        QCOMPARE(PayrollRecords(c.f()).setup().stateCode, QStringLiteral("TX"));
        c.pay(QDate(2026, 10, 9));
        PayrollReports reports(c.f());
        QCOMPARE(value(reports.form940(2026), QStringLiteral("1a")), QStringLiteral("TX"));
        const ReportTable w2 = reports.formW2(2026, c.jane);
        QVERIFY(w2.row(QStringLiteral("line:15")) == nullptr);
        QVERIFY(w2.notes.join(QLatin1Char(' ')).contains(QStringLiteral("Colorado only")));
        QCOMPARE(amount(w2, QStringLiteral("1")), 300000);
        s.stateCode = QStringLiteral("Texas");
        QCOMPARE(PayrollRecords(c.f()).saveSetup(s).code, ErrorCode::Invalid);
        QVERIFY(PayrollReports::isColoradoForm(QStringLiteral("dr_1094")));
        QVERIFY(!PayrollReports::isColoradoForm(QStringLiteral("form_941")));
    }

    void yearWithoutTablesSaysSo()
    {
        PayrollCompany c;
        const ReportTable f = PayrollReports(c.f()).form941(2027, 1);
        QVERIFY(f.notes.join(QLatin1Char(' ')).contains(QStringLiteral("no 2027 payroll tax tables")));
    }
};

QTEST_GUILESS_MAIN(TstBooksPayrollForms)
#include "tst_books_payroll_forms.moc"
