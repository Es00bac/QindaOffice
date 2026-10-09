// SPDX-License-Identifier: GPL-3.0-or-later
// Federal payroll form worksheets (payroll_reports.h): Form 941 (Rev.
// March 2026) with Schedule B and Form 940 (2026: the draft repeats the
// 2025 lines); the W-2 and W-3 are in payroll_forms_w2.cpp. Line numbers
// and labels are the forms' own, shortened; docs/books.md "Payroll reports
// and forms" lists the sources.
#include "payroll_forms_p.h"

#include "company_file.h"
#include "documents.h"
#include "payroll_records.h"
#include "payroll_reports.h"
#include "sql_util.h"

#include "colorado_payroll.h"
#include "payroll_calendar.h"

#include <QMap>
#include <QSet>

namespace QindaOffice::Books {

using namespace Payroll;
using namespace PayrollForms;

namespace PayrollForms {

namespace {

QString labelOf(const QString &number, const QString &text)
{
    return number.isEmpty() ? text : number + QStringLiteral("  ") + text;
}

} // namespace

FormTable::FormTable(CompanyFile &file, const QString &id, const QString &title, const QString &subtitle)
{
    m_table.id = id;
    m_table.title = title;
    m_table.company = file.settings().displayName();
    m_table.subtitle = subtitle;
    m_table.currency = file.currency();
    // AGENT-NOTE: a report's first column is its label (the app's table and
    // the PDF give it the room and the left edge), so a line's number leads
    // its words there: "5a  Taxable social security wages × 0.124".
    m_table.columns = {{QStringLiteral("label"), QStringLiteral("Line"), ColumnType::Text},
                       {QStringLiteral("column1"), QStringLiteral("Column 1"), ColumnType::Money},
                       {QStringLiteral("amount"), QStringLiteral("Amount"), ColumnType::Money}};
}

void FormTable::section(const QString &title)
{
    ReportRow r;
    r.kind = RowKind::Section;
    r.key = QStringLiteral("section:%1").arg(m_table.rows.size());
    r.cells << ReportCell::ofText(title) << ReportCell::blank() << ReportCell::blank();
    m_table.rows << r;
}

void FormTable::line(const QString &number, const QString &text, const std::optional<Money> &amount,
                     const std::optional<Money> &column1)
{
    ReportRow r;
    r.kind = RowKind::Account;
    r.key = m_prefix + QStringLiteral("line:") + number;
    r.cells << ReportCell::ofText(labelOf(number, text))
            << (column1 ? ReportCell::ofMoney(column1->minor()) : ReportCell::blank())
            << (amount ? ReportCell::ofMoney(amount->minor()) : ReportCell::blank());
    m_table.rows << r;
}

void FormTable::text(const QString &number, const QString &text, const QString &value)
{
    ReportRow r;
    r.kind = RowKind::Detail;
    r.key = m_prefix + QStringLiteral("line:") + number;
    r.cells << ReportCell::ofText(labelOf(number, text)) << ReportCell::blank() << ReportCell::ofText(value);
    m_table.rows << r;
}

QList<PaycheckRecord> checks(CompanyFile &file, const QDate &from, const QDate &to)
{
    return PayrollLedger(file).paychecks(from, to);
}

Money sum(const QList<PaycheckRecord> &list, Money (*field)(const PaycheckRecord &))
{
    Money total = usd(0);
    for (const PaycheckRecord &r : list) {
        total += field(r);
    }
    return total;
}

Money deposited(CompanyFile &file, const QString &tax, const QDate &from, const QDate &to)
{
    return Money(Sql::scalarInt(file.database(),
                                QStringLiteral("SELECT COALESCE(SUM(d.total), 0) FROM payroll_payments pp"
                                               " JOIN documents d ON d.id = pp.doc_id WHERE d.state <> ? AND pp.tax = ?"
                                               " AND pp.period_start >= ? AND pp.period_end <= ?"),
                                {int(DocState::Void), tax, Sql::isoDate(from), Sql::isoDate(to)}),
                 file.currency());
}

QString einText(CompanyFile &file, bool reveal)
{
    const QString ein = PayrollRecords(file).setup().ein;
    if (ein.isEmpty()) {
        return QStringLiteral("(enter the EIN in Payroll Setup)");
    }
    return reveal ? ein.left(2) + QLatin1Char('-') + ein.mid(2) : maskedEin(ein);
}

} // namespace PayrollForms

namespace {

Money fit(const PaycheckRecord &r) { return r.taxes.federalIncomeTax; }
Money federalWages(const PaycheckRecord &r) { return r.taxes.federalWages; }
Money ssWages(const PaycheckRecord &r) { return r.taxes.socialSecurityWages; }
Money medicareWages(const PaycheckRecord &r) { return r.taxes.medicareWages; }
Money employeeFica(const PaycheckRecord &r) { return r.taxes.socialSecurity + r.taxes.medicare + r.taxes.additionalMedicare; }
Money ficaTotal(const PaycheckRecord &r)
{
    return r.taxes.federalIncomeTax + r.taxes.socialSecurity + r.taxes.socialSecurityEmployer + r.taxes.medicare
        + r.taxes.medicareEmployer + r.taxes.additionalMedicare;
}
Money futa(const PaycheckRecord &r) { return r.taxes.futa; }

QString quarterText(int year, int quarter)
{
    static const char *const months[] = {"January–March", "April–June", "July–September", "October–December"};
    return QStringLiteral("Quarter %1 of %2 (%3)").arg(quarter).arg(year).arg(QString::fromUtf8(months[quarter - 1]));
}

} // namespace

ReportTable PayrollReports::form941(int year, int quarter) const
{
    const QDate qs = quarterStart(year, quarter);
    const QDate qe = quarterEnd(year, quarter);
    FormTable f(m_file, QStringLiteral("form_941"), QStringLiteral("Form 941 worksheet"),
                quarterText(year, quarter) + QStringLiteral(" · Form 941 (Rev. March 2026)"));
    const std::optional<PayrollTables> t = PayrollTables::forYear(year);
    if (!t) {
        f.note(PayrollTables::missingYearMessage(year));
        return f.table();
    }
    const QList<PaycheckRecord> list = checks(m_file, qs, qe);
    const FicaTables &fica = t->fica();
    f.text(QStringLiteral("EIN"), QStringLiteral("Employer identification number"), einText(m_file, false));
    f.section(QStringLiteral("Part 1: Answer these questions for this quarter"));
    // Line 1: on the payroll for the pay period that includes the 12th of
    // the quarter's last month.
    const QDate twelfth(year, quarter * 3, 12);
    QSet<qint64> counted;
    for (const PaycheckRecord &r : list) {
        const bool spans = r.periodStart.isValid() && r.periodEnd.isValid() ? r.periodStart <= twelfth && twelfth <= r.periodEnd
                                                                          : r.payDate.month() == twelfth.month();
        if (spans) {
            counted.insert(r.employeeId);
        }
    }
    f.text(QStringLiteral("1"), QStringLiteral("Employees paid for the pay period including %1")
                                    .arg(twelfth.toString(QStringLiteral("MMM d"))),
           QString::number(counted.size()));
    const Money line2 = sum(list, federalWages);
    const Money line3 = sum(list, fit);
    f.line(QStringLiteral("2"), QStringLiteral("Wages, tips, and other compensation"), line2);
    f.line(QStringLiteral("3"), QStringLiteral("Federal income tax withheld"), line3);
    const Money ss = sum(list, ssWages);
    const Money med = sum(list, medicareWages);
    f.text(QStringLiteral("4"), QStringLiteral("No wages subject to social security or Medicare tax"),
           ss.isZero() && med.isZero() && !list.isEmpty() ? QStringLiteral("☒") : QStringLiteral("☐"));
    // 5d's wages: the part of each employee's Medicare wages above the
    // threshold paid in this quarter.
    Money addlWages = usd(0);
    PayrollLedger ledger(m_file);
    QSet<qint64> employees;
    for (const PaycheckRecord &r : list) {
        employees.insert(r.employeeId);
    }
    for (const qint64 id : std::as_const(employees)) {
        const Money before = quarter == 1 ? usd(0) : ledger.yearToDate(id, qs.addDays(-1)).ytd.medicareWages;
        const Money through = ledger.yearToDate(id, qe).ytd.medicareWages;
        addlWages += nonNegative(through - maxMoney(before, fica.additionalMedicareThreshold));
    }
    const Decimal ssBoth = ratePlus(fica.socialSecurityRate, fica.socialSecurityRate);
    const Decimal medBoth = ratePlus(fica.medicareRate, fica.medicareRate);
    const Money l5a = percentOf(ss, ssBoth);
    const Money l5c = percentOf(med, medBoth);
    const Money l5d = percentOf(addlWages, fica.additionalMedicareRate);
    const Money l5e = l5a + l5c + l5d;
    f.line(QStringLiteral("5a"), QStringLiteral("Taxable social security wages × %1").arg(ssBoth.toString()), l5a, ss);
    f.line(QStringLiteral("5b"), QStringLiteral("Taxable social security tips"), usd(0), usd(0));
    f.line(QStringLiteral("5c"), QStringLiteral("Taxable Medicare wages & tips × %1").arg(medBoth.toString()), l5c, med);
    f.line(QStringLiteral("5d"), QStringLiteral("Wages subject to Additional Medicare Tax withholding × %1")
                                     .arg(fica.additionalMedicareRate.toString()),
           l5d, addlWages);
    f.line(QStringLiteral("5e"), QStringLiteral("Total social security and Medicare taxes"), l5e);
    f.line(QStringLiteral("5f"), QStringLiteral("Section 3121(q) Notice and Demand"), usd(0));
    const Money l6 = line3 + l5e;
    f.line(QStringLiteral("6"), QStringLiteral("Total taxes before adjustments"), l6);
    // Line 7: what was withheld from employees less the employees' share of
    // 5a–5d (the employer's share was recorded on the totals).
    const Money employeeShare = percentOf(ss, fica.socialSecurityRate) + percentOf(med, fica.medicareRate) + l5d;
    const Money l7 = sum(list, employeeFica) - employeeShare;
    f.line(QStringLiteral("7"), QStringLiteral("Current quarter's adjustment for fractions of cents"), l7);
    f.line(QStringLiteral("8"), QStringLiteral("Current quarter's adjustment for sick pay"), usd(0));
    f.line(QStringLiteral("9"), QStringLiteral("Current quarter's adjustments for tips and group-term life insurance"), usd(0));
    const Money l10 = l6 + l7;
    f.line(QStringLiteral("10"), QStringLiteral("Total taxes after adjustments"), l10);
    f.line(QStringLiteral("11"), QStringLiteral("Qualified small business payroll tax credit (Form 8974)"), usd(0));
    const Money l12 = l10;
    f.line(QStringLiteral("12"), QStringLiteral("Total taxes after adjustments and nonrefundable credits"), l12);
    const Money l13 = deposited(m_file, QStringLiteral("federal941"), qs, qe);
    f.line(QStringLiteral("13"), QStringLiteral("Total deposits for this quarter"), l13);
    f.line(QStringLiteral("14"), QStringLiteral("Balance due"), nonNegative(l12 - l13));
    f.line(QStringLiteral("15a"), QStringLiteral("Overpayment"), nonNegative(l13 - l12));
    f.section(QStringLiteral("Part 2: Your deposit schedule and tax liability for this quarter"));
    const PayrollSetup setup = PayrollRecords(m_file).setup();
    if (l12 < t->deposits().quarterlyDeMinimis) {
        f.text(QStringLiteral("16"), QStringLiteral("Line 12 is less than %1: box 1").arg(usdText(t->deposits().quarterlyDeMinimis)),
               QStringLiteral("☒"));
    } else if (setup.depositSchedule == DepositSchedule::Semiweekly) {
        f.text(QStringLiteral("16"), QStringLiteral("Semiweekly schedule depositor: box 3, attach Schedule B"), QStringLiteral("☒"));
    } else {
        f.text(QStringLiteral("16"), QStringLiteral("Monthly schedule depositor: box 2"), QStringLiteral("☒"));
    }
    Money total = usd(0);
    for (int m = 0; m < 3; ++m) {
        Money month = usd(0);
        for (const PaycheckRecord &r : list) {
            if (r.payDate.month() == (quarter - 1) * 3 + 1 + m) {
                month += ficaTotal(r);
            }
        }
        total += month;
        f.line(QStringLiteral("16 M%1").arg(m + 1), QStringLiteral("Tax liability: Month %1").arg(m + 1), month);
    }
    f.line(QStringLiteral("16 T"), QStringLiteral("Total liability for quarter (must equal line 12)"), total);
    if (total != l12) {
        f.note(QStringLiteral("The paychecks' liability differs from line 12 by %1 — a paycheck voided in the middle of "
                              "the quarter left a rounding difference; Form 941 line 7 may carry it.")
                   .arg(usdText(total - l12)));
    }
    f.note(QStringLiteral("QindaBooks prepares the numbers; the business files Form 941 and pays through EFTPS."));
    return f.table();
}

ReportTable PayrollReports::schedule941B(int year, int quarter) const
{
    ReportTable t;
    t.id = QStringLiteral("schedule_941b");
    t.title = QStringLiteral("Schedule B (Form 941) worksheet");
    t.company = m_file.settings().displayName();
    t.subtitle = quarterText(year, quarter) + QStringLiteral(" · Report of tax liability for semiweekly schedule depositors");
    t.currency = m_file.currency();
    t.columns = {{QStringLiteral("date"), QStringLiteral("Pay date"), ColumnType::Date},
                 {QStringLiteral("liability"), QStringLiteral("Tax liability"), ColumnType::Money}};
    QMap<QDate, Money> byDay;
    for (const PaycheckRecord &r : checks(m_file, quarterStart(year, quarter), quarterEnd(year, quarter))) {
        byDay[r.payDate] = byDay.value(r.payDate, usd(0)) + ficaTotal(r);
    }
    Money total = usd(0);
    for (int m = 0; m < 3; ++m) {
        const int month = (quarter - 1) * 3 + 1 + m;
        Money monthTotal = usd(0);
        for (auto it = byDay.cbegin(); it != byDay.cend(); ++it) {
            if (it.key().month() != month) {
                continue;
            }
            ReportRow row;
            row.kind = RowKind::Account;
            row.key = QStringLiteral("day:") + Sql::isoDate(it.key());
            row.cells << ReportCell::ofDate(it.key()) << ReportCell::ofMoney(it.value().minor());
            t.rows << row;
            monthTotal += it.value();
        }
        ReportRow sub;
        sub.kind = RowKind::Subtotal;
        sub.key = QStringLiteral("month:%1").arg(m + 1);
        sub.cells << ReportCell::ofText(QStringLiteral("Tax liability for Month %1").arg(m + 1))
                  << ReportCell::ofMoney(monthTotal.minor());
        t.rows << sub;
        total += monthTotal;
    }
    ReportRow all;
    all.kind = RowKind::Total;
    all.key = QStringLiteral("total");
    all.cells << ReportCell::ofText(QStringLiteral("Total liability for the quarter (equals Form 941 line 12)"))
              << ReportCell::ofMoney(total.minor());
    t.rows << all;
    t.notes << QStringLiteral("Only a semiweekly schedule depositor files Schedule B, and not when line 12 is under $2,500.");
    return t;
}

ReportTable PayrollReports::form940(int year) const
{
    FormTable f(m_file, QStringLiteral("form_940"), QStringLiteral("Form 940 worksheet"),
                QStringLiteral("Calendar year %1 · Form 940").arg(year));
    const std::optional<PayrollTables> t = PayrollTables::forYear(year);
    if (!t) {
        f.note(PayrollTables::missingYearMessage(year));
        return f.table();
    }
    const QList<PaycheckRecord> list = checks(m_file, QDate(year, 1, 1), QDate(year, 12, 31));
    f.text(QStringLiteral("EIN"), QStringLiteral("Employer identification number"), einText(m_file, false));
    f.section(QStringLiteral("Part 1: Tell us about your return"));
    const PayrollSetup setup = PayrollRecords(m_file).setup();
    const QString state = setup.colorado ? QStringLiteral("CO") : setup.stateCode;
    f.text(QStringLiteral("1a"), QStringLiteral("State where you paid state unemployment tax"),
           state.isEmpty() ? QStringLiteral("(enter the state in Payroll Setup)") : state);
    const bool reduced = !state.isEmpty() && t->futa().creditReduction.contains(state);
    f.text(QStringLiteral("2"), QStringLiteral("Wages paid in a credit reduction state"), reduced ? QStringLiteral("☒") : QStringLiteral("☐"));
    f.section(QStringLiteral("Part 2: Determine your FUTA tax before adjustments"));
    Money payments = usd(0);
    Money exempt = usd(0);
    QMap<qint64, Money> subjectByEmployee;
    QMap<qint64, Money> taxableByEmployee;
    for (const PaycheckRecord &r : list) {
        payments += r.taxes.gross + r.shareholderHealth;
        exempt += r.section125 + r.shareholderHealth;
        subjectByEmployee[r.employeeId] = subjectByEmployee.value(r.employeeId, usd(0)) + r.taxes.gross - r.section125;
        taxableByEmployee[r.employeeId] = taxableByEmployee.value(r.employeeId, usd(0)) + r.taxes.futaWages;
    }
    Money over = usd(0);
    for (auto it = subjectByEmployee.cbegin(); it != subjectByEmployee.cend(); ++it) {
        over += nonNegative(it.value() - taxableByEmployee.value(it.key()));
    }
    f.line(QStringLiteral("3"), QStringLiteral("Total payments to all employees"), payments);
    f.line(QStringLiteral("4"), QStringLiteral("Payments exempt from FUTA tax (4a fringe benefits: section 125, health plans)"), exempt);
    f.line(QStringLiteral("5"), QStringLiteral("Total of payments made to each employee in excess of %1").arg(usdText(t->futa().wageBase)),
           over);
    const Money l6 = exempt + over;
    const Money l7 = payments - l6;
    const Decimal net = rateMinus(t->futa().grossRate, t->futa().maxCredit);
    const Money l8 = percentOf(l7, net);
    f.line(QStringLiteral("6"), QStringLiteral("Subtotal (line 4 + line 5)"), l6);
    f.line(QStringLiteral("7"), QStringLiteral("Total taxable FUTA wages (line 3 − line 6)"), l7);
    f.line(QStringLiteral("8"), QStringLiteral("FUTA tax before adjustments (line 7 × %1)").arg(net.toString()), l8);
    f.section(QStringLiteral("Part 3: Determine your adjustments"));
    f.line(QStringLiteral("9"), QStringLiteral("All taxable FUTA wages excluded from state unemployment tax"), usd(0));
    f.line(QStringLiteral("10"), QStringLiteral("Some wages excluded, or state unemployment tax paid late"), usd(0));
    const Money l11 = reduced ? percentOf(l7, t->futa().creditReduction.value(state)) : usd(0);
    f.line(QStringLiteral("11"), QStringLiteral("Credit reduction (Schedule A)"), l11);
    f.section(QStringLiteral("Part 4: Determine your FUTA tax and balance due or overpayment"));
    const Money l12 = l8 + l11;
    const Money l13 = deposited(m_file, QStringLiteral("futa"), QDate(year, 1, 1), QDate(year, 12, 31));
    f.line(QStringLiteral("12"), QStringLiteral("Total FUTA tax after adjustments"), l12);
    f.line(QStringLiteral("13"), QStringLiteral("FUTA tax deposited for the year"), l13);
    f.line(QStringLiteral("14"), QStringLiteral("Balance due"), nonNegative(l12 - l13));
    f.line(QStringLiteral("15a"), QStringLiteral("Overpayment"), nonNegative(l13 - l12));
    f.section(QStringLiteral("Part 5: Report your FUTA tax liability by quarter"));
    if (l12 > t->futa().depositThreshold) {
        Money total = usd(0);
        for (int q = 1; q <= 4; ++q) {
            const Money quarterFuta = sum(checks(m_file, quarterStart(year, q), quarterEnd(year, q)), futa);
            total += quarterFuta;
            f.line(QStringLiteral("16%1").arg(QChar(u'a' + q - 1)), QStringLiteral("Quarter %1").arg(q), quarterFuta);
        }
        f.line(QStringLiteral("17"), QStringLiteral("Total tax liability for the year (must equal line 12)"), total);
    } else {
        f.text(QStringLiteral("16"), QStringLiteral("Line 12 is %1 or less: Part 5 is not required")
                                         .arg(usdText(t->futa().depositThreshold)),
               QString());
    }
    f.note(QStringLiteral("QindaBooks prepares the numbers; the business files Form 940 and pays through EFTPS."));
    return f.table();
}

} // namespace QindaOffice::Books
