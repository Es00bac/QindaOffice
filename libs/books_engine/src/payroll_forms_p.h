// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private (books_engine): shared pieces of the payroll form worksheets
// (payroll_forms_federal.cpp, payroll_forms_colorado.cpp) — a ReportTable
// laid out as the form's lines, and the period sums every form reads.

#include "money.h"
#include "payroll_ledger.h"
#include "report_table.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

namespace PayrollForms {

// Columns: Line · Description · Column 1 (a wage base, where the form has
// one) · Amount. Every value cell is money unless a line says otherwise.
class FormTable {
public:
    FormTable(CompanyFile &file, const QString &id, const QString &title, const QString &subtitle);
    void section(const QString &title);
    void line(const QString &number, const QString &text, const std::optional<Money> &amount,
              const std::optional<Money> &column1 = {});
    // A line whose value is text (a state, a count, a checkbox).
    void text(const QString &number, const QString &text, const QString &value);
    void note(const QString &text) { m_table.notes << text; }
    // Row keys are "line:<number>"; with a prefix (several W-2s in one
    // table) "<prefix>line:<number>".
    void setKeyPrefix(const QString &prefix) { m_prefix = prefix; }
    [[nodiscard]] ReportTable table() const { return m_table; }

private:
    ReportTable m_table;
    QString m_prefix;
};

// Live paychecks paid in [from, to].
[[nodiscard]] QList<PaycheckRecord> checks(CompanyFile &file, const QDate &from, const QDate &to);
[[nodiscard]] Money sum(const QList<PaycheckRecord> &checks, Money (*field)(const PaycheckRecord &));
// What was paid to the agency for `tax` for periods inside [from, to]
// (live payroll liability payments).
[[nodiscard]] Money deposited(CompanyFile &file, const QString &tax, const QDate &from, const QDate &to);
// Employer identity lines (name, address, EIN masked unless revealed).
[[nodiscard]] QString einText(CompanyFile &file, bool reveal);

} // namespace PayrollForms
} // namespace QindaOffice::Books
