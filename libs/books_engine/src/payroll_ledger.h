// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Paychecks in the ledger (M71, D-081; docs/books.md "Payroll"): one
// paycheck = one DocKind::Paycheck document + its row in `paychecks` + one
// balanced journal transaction:
//
//   Dr Wages (Officer Wages for an owner-employee)  gross pay
//   Dr Payroll Taxes                                 the employer's taxes
//   Cr Payroll Liabilities: Federal Withholding, Social Security
//      (employee + employer), Medicare (both + Additional), FUTA,
//      Colorado Withholding, Colorado UI, Colorado FAMLI (both shares),
//      Local OPT (both), Health Insurance / Retirement Plan / Other
//      deductions                                   what is owed to others
//   Cr the bank account                              net pay
//
// AGENT-GUARD (numbers): every amount comes from books_payroll's
// computePaycheck over the employee's record, the company setup and the
// year to date of LIVE (not void) paychecks — the ledger never takes a tax
// amount from the caller. A paycheck's row never changes (SQL triggers);
// Void zeroes it (Ledger::voidDocument) and the year to date forgets it;
// Adjust (replacePaycheck) voids it and writes a corrected one in the same
// SQL transaction, the new row naming the old (replaces_doc_id).
//
// AGENT-CONTRACT (check printing, check_queue.h): a paycheck drawn on a
// bank account may wait in the print queue (`toPrint`) and is numbered
// when it prints, like any check; books_io prints the pay stub on both
// voucher stubs (check_print.cpp).

#include "books_types.h"
#include "journal.h"
#include "money.h"
#include "payroll_records.h"

#include "paycheck_calculator.h"
#include "payroll_tables.h"

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

struct PaycheckRequest {
    qint64 employeeId = 0;
    QDate payDate;
    QDate periodStart;
    QDate periodEnd;
    std::optional<Decimal> hours;     // hourly employees; none = their usual hours
    std::optional<Money> regular;     // overrides the pay from the employee's rate
    Money bonus;                      // supplemental wages (flat federal rate)
    qint64 bankAccountId = 0;         // 0 = the payroll bank account in the setup
    QString checkNumber;              // "" with toPrint, or for direct deposit
    bool toPrint = false;             // wait in the check print queue
    QString memo;
};

// A paycheck as stored: the request's facts and every computed amount.
struct PaycheckRecord {
    qint64 docId = 0;
    qint64 employeeId = 0;
    QString employeeName;
    QDate payDate;
    QDate periodStart;
    QDate periodEnd;
    int taxYear = 0;
    Payroll::PayFrequency frequency = Payroll::PayFrequency::Biweekly;
    QString hours;
    QString number;
    qint64 bankAccountId = 0;
    bool voided = false;
    qint64 replacesDocId = 0;
    QString optCity;
    PayDeductions::Retirement retirementKind = PayDeductions::Retirement::None;
    Money regular;
    Money supplemental;
    Money section125;
    Money retirement;
    Money shareholderHealth;
    Money afterTax;
    Payroll::PaycheckTaxes taxes;     // gross, wage bases, every tax, net
    QJsonObject details;              // worksheets and notes as computed
};

// The same employee's paychecks earlier in the calendar year, summed.
struct PayrollYearToDate {
    Payroll::YearToDate ytd;          // the wage bases computePaycheck needs
    Money gross;
    Money federalWages;
    Money federalIncomeTax;
    Money socialSecurity;
    Money medicare;
    Money additionalMedicare;
    Money coloradoIncomeTax;
    Money famliEmployee;
    Money optEmployee;
    Money section125;
    Money retirement;
    Money afterTax;
    Money net;
};

class PayrollLedger {
public:
    explicit PayrollLedger(CompanyFile &file);

    // The year's tables, or nullopt with the plain "no tables" sentence.
    [[nodiscard]] static std::optional<Payroll::PayrollTables> tables(int year, QString *error = nullptr);

    // Everything a paycheck would be, posting nothing (Run Payroll's
    // preview). `ok` false with `error` when it cannot be computed.
    [[nodiscard]] PaycheckRecord preview(const PaycheckRequest &request, QString *error = nullptr) const;
    // Computes and posts one paycheck; Result.id = the document.
    Result createPaycheck(const PaycheckRequest &request, const PostOptions &options = {});
    // Several at once in ONE SQL transaction (all or nothing); the same
    // employee twice is computed in order, each seeing the one before.
    Result createPaychecks(const QList<PaycheckRequest> &requests, QList<qint64> *docIds = nullptr,
                           const PostOptions &options = {});
    // Voids the paycheck (its posting and its year to date). Audited.
    Result voidPaycheck(qint64 docId, const QString &reason, const PostOptions &options = {});
    // Adjust: voids the paycheck and writes the corrected one in one SQL
    // transaction. Result.id = the new document.
    Result replacePaycheck(qint64 docId, const PaycheckRequest &corrected, const QString &reason,
                           const PostOptions &options = {});

    [[nodiscard]] std::optional<PaycheckRecord> paycheck(qint64 docId) const;
    // Paychecks paid between the dates (inclusive; invalid = open), oldest
    // first; void ones only when asked.
    [[nodiscard]] QList<PaycheckRecord> paychecks(const QDate &from = {}, const QDate &to = {}, qint64 employeeId = 0,
                                                  bool includeVoid = false) const;
    // Live paychecks of the employee in `payDate`'s calendar year paid
    // before it (same day: those with a smaller document id), and the
    // month to date for the local OPT.
    [[nodiscard]] PayrollYearToDate yearToDate(qint64 employeeId, const QDate &payDate,
                                               qint64 beforeDocId = 0) const;

    // The employer's running totals before a paycheck (every employee):
    // the calendar quarter's social security, Medicare and UI, the year's
    // FUTA (paycheck_calculator.h EmployerToDate).
    [[nodiscard]] Payroll::EmployerToDate employerToDate(const QDate &payDate, qint64 beforeDocId = 0) const;

private:
    // Computes against the stored records; `beforeDocId` bounds the year
    // to date (0 = every live paycheck dated on or before the pay date).
    [[nodiscard]] PaycheckRecord compute(const PaycheckRequest &request, qint64 beforeDocId, QString *error) const;
    // Writes the document, its paychecks row and the posting. Caller holds
    // the SqlTransaction.
    Result post(PaycheckRecord &record, const PaycheckRequest &request, const PostOptions &options);
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
