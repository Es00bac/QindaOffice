// SPDX-License-Identifier: GPL-3.0-or-later
// W-2 and W-3 worksheets (payroll_reports.h), 2026 boxes. The SSN and EIN
// show in full only with `revealIds` (the forms that are filed); every
// other view masks them.
#include "payroll_forms_p.h"

#include "company_file.h"
#include "payroll_records.h"
#include "payroll_reports.h"

#include <QMap>
#include <QSet>

namespace QindaOffice::Books {

using namespace Payroll;
using namespace PayrollForms;

namespace {

// One employee's year, box by box.
struct W2Boxes {
    Money wages = usd(0);          // 1
    Money federalTax = usd(0);     // 2
    Money ssWages = usd(0);        // 3
    Money ssTax = usd(0);          // 4
    Money medicareWages = usd(0);  // 5
    Money medicareTax = usd(0);    // 6 (Additional Medicare included)
    Money code401k = usd(0);       // 12 D
    Money codeSimple = usd(0);     // 12 S
    Money shareholderHealth = usd(0); // 14 (informational)
    Money opt = usd(0);            // 14 (informational)
    Money stateWages = usd(0);     // 16
    Money stateTax = usd(0);       // 17
};

W2Boxes boxesFor(const QList<PaycheckRecord> &list)
{
    W2Boxes b;
    for (const PaycheckRecord &r : list) {
        b.wages += r.taxes.federalWages;
        b.federalTax += r.taxes.federalIncomeTax;
        b.ssWages += r.taxes.socialSecurityWages;
        b.ssTax += r.taxes.socialSecurity;
        b.medicareWages += r.taxes.medicareWages;
        b.medicareTax += r.taxes.medicare + r.taxes.additionalMedicare;
        (r.retirementKind == PayDeductions::Retirement::SimpleIra ? b.codeSimple : b.code401k) += r.retirement;
        b.shareholderHealth += r.shareholderHealth;
        b.opt += r.taxes.optEmployee;
        b.stateWages += r.taxes.coloradoWages;
        b.stateTax += r.taxes.coloradoIncomeTax;
    }
    return b;
}

// Boxes 15–17 carry Colorado's figures, for a business that chose
// Colorado; another state's are the company's own payroll tax items, not
// on this worksheet (`stateId` empty = no state boxes).
void addBoxes(FormTable &f, const W2Boxes &b, const std::optional<QString> &stateId)
{
    f.line(QStringLiteral("1"), QStringLiteral("Wages, tips, other compensation"), b.wages);
    f.line(QStringLiteral("2"), QStringLiteral("Federal income tax withheld"), b.federalTax);
    f.line(QStringLiteral("3"), QStringLiteral("Social security wages"), b.ssWages);
    f.line(QStringLiteral("4"), QStringLiteral("Social security tax withheld"), b.ssTax);
    f.line(QStringLiteral("5"), QStringLiteral("Medicare wages and tips"), b.medicareWages);
    f.line(QStringLiteral("6"), QStringLiteral("Medicare tax withheld"), b.medicareTax);
    if (b.code401k.isPositive()) {
        f.line(QStringLiteral("12 D"), QStringLiteral("Elective deferrals to a 401(k)"), b.code401k);
    }
    if (b.codeSimple.isPositive()) {
        f.line(QStringLiteral("12 S"), QStringLiteral("Salary reduction contributions to a SIMPLE plan"), b.codeSimple);
    }
    f.text(QStringLiteral("13"), QStringLiteral("Retirement plan"),
           (b.code401k + b.codeSimple).isPositive() ? QStringLiteral("☒") : QStringLiteral("☐"));
    if (b.shareholderHealth.isPositive()) {
        f.line(QStringLiteral("14a"), QStringLiteral("Other: S-corp health insurance (included in box 1)"), b.shareholderHealth);
    }
    if (b.opt.isPositive()) {
        f.line(QStringLiteral("14a"), QStringLiteral("Other: local Occupational Privilege Tax"), b.opt);
    }
    if (!stateId) {
        return;
    }
    f.text(QStringLiteral("15"), QStringLiteral("State · Employer's state ID number"),
           QStringLiteral("CO · ") + (stateId->isEmpty() ? QStringLiteral("(enter the Colorado account number)") : *stateId));
    f.line(QStringLiteral("16"), QStringLiteral("State wages, tips, etc."), b.stateWages);
    f.line(QStringLiteral("17"), QStringLiteral("State income tax"), b.stateTax);
}

std::optional<QString> coloradoStateId(CompanyFile &file)
{
    const PayrollSetup s = PayrollRecords(file).setup();
    return s.colorado ? std::optional<QString>(s.coloradoAccount) : std::nullopt;
}

const QString kOtherStates = QStringLiteral("State boxes 15–17 are filled for Colorado only. Another state's wages and "
                                            "user-entered withholding are in the Employee Earnings Summary.");

QString employerBlock(CompanyFile &file)
{
    const CompanySettings s = file.settings();
    return (s.legalName + QLatin1Char('\n') + s.address).trimmed().replace(QLatin1Char('\n'), QStringLiteral(", "));
}

} // namespace

ReportTable PayrollReports::formW2(int year, qint64 employeeId, bool revealIds) const
{
    PayrollRecords records(m_file);
    QMap<qint64, QList<PaycheckRecord>> byEmployee;
    for (const PaycheckRecord &r : checks(m_file, QDate(year, 1, 1), QDate(year, 12, 31))) {
        if (employeeId == 0 || r.employeeId == employeeId) {
            byEmployee[r.employeeId] << r;
        }
    }
    const QString title = employeeId != 0 ? QStringLiteral("Form W-2 worksheet: %1").arg(records.employee(employeeId).fullName())
                                          : QStringLiteral("Form W-2 worksheets");
    FormTable f(m_file, QStringLiteral("form_w2"), title, QStringLiteral("Wage and Tax Statement %1").arg(year));
    const std::optional<QString> stateId = coloradoStateId(m_file);
    if (employeeId != 0 && !byEmployee.contains(employeeId)) {
        byEmployee.insert(employeeId, {});
    }
    for (auto it = byEmployee.cbegin(); it != byEmployee.cend(); ++it) {
        const EmployeePayroll e = records.employee(it.key());
        if (employeeId == 0) {
            f.setKeyPrefix(QStringLiteral("employee:%1:").arg(it.key()));
            f.section(e.fullName());
        }
        const QString ssn = normalizedTaxId(e.ssn, 9);
        f.text(QStringLiteral("a"), QStringLiteral("Employee's social security number"),
               ssn.isEmpty() ? QStringLiteral("(missing)")
                             : (revealIds ? ssn.left(3) + QLatin1Char('-') + ssn.mid(3, 2) + QLatin1Char('-') + ssn.right(4)
                                          : maskedSsn(ssn)));
        f.text(QStringLiteral("b"), QStringLiteral("Employer identification number (EIN)"), einText(m_file, revealIds));
        f.text(QStringLiteral("c"), QStringLiteral("Employer's name, address, and ZIP code"), employerBlock(m_file));
        f.text(QStringLiteral("e"), QStringLiteral("Employee's name"), e.fullName());
        f.text(QStringLiteral("f"), QStringLiteral("Employee's address and ZIP code"),
               QStringLiteral("%1, %2, %3 %4").arg(e.street, e.city, e.state, e.zip));
        addBoxes(f, boxesFor(it.value()), stateId);
    }
    f.note(QStringLiteral("Box 12 code DD (health coverage) is optional for employers filing fewer than 250 W-2s and "
                          "is not shown."));
    if (!stateId) {
        f.note(kOtherStates);
    }
    return f.table();
}

ReportTable PayrollReports::formW3(int year, bool revealIds) const
{
    FormTable f(m_file, QStringLiteral("form_w3"), QStringLiteral("Form W-3 worksheet"),
                QStringLiteral("Transmittal of Wage and Tax Statements %1").arg(year));
    const QList<PaycheckRecord> list = checks(m_file, QDate(year, 1, 1), QDate(year, 12, 31));
    QSet<qint64> employees;
    for (const PaycheckRecord &r : list) {
        employees.insert(r.employeeId);
    }
    f.text(QStringLiteral("b"), QStringLiteral("Kind of Payer · Kind of Employer"), QStringLiteral("941 · None apply"));
    f.text(QStringLiteral("c"), QStringLiteral("Total number of Forms W-2"), QString::number(employees.size()));
    f.text(QStringLiteral("e"), QStringLiteral("Employer identification number (EIN)"), einText(m_file, revealIds));
    f.text(QStringLiteral("f–g"), QStringLiteral("Employer's name and address"), employerBlock(m_file));
    const W2Boxes b = boxesFor(list);
    const std::optional<QString> stateId = coloradoStateId(m_file);
    addBoxes(f, b, stateId);
    f.line(QStringLiteral("12a"), QStringLiteral("Deferred compensation (codes D and S)"), b.code401k + b.codeSimple);
    f.note(QStringLiteral("File Copy A of each W-2 with the W-3 through the SSA's Business Services Online by February 1, "
                          "%1 (January 31 is a Sunday)%2.")
               .arg(year + 1)
               .arg(stateId ? QStringLiteral("; Colorado W-2s through Revenue Online with the DR 1093") : QString()));
    if (!stateId) {
        f.note(kOtherStates);
    }
    return f.table();
}

} // namespace QindaOffice::Books
