// SPDX-License-Identifier: GPL-3.0-or-later
// Colorado payroll worksheets (payroll_reports.h): DR 1094 (08/18/20) for a
// filing period, DR 1093 (08/14/24) for the year, the CDLE quarterly UI
// wage report (MyUI Employer+) and the FAMLI quarterly wage report (My
// FAMLI+ Employer). Neither agency publishes its screen's line labels, so
// the two wage reports carry the fields their upload formats ask for.
#include "payroll_forms_p.h"

#include "company_file.h"
#include "payroll_records.h"
#include "payroll_reports.h"

#include "colorado_payroll.h"
#include "payroll_calendar.h"

#include <QMap>
#include <QSet>

namespace QindaOffice::Books {

using namespace Payroll;
using namespace PayrollForms;

namespace {

Money coWithheld(const PaycheckRecord &r) { return r.taxes.coloradoIncomeTax; }

QString ssnShown(const QString &ssn, bool reveal)
{
    const QString digits = normalizedTaxId(ssn, 9);
    if (digits.isEmpty()) {
        return QStringLiteral("(missing)");
    }
    return reveal ? digits.left(3) + QLatin1Char('-') + digits.mid(3, 2) + QLatin1Char('-') + digits.right(4)
                  : maskedSsn(digits);
}

// Per-employee rows for the wage reports: SSN, name, then money columns.
ReportTable wageTable(CompanyFile &file, const QString &id, const QString &title, const QString &subtitle,
                      const QList<ReportColumn> &money)
{
    ReportTable t;
    t.id = id;
    t.title = title;
    t.company = file.settings().displayName();
    t.subtitle = subtitle;
    t.currency = file.currency();
    t.columns = {{QStringLiteral("ssn"), QStringLiteral("SSN"), ColumnType::Text},
                 {QStringLiteral("last"), QStringLiteral("Last name"), ColumnType::Text},
                 {QStringLiteral("first"), QStringLiteral("First name"), ColumnType::Text},
                 {QStringLiteral("mi"), QStringLiteral("MI"), ColumnType::Text}};
    t.columns << money;
    return t;
}

} // namespace

ReportTable PayrollReports::dr1094(const QDate &start, const QDate &end) const
{
    FormTable f(m_file, QStringLiteral("dr_1094"), QStringLiteral("DR 1094 worksheet"),
                QStringLiteral("Colorado W-2 Wage Withholding Tax Return · %1 – %2")
                    .arg(start.toString(QStringLiteral("MM/yy")), end.toString(QStringLiteral("MM/yy"))));
    const PayrollSetup setup = PayrollRecords(m_file).setup();
    f.text(QStringLiteral("Account"), QStringLiteral("Colorado account number"),
           setup.coloradoAccount.isEmpty() ? QStringLiteral("(enter it in Payroll Setup)") : setup.coloradoAccount);
    const Money l1 = sum(checks(m_file, start, end), coWithheld);
    f.line(QStringLiteral("1"), QStringLiteral("Total Colorado tax withheld from wages reportable on a W-2"), l1);
    f.line(QStringLiteral("2"), QStringLiteral("Overpayment of tax for prior record(s) of the current year only"), usd(0));
    f.line(QStringLiteral("3"), QStringLiteral("Line 1 minus line 2"), l1);
    f.line(QStringLiteral("4"), QStringLiteral("Penalty"), usd(0));
    f.line(QStringLiteral("5"), QStringLiteral("Interest"), usd(0));
    f.line(QStringLiteral("6"), QStringLiteral("Amount owed (lines 3, 4 and 5)"), l1);
    f.line(QStringLiteral("Paid"), QStringLiteral("Already remitted for this period"),
           deposited(m_file, QStringLiteral("coWithholding"), start, end));
    f.note(QStringLiteral("Pay through Revenue Online or EFT; an EFT payment files the return (do not also file a paper "
                          "DR 1094)."));
    return f.table();
}

ReportTable PayrollReports::dr1093(int year) const
{
    FormTable f(m_file, QStringLiteral("dr_1093"), QStringLiteral("DR 1093 worksheet"),
                QStringLiteral("Annual Transmittal of State W-2 Forms %1").arg(year));
    const QList<PaycheckRecord> list = checks(m_file, QDate(year, 1, 1), QDate(year, 12, 31));
    QSet<qint64> employees;
    for (const PaycheckRecord &r : list) {
        if (r.taxes.coloradoWages.isPositive()) {
            employees.insert(r.employeeId);
        }
    }
    f.text(QStringLiteral("W-2s"), QStringLiteral("Number of W-2s"), QString::number(employees.size()));
    const Money l1 = sum(list, coWithheld);
    const Money l2 = deposited(m_file, QStringLiteral("coWithholding"), QDate(year, 1, 1), QDate(year, 12, 31));
    f.line(QStringLiteral("1"), QStringLiteral("Total Colorado income taxes withheld per W-2 forms"), l1);
    f.line(QStringLiteral("2"), QStringLiteral("Total Colorado income taxes remitted for the period"), l2);
    f.line(QStringLiteral("3A"), QStringLiteral("Balance due"), nonNegative(l1 - l2));
    f.line(QStringLiteral("3B"), QStringLiteral("Overpayment"), nonNegative(l2 - l1));
    f.line(QStringLiteral("4"), QStringLiteral("Penalty"), usd(0));
    f.line(QStringLiteral("5"), QStringLiteral("Interest"), usd(0));
    f.line(QStringLiteral("6"), QStringLiteral("Additional balance paid (lines 3A, 4 and 5)"), nonNegative(l1 - l2));
    f.note(QStringLiteral("Due by the last day of January (February 1, %1 when January 31 is a Sunday). Employers with 10 "
                          "or more W-2s file them electronically through Revenue Online.")
               .arg(year + 1));
    return f.table();
}

ReportTable PayrollReports::uiWageReport(int year, int quarter, bool revealIds) const
{
    ReportTable t = wageTable(m_file, QStringLiteral("ui_wage_report"), QStringLiteral("Colorado UI quarterly wage report"),
                              QStringLiteral("%1 · MyUI Employer+").arg(quarterLabel(year, quarter)),
                              {{QStringLiteral("gross"), QStringLiteral("UI gross wages"), ColumnType::Money},
                               {QStringLiteral("chargeable"), QStringLiteral("Chargeable wages"), ColumnType::Money},
                               {QStringLiteral("months"), QStringLiteral("Employed on the 12th (months 1–3)"), ColumnType::Text},
                               {QStringLiteral("officer"), QStringLiteral("Owner/officer"), ColumnType::Text}});
    const QDate qs = quarterStart(year, quarter);
    const QDate qe = quarterEnd(year, quarter);
    QMap<qint64, QList<PaycheckRecord>> byEmployee;
    for (const PaycheckRecord &r : checks(m_file, qs, qe)) {
        byEmployee[r.employeeId] << r;
    }
    PayrollRecords records(m_file);
    Money gross = usd(0);
    Money chargeable = usd(0);
    for (auto it = byEmployee.cbegin(); it != byEmployee.cend(); ++it) {
        const EmployeePayroll e = records.employee(it.key());
        Money g = usd(0);
        Money c = usd(0);
        QString months;
        for (int m = 0; m < 3; ++m) {
            const QDate twelfth(year, (quarter - 1) * 3 + 1 + m, 12);
            bool on = false;
            for (const PaycheckRecord &r : it.value()) {
                on = on || (r.periodStart.isValid() && r.periodStart <= twelfth && twelfth <= r.periodEnd);
            }
            months += on ? QLatin1Char('1') : QLatin1Char('0');
        }
        for (const PaycheckRecord &r : it.value()) {
            g += r.taxes.gross - r.section125;
            c += r.taxes.uiWages;
        }
        ReportRow row;
        row.kind = RowKind::Account;
        row.key = QStringLiteral("employee:%1").arg(it.key());
        row.cells << ReportCell::ofText(ssnShown(e.ssn, revealIds)) << ReportCell::ofText(e.lastName)
                  << ReportCell::ofText(e.firstName) << ReportCell::ofText(e.middleInitial.left(1))
                  << ReportCell::ofMoney(g.minor()) << ReportCell::ofMoney(c.minor()) << ReportCell::ofText(months)
                  << ReportCell::ofText(e.ownerEmployee ? QStringLiteral("Y") : QStringLiteral("N"));
        t.rows << row;
        gross += g;
        chargeable += c;
    }
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total");
    total.cells << ReportCell::ofText(QStringLiteral("Total")) << ReportCell::blank() << ReportCell::blank()
                << ReportCell::blank() << ReportCell::ofMoney(gross.minor()) << ReportCell::ofMoney(chargeable.minor())
                << ReportCell::blank() << ReportCell::blank();
    t.rows << total;
    // The premium CDLE computes: each part of the rate on the quarter's
    // chargeable wages, rounded on its own.
    const std::optional<PayrollTables> tables = PayrollTables::forYear(year);
    const PayrollSetup setup = records.setup();
    if (tables) {
        const UiRate rate = !setup.uiRate.isEmpty() ? setup.uiRate
                                                    : (setup.uiHeavyConstruction ? tables->colorado().uiHeavyConstructionRate
                                                                                 : tables->colorado().uiIntroductoryRate);
        for (const UiRatePart &part : rate) {
            t.notes << QStringLiteral("%1 %2: %3").arg(part.name, part.rate.toPercentString(),
                                                       usdText(percentOf(chargeable, part.rate)));
        }
        t.notes << QStringLiteral("Premium: %1 on %2 of chargeable wages (MyUI Employer+ computes it again).")
                       .arg(usdText(uiPremium(chargeable, rate)), usdText(chargeable));
    }
    t.notes << QStringLiteral("UI gross wages leave out cafeteria-plan (section 125) benefits; 401(k) deferrals stay in.");
    return t;
}

ReportTable PayrollReports::famliWageReport(int year, int quarter, bool revealIds) const
{
    ReportTable t = wageTable(m_file, QStringLiteral("famli_wage_report"), QStringLiteral("Colorado FAMLI quarterly wage report"),
                              QStringLiteral("%1 · My FAMLI+ Employer").arg(quarterLabel(year, quarter)),
                              {{QStringLiteral("ytd"), QStringLiteral("Year-to-date wages"), ColumnType::Money},
                               {QStringLiteral("gross"), QStringLiteral("Gross wages this quarter"), ColumnType::Money},
                               {QStringLiteral("subject"), QStringLiteral("Subject wages this quarter"), ColumnType::Money},
                               {QStringLiteral("employee"), QStringLiteral("Employee contribution"), ColumnType::Money},
                               {QStringLiteral("employer"), QStringLiteral("Employer contribution"), ColumnType::Money}});
    const QDate qs = quarterStart(year, quarter);
    const QDate qe = quarterEnd(year, quarter);
    QMap<qint64, QList<PaycheckRecord>> byEmployee;
    for (const PaycheckRecord &r : checks(m_file, qs, qe)) {
        byEmployee[r.employeeId] << r;
    }
    PayrollRecords records(m_file);
    PayrollLedger ledger(m_file);
    Money totals[5] = {usd(0), usd(0), usd(0), usd(0), usd(0)};
    for (auto it = byEmployee.cbegin(); it != byEmployee.cend(); ++it) {
        const EmployeePayroll e = records.employee(it.key());
        Money values[5] = {ledger.yearToDate(it.key(), qe).gross, usd(0), usd(0), usd(0), usd(0)};
        for (const PaycheckRecord &r : it.value()) {
            values[1] += r.taxes.gross;
            values[2] += r.taxes.famliWages;
            values[3] += r.taxes.famliEmployee;
            values[4] += r.taxes.famliEmployer;
        }
        ReportRow row;
        row.kind = RowKind::Account;
        row.key = QStringLiteral("employee:%1").arg(it.key());
        row.cells << ReportCell::ofText(ssnShown(e.ssn, revealIds)) << ReportCell::ofText(e.lastName)
                  << ReportCell::ofText(e.firstName) << ReportCell::ofText(e.middleInitial.left(1));
        for (int i = 0; i < 5; ++i) {
            row.cells << ReportCell::ofMoney(values[i].minor());
            totals[i] += values[i];
        }
        t.rows << row;
    }
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total");
    total.cells << ReportCell::ofText(QStringLiteral("Total")) << ReportCell::blank() << ReportCell::blank() << ReportCell::blank();
    for (int i = 0; i < 5; ++i) {
        total.cells << ReportCell::ofMoney(totals[i].minor());
    }
    t.rows << total;
    t.notes << QStringLiteral("Premium due for the quarter: %1 (employee %2 + employer %3). Subject wages stop at the "
                              "social security wage base.")
                   .arg(usdText(totals[3] + totals[4]), usdText(totals[3]), usdText(totals[4]));
    return t;
}

} // namespace QindaOffice::Books
