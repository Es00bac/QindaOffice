// SPDX-License-Identifier: GPL-3.0-or-later
// Payroll Summary, Payroll Liability Balances, Employee Earnings Summary
// and Paycheck History (payroll_reports.h); the form worksheets are in
// payroll_forms.cpp.
#include "payroll_reports.h"

#include "company_file.h"
#include "payroll_ledger.h"
#include "payroll_liabilities.h"
#include "reports_util.h"

#include <QMap>

#include <functional>

namespace QindaOffice::Books {

using namespace Payroll;
using ReportUtil::makeTable;
using ReportUtil::moneyRow;
using ReportUtil::periodText;
using ReportUtil::sectionRow;

namespace {

// One payroll item: its label, how to read it off a paycheck, and the row
// kind it shows as.
struct Item {
    QString key;
    QString label;
    std::function<Money(const PaycheckRecord &)> amount;
    RowKind kind = RowKind::Account;
};

QList<Item> summaryItems()
{
    using R = const PaycheckRecord &;
    return {
        {QStringLiteral("section:wages"), QStringLiteral("Wages"), {}, RowKind::Section},
        {QStringLiteral("regular"), QStringLiteral("Regular pay"), [](R r) { return r.regular; }},
        {QStringLiteral("bonus"), QStringLiteral("Bonus (supplemental)"), [](R r) { return r.supplemental; }},
        {QStringLiteral("total:gross"), QStringLiteral("Gross pay"), [](R r) { return r.taxes.gross; }, RowKind::Subtotal},
        {QStringLiteral("section:pretax"), QStringLiteral("Deductions before taxes"), {}, RowKind::Section},
        {QStringLiteral("section125"), QStringLiteral("Health insurance (Section 125)"), [](R r) { return r.section125; }},
        {QStringLiteral("retirement"), QStringLiteral("Retirement plan deferrals"), [](R r) { return r.retirement; }},
        {QStringLiteral("section:withheld"), QStringLiteral("Taxes withheld"), {}, RowKind::Section},
        {QStringLiteral("fit"), QStringLiteral("Federal income tax"), [](R r) { return r.taxes.federalIncomeTax; }},
        {QStringLiteral("ss"), QStringLiteral("Social security"), [](R r) { return r.taxes.socialSecurity; }},
        {QStringLiteral("medicare"), QStringLiteral("Medicare"), [](R r) { return r.taxes.medicare; }},
        {QStringLiteral("addlMedicare"), QStringLiteral("Additional Medicare"), [](R r) { return r.taxes.additionalMedicare; }},
        {QStringLiteral("coWh"), QStringLiteral("Colorado income tax"), [](R r) { return r.taxes.coloradoIncomeTax; }},
        {QStringLiteral("famliEe"), QStringLiteral("Colorado FAMLI (employee)"), [](R r) { return r.taxes.famliEmployee; }},
        {QStringLiteral("optEe"), QStringLiteral("Local OPT (employee)"), [](R r) { return r.taxes.optEmployee; }},
        {QStringLiteral("total:withheld"), QStringLiteral("Total taxes withheld"), [](R r) { return r.taxes.employeeTaxes; },
         RowKind::Subtotal},
        {QStringLiteral("afterTax"), QStringLiteral("Deductions after taxes"), [](R r) { return r.afterTax; }},
        {QStringLiteral("total:net"), QStringLiteral("Net pay"), [](R r) { return r.taxes.netPay; }, RowKind::Total},
        {QStringLiteral("section:employer"), QStringLiteral("Employer taxes"), {}, RowKind::Section},
        {QStringLiteral("ssEr"), QStringLiteral("Social security (employer)"), [](R r) { return r.taxes.socialSecurityEmployer; }},
        {QStringLiteral("medicareEr"), QStringLiteral("Medicare (employer)"), [](R r) { return r.taxes.medicareEmployer; }},
        {QStringLiteral("futa"), QStringLiteral("Federal unemployment (FUTA)"), [](R r) { return r.taxes.futa; }},
        {QStringLiteral("coUi"), QStringLiteral("Colorado unemployment (UI)"), [](R r) { return r.taxes.coloradoUi; }},
        {QStringLiteral("famliEr"), QStringLiteral("Colorado FAMLI (employer)"), [](R r) { return r.taxes.famliEmployer; }},
        {QStringLiteral("optEr"), QStringLiteral("Local OPT (employer)"), [](R r) { return r.taxes.optEmployer; }},
        {QStringLiteral("total:employer"), QStringLiteral("Total employer taxes"), [](R r) { return r.taxes.employerTaxes; },
         RowKind::Subtotal},
        {QStringLiteral("total:cost"), QStringLiteral("Total payroll cost"),
         [](R r) { return r.taxes.gross + r.taxes.employerTaxes; }, RowKind::Total},
        {QStringLiteral("shareholderHealth"), QStringLiteral("2% shareholder health insurance (W-2 box 1, not paid by check)"),
         [](R r) { return r.shareholderHealth; }},
    };
}

} // namespace

PayrollReports::PayrollReports(CompanyFile &file)
    : m_file(file)
{
}

QList<PayrollReports::Entry> PayrollReports::catalog()
{
    const QString payroll = QStringLiteral("Payroll");
    const QString forms = QStringLiteral("Payroll tax forms");
    return {
        {QStringLiteral("payroll_summary"), QStringLiteral("Payroll Summary"), payroll,
         QStringLiteral("Gross pay, deductions, taxes withheld, net pay and employer taxes in a period, per employee.")},
        {QStringLiteral("payroll_liability_balances"), QStringLiteral("Payroll Liability Balances"), payroll,
         QStringLiteral("What paychecks owe each tax agency or plan, by period and due date: owed, paid, balance.")},
        {QStringLiteral("employee_earnings"), QStringLiteral("Employee Earnings Summary"), payroll,
         QStringLiteral("One line per employee: gross pay, every deduction and tax, net pay.")},
        {QStringLiteral("paycheck_history"), QStringLiteral("Paycheck History"), payroll,
         QStringLiteral("Every paycheck in a period, void and adjusted ones marked.")},
        {QStringLiteral("form_941"), QStringLiteral("Form 941 Worksheet"), forms,
         QStringLiteral("Employer's quarterly federal tax return, line by line, for the quarter the period ends in.")},
        {QStringLiteral("schedule_941b"), QStringLiteral("Schedule B (Form 941) Worksheet"), forms,
         QStringLiteral("A semiweekly depositor's tax liability by payday for the quarter.")},
        {QStringLiteral("form_940"), QStringLiteral("Form 940 Worksheet"), forms,
         QStringLiteral("Employer's annual federal unemployment (FUTA) tax return for the year the period ends in.")},
        {QStringLiteral("form_w2"), QStringLiteral("W-2 Worksheets"), forms,
         QStringLiteral("Each employee's Wage and Tax Statement for the year, box by box.")},
        {QStringLiteral("form_w3"), QStringLiteral("W-3 Worksheet"), forms,
         QStringLiteral("The year's W-2 totals for the Social Security Administration.")},
        {QStringLiteral("dr_1094"), QStringLiteral("Colorado DR 1094 Worksheet"), forms,
         QStringLiteral("Colorado wage withholding for a filing period.")},
        {QStringLiteral("dr_1093"), QStringLiteral("Colorado DR 1093 Worksheet"), forms,
         QStringLiteral("Colorado's annual transmittal: tax withheld on the W-2s against what was remitted.")},
        {QStringLiteral("ui_wage_report"), QStringLiteral("Colorado UI Wage Report"), forms,
         QStringLiteral("The quarter's wages per employee for MyUI Employer+, and the premium.")},
        {QStringLiteral("famli_wage_report"), QStringLiteral("Colorado FAMLI Wage Report"), forms,
         QStringLiteral("The quarter's wages and premiums per employee for My FAMLI+ Employer.")},
    };
}

std::optional<ReportTable> PayrollReports::build(const QString &id, const QDate &from, const QDate &to,
                                                 bool revealIds) const
{
    const int year = to.year();
    const int quarter = (to.month() - 1) / 3 + 1;
    if (id == QLatin1String("payroll_summary")) return payrollSummary(from, to);
    if (id == QLatin1String("payroll_liability_balances")) return liabilityBalances(from, to);
    if (id == QLatin1String("employee_earnings")) return employeeEarnings(from, to);
    if (id == QLatin1String("paycheck_history")) return paycheckHistory(from, to);
    if (id == QLatin1String("form_941")) return form941(year, quarter);
    if (id == QLatin1String("schedule_941b")) return schedule941B(year, quarter);
    if (id == QLatin1String("form_940")) return form940(year);
    if (id == QLatin1String("form_w2")) return formW2(year, 0, revealIds);
    if (id == QLatin1String("form_w3")) return formW3(year, revealIds);
    if (id == QLatin1String("dr_1094")) return dr1094(from, to);
    if (id == QLatin1String("dr_1093")) return dr1093(year);
    if (id == QLatin1String("ui_wage_report")) return uiWageReport(year, quarter, revealIds);
    if (id == QLatin1String("famli_wage_report")) return famliWageReport(year, quarter, revealIds);
    return std::nullopt;
}

ReportTable PayrollReports::payrollSummary(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("payroll_summary"), QStringLiteral("Payroll Summary"), periodText(from, to));
    const QList<PaycheckRecord> checks = PayrollLedger(m_file).paychecks(from, to);
    QMap<QString, qint64> employees; // name → id, so the columns are in name order
    for (const PaycheckRecord &r : checks) {
        employees.insert(r.employeeName, r.employeeId);
    }
    t.columns = {{QStringLiteral("label"), QStringLiteral("Payroll item"), ColumnType::Text}};
    for (auto it = employees.cbegin(); it != employees.cend(); ++it) {
        t.columns << ReportColumn{QStringLiteral("employee:%1").arg(it.value()), it.key(), ColumnType::Money};
    }
    t.columns << ReportColumn{QStringLiteral("total"), QStringLiteral("Total"), ColumnType::Money};
    for (const Item &item : summaryItems()) {
        if (!item.amount) {
            t.rows << sectionRow(item.key, item.label, int(t.columns.size()));
            continue;
        }
        QList<qint64> values;
        qint64 total = 0;
        for (auto it = employees.cbegin(); it != employees.cend(); ++it) {
            qint64 sum = 0;
            for (const PaycheckRecord &r : checks) {
                if (r.employeeId == it.value()) {
                    sum += item.amount(r).minor();
                }
            }
            values << sum;
            total += sum;
        }
        values << total;
        if (item.kind == RowKind::Account && total == 0) {
            continue; // an item nobody had
        }
        t.rows << moneyRow(item.kind, item.key, item.label, values, item.kind == RowKind::Account ? 1 : 0);
    }
    t.notes << QStringLiteral("%1 paychecks; void paychecks are left out.").arg(checks.size());
    return t;
}

ReportTable PayrollReports::liabilityBalances(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("payroll_liability_balances"), QStringLiteral("Payroll Liability Balances"),
                              periodText(from, to));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Liability"), ColumnType::Text},
                 {QStringLiteral("due"), QStringLiteral("Due"), ColumnType::Date},
                 {QStringLiteral("accrued"), QStringLiteral("Owed"), ColumnType::Money},
                 {QStringLiteral("paid"), QStringLiteral("Paid"), ColumnType::Money},
                 {QStringLiteral("balance"), QStringLiteral("Balance"), ColumnType::Money}};
    QMap<QString, QList<LiabilityDue>> byGroup;
    QStringList order;
    for (const LiabilityDue &row : PayrollLiabilities(m_file).liabilities(from, to)) {
        if (!byGroup.contains(row.tax)) {
            order << row.tax;
        }
        byGroup[row.tax] << row;
    }
    qint64 grand[3] = {0, 0, 0};
    for (const QString &tax : std::as_const(order)) {
        const QList<LiabilityDue> &rows = byGroup.value(tax);
        t.rows << sectionRow(QStringLiteral("section:") + tax, rows.first().label, int(t.columns.size()));
        qint64 sums[3] = {0, 0, 0};
        for (const LiabilityDue &row : rows) {
            ReportRow r;
            r.kind = RowKind::Account;
            r.depth = 1;
            r.key = QStringLiteral("%1:%2").arg(tax, row.start.toString(Qt::ISODate));
            r.cells << ReportCell::ofText(row.period)
                    << (row.due.isValid() ? ReportCell::ofDate(row.due) : ReportCell::blank())
                    << ReportCell::ofMoney(row.accrued.minor()) << ReportCell::ofMoney(row.paid.minor())
                    << ReportCell::ofMoney(row.balance.minor());
            r.drill.from = row.start;
            r.drill.to = row.end;
            t.rows << r;
            sums[0] += row.accrued.minor();
            sums[1] += row.paid.minor();
            sums[2] += row.balance.minor();
        }
        ReportRow total = moneyRow(RowKind::Subtotal, QStringLiteral("total:") + tax, QStringLiteral("Total"),
                                   {0, sums[0], sums[1], sums[2]});
        total.cells[1] = ReportCell::blank();
        t.rows << total;
        for (int i = 0; i < 3; ++i) {
            grand[i] += sums[i];
        }
    }
    ReportRow total = moneyRow(RowKind::Total, QStringLiteral("total:all"), QStringLiteral("Total payroll liabilities"),
                               {0, grand[0], grand[1], grand[2]});
    total.cells[1] = ReportCell::blank();
    t.rows << total;
    return t;
}

ReportTable PayrollReports::employeeEarnings(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("employee_earnings"), QStringLiteral("Employee Earnings Summary"),
                              periodText(from, to));
    const QList<Item> items = summaryItems();
    const QStringList keys{QStringLiteral("total:gross"), QStringLiteral("section125"), QStringLiteral("retirement"),
                           QStringLiteral("fit"), QStringLiteral("ss"), QStringLiteral("medicare"),
                           QStringLiteral("addlMedicare"), QStringLiteral("coWh"), QStringLiteral("famliEe"),
                           QStringLiteral("optEe"), QStringLiteral("afterTax"), QStringLiteral("total:net"),
                           QStringLiteral("total:employer")};
    t.columns = {{QStringLiteral("label"), QStringLiteral("Employee"), ColumnType::Text}};
    QList<Item> chosen;
    for (const QString &key : keys) {
        for (const Item &item : items) {
            if (item.key == key) {
                chosen << item;
                t.columns << ReportColumn{key, item.label, ColumnType::Money};
            }
        }
    }
    QMap<QString, QList<PaycheckRecord>> byEmployee;
    for (const PaycheckRecord &r : PayrollLedger(m_file).paychecks(from, to)) {
        byEmployee[r.employeeName] << r;
    }
    QList<qint64> totals(chosen.size(), 0);
    for (auto it = byEmployee.cbegin(); it != byEmployee.cend(); ++it) {
        QList<qint64> values;
        for (qsizetype i = 0; i < chosen.size(); ++i) {
            qint64 sum = 0;
            for (const PaycheckRecord &r : it.value()) {
                sum += chosen.at(i).amount(r).minor();
            }
            values << sum;
            totals[i] += sum;
        }
        ReportRow row = moneyRow(RowKind::Account, QStringLiteral("employee:%1").arg(it.value().first().employeeId),
                                 it.key(), values);
        for (const PaycheckRecord &r : it.value()) {
            row.drill.documentIds << r.docId;
        }
        t.rows << row;
    }
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:all"), QStringLiteral("Total"), totals);
    return t;
}

ReportTable PayrollReports::paycheckHistory(const QDate &from, const QDate &to, qint64 employeeId) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("paycheck_history"), QStringLiteral("Paycheck History"),
                              periodText(from, to));
    t.columns = {{QStringLiteral("date"), QStringLiteral("Pay date"), ColumnType::Date},
                 {QStringLiteral("number"), QStringLiteral("Check no."), ColumnType::Text},
                 {QStringLiteral("employee"), QStringLiteral("Employee"), ColumnType::Text},
                 {QStringLiteral("gross"), QStringLiteral("Gross pay"), ColumnType::Money},
                 {QStringLiteral("withheld"), QStringLiteral("Taxes withheld"), ColumnType::Money},
                 {QStringLiteral("deductions"), QStringLiteral("Deductions"), ColumnType::Money},
                 {QStringLiteral("net"), QStringLiteral("Net pay"), ColumnType::Money},
                 {QStringLiteral("employer"), QStringLiteral("Employer taxes"), ColumnType::Money},
                 {QStringLiteral("status"), QStringLiteral("Status"), ColumnType::Text}};
    qint64 sums[5] = {0, 0, 0, 0, 0};
    for (const PaycheckRecord &r : PayrollLedger(m_file).paychecks(from, to, employeeId, true)) {
        ReportRow row;
        row.kind = RowKind::Detail;
        row.key = QStringLiteral("doc:%1").arg(r.docId);
        const Money deductions = r.taxes.preTaxDeductions + r.taxes.afterTaxDeductions;
        row.cells << ReportCell::ofDate(r.payDate) << ReportCell::ofText(r.number) << ReportCell::ofText(r.employeeName)
                  << ReportCell::ofMoney(r.taxes.gross.minor()) << ReportCell::ofMoney(r.taxes.employeeTaxes.minor())
                  << ReportCell::ofMoney(deductions.minor()) << ReportCell::ofMoney(r.taxes.netPay.minor())
                  << ReportCell::ofMoney(r.taxes.employerTaxes.minor())
                  << ReportCell::ofText(r.voided ? QStringLiteral("Void")
                                                 : (r.replacesDocId != 0 ? QStringLiteral("Adjusted") : QString()));
        row.drill.documentIds << r.docId;
        t.rows << row;
        if (!r.voided) {
            sums[0] += r.taxes.gross.minor();
            sums[1] += r.taxes.employeeTaxes.minor();
            sums[2] += deductions.minor();
            sums[3] += r.taxes.netPay.minor();
            sums[4] += r.taxes.employerTaxes.minor();
        }
    }
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total:all");
    total.cells << ReportCell::ofText(QStringLiteral("Total (live paychecks)")) << ReportCell::blank() << ReportCell::blank();
    for (const qint64 s : sums) {
        total.cells << ReportCell::ofMoney(s);
    }
    total.cells << ReportCell::blank();
    t.rows << total;
    return t;
}

} // namespace QindaOffice::Books
