// SPDX-License-Identifier: GPL-3.0-or-later
// Paychecks: the employee's year to date and the employer's running totals
// that each new paycheck is computed against (payroll_ledger.h).
#include "payroll_ledger.h"

#include "company_file.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

using namespace Payroll;

PayrollYearToDate PayrollLedger::yearToDate(qint64 employeeId, const QDate &payDate, qint64 beforeDocId) const
{
    PayrollYearToDate y;
    const QString cur = m_file.currency();
    const QString live = QStringLiteral(" FROM paychecks p JOIN documents d ON d.id = p.doc_id WHERE d.state <> %1"
                                        " AND p.employee_id = ? AND p.tax_year = ? AND (p.pay_date < ? OR (p.pay_date = ?"
                                        " AND (? = 0 OR p.doc_id < ?)))")
                             .arg(int(DocState::Void));
    const QString day = Sql::isoDate(payDate);
    QSqlQuery q(m_file.database());
    if (Sql::run(q,
                 QStringLiteral("SELECT SUM(ss_wages), SUM(medicare_wages), SUM(futa_wages), SUM(ui_wages),"
                                " SUM(famli_wages), SUM(supplemental), SUM(retirement), SUM(gross), SUM(federal_wages),"
                                " SUM(fit), SUM(ss), SUM(medicare), SUM(addl_medicare), SUM(co_wh), SUM(famli_ee),"
                                " SUM(opt_ee), SUM(section125), SUM(after_tax), SUM(net)")
                     + live,
                 {employeeId, payDate.year(), day, day, beforeDocId, beforeDocId})
        && q.next()) {
        const auto money = [&](int i) { return Money(q.value(i).toLongLong(), cur); };
        y.ytd.socialSecurityWages = money(0);
        y.ytd.medicareWages = money(1);
        y.ytd.futaWages = money(2);
        y.ytd.uiWages = money(3);
        y.ytd.famliWages = money(4);
        y.ytd.supplemental = money(5);
        y.ytd.retirement = money(6);
        y.gross = money(7);
        y.federalWages = money(8);
        y.federalIncomeTax = money(9);
        y.socialSecurity = money(10);
        y.medicare = money(11);
        y.additionalMedicare = money(12);
        y.coloradoIncomeTax = money(13);
        y.famliEmployee = money(14);
        y.optEmployee = money(15);
        y.section125 = money(16);
        y.retirement = money(6);
        y.afterTax = money(17);
        y.net = money(18);
    }
    q.finish();
    // The calendar month so far, for the local OPT.
    const QString monthStart = Sql::isoDate(QDate(payDate.year(), payDate.month(), 1));
    if (Sql::run(q,
                 QStringLiteral("SELECT SUM(gross), MAX(opt_ee + opt_er)") + live + QStringLiteral(" AND p.pay_date >= ?"),
                 {employeeId, payDate.year(), day, day, beforeDocId, beforeDocId, monthStart})
        && q.next()) {
        y.ytd.monthWages = Money(q.value(0).toLongLong(), cur);
        y.ytd.optChargedThisMonth = q.value(1).toLongLong() > 0;
    }
    return y;
}

EmployerToDate PayrollLedger::employerToDate(const QDate &payDate, qint64 beforeDocId) const
{
    EmployerToDate e;
    const QString cur = m_file.currency();
    const QString day = Sql::isoDate(payDate);
    const QString live = QStringLiteral(" FROM paychecks p JOIN documents d ON d.id = p.doc_id WHERE d.state <> %1"
                                        " AND p.pay_date >= ? AND (p.pay_date < ? OR (p.pay_date = ? AND (? = 0 OR p.doc_id < ?)))")
                             .arg(int(DocState::Void));
    QSqlQuery q(m_file.database());
    const QDate quarter(payDate.year(), (quarterOf(payDate) - 1) * 3 + 1, 1);
    if (Sql::run(q, QStringLiteral("SELECT SUM(ss_wages), SUM(ss_er), SUM(medicare_wages), SUM(medicare_er), SUM(ui_wages),"
                                   " SUM(co_ui)") + live,
                 {Sql::isoDate(quarter), day, day, beforeDocId, beforeDocId})
        && q.next()) {
        e.quarterSocialSecurityWages = Money(q.value(0).toLongLong(), cur);
        e.quarterSocialSecurityEmployer = Money(q.value(1).toLongLong(), cur);
        e.quarterMedicareWages = Money(q.value(2).toLongLong(), cur);
        e.quarterMedicareEmployer = Money(q.value(3).toLongLong(), cur);
        e.quarterUiWages = Money(q.value(4).toLongLong(), cur);
        e.quarterUi = Money(q.value(5).toLongLong(), cur);
    }
    q.finish();
    if (Sql::run(q, QStringLiteral("SELECT SUM(futa_wages), SUM(futa)") + live,
                 {Sql::isoDate(QDate(payDate.year(), 1, 1)), day, day, beforeDocId, beforeDocId})
        && q.next()) {
        e.yearFutaWages = Money(q.value(0).toLongLong(), cur);
        e.yearFuta = Money(q.value(1).toLongLong(), cur);
    }
    return e;
}

} // namespace QindaOffice::Books
