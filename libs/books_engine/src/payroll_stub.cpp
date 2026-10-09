// SPDX-License-Identifier: GPL-3.0-or-later
// A paycheck's pay stub lines (payroll_stub.h).
#include "payroll_stub.h"

#include "company_file.h"
#include "payroll_items.h"
#include "payroll_ledger.h"
#include "payroll_records.h"

namespace QindaOffice::Books {

using namespace Payroll;

QList<StubLine> payStubLines(CompanyFile &file, const PaycheckRecord &r)
{
    QList<StubLine> out;
    const Money zero = file.money(0);
    // The year to date through this check: the paychecks before it, plus it.
    const PayrollYearToDate y = PayrollLedger(file).yearToDate(r.employeeId, r.payDate, r.docId);
    const QMap<qint64, PayrollItemToDate> items = PayrollItems(file).yearToDate(r.employeeId, r.payDate, r.docId);
    const PayrollSetup setup = PayrollRecords(file).setup();
    const EmployeePayroll e = PayrollRecords(file).employee(r.employeeId);
    const PaycheckTaxes &t = r.taxes;
    const auto add = [&](StubLine::Section section, const QString &label, const Money &now, const Money &before,
                         bool always) {
        if (always || !now.isZero() || !before.isZero()) {
            out << StubLine{section, label, now, now + before};
        }
    };
    using S = StubLine::Section;
    add(S::Earnings, QStringLiteral("Gross pay"), t.gross, y.gross, true);
    add(S::Earnings, QStringLiteral("Health insurance (before tax)"), r.section125, y.section125, false);
    add(S::Earnings, QStringLiteral("Retirement plan (before tax)"), r.retirement, y.retirement, false);
    add(S::Taxes, QStringLiteral("Federal income tax"), t.federalIncomeTax, y.federalIncomeTax, true);
    add(S::Taxes, QStringLiteral("Social security"), t.socialSecurity, y.socialSecurity, true);
    add(S::Taxes, QStringLiteral("Medicare"), t.medicare, y.medicare, true);
    add(S::Taxes, QStringLiteral("Additional Medicare"), t.additionalMedicare, y.additionalMedicare, false);
    add(S::Taxes, QStringLiteral("Colorado income tax"), t.coloradoIncomeTax, y.coloradoIncomeTax, setup.colorado);
    add(S::Taxes, QStringLiteral("Colorado FAMLI"), t.famliEmployee, y.famliEmployee, setup.colorado);
    add(S::Taxes, QStringLiteral("Local OPT"), t.optEmployee, y.optEmployee, false);
    for (const CustomItemResult &c : t.custom) {
        if (c.side == CustomItem::Side::Employee) {
            add(S::Taxes, c.name, c.amount, items.value(c.id, PayrollItemToDate{zero, zero}).amount, true);
        }
    }
    add(S::AfterTax, e.deductions.afterTaxLabel.isEmpty() ? QStringLiteral("Deduction after tax") : e.deductions.afterTaxLabel,
        r.afterTax, y.afterTax, false);
    add(S::Net, QStringLiteral("Net pay"), t.netPay, y.net, true);
    // The employer's side: this check only.
    add(S::Employer, QStringLiteral("Social security"), t.socialSecurityEmployer, zero, true);
    add(S::Employer, QStringLiteral("Medicare"), t.medicareEmployer, zero, true);
    add(S::Employer, QStringLiteral("Federal unemployment (FUTA)"), t.futa, zero, true);
    add(S::Employer, QStringLiteral("Colorado unemployment (UI)"), t.coloradoUi, zero, setup.colorado);
    add(S::Employer, QStringLiteral("Colorado FAMLI"), t.famliEmployer, zero, false);
    add(S::Employer, QStringLiteral("Local OPT"), t.optEmployer, zero, false);
    for (const CustomItemResult &c : t.custom) {
        if (c.side == CustomItem::Side::Employer) {
            add(S::Employer, c.name, c.amount, zero, true);
        }
    }
    return out;
}

} // namespace QindaOffice::Books
