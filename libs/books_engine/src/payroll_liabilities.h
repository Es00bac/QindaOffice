// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Payroll liabilities (M71, D-081; docs/books.md "Pay Liabilities"): what
// the live paychecks put on Payroll Liabilities, grouped the way each
// agency collects it, by deposit or filing period, with its due date — less
// what was paid for that group and period. Paying records a
// DocKind::PayrollLiabilityPayment (Dr the liability accounts, Cr the bank)
// and its `payroll_payments` row (group and period); voiding it puts the
// balance back.
//
// Groups: "federal941" (withheld income tax, social security and Medicare —
// one EFTPS deposit), "futa", for a business that chose Colorado
// "coWithholding" (DR 1094), "coUi", "coFamli" and "opt:<city>", each of
// the company's own payroll tax items "custom:<id>" (its name, agency and
// due schedule, payroll_items.h), and the deductions "section125",
// "retirement", "afterTax" (paid as the insurer or plan bills: no due
// date).
//
// AGENT-NOTE (rounding): a payment may differ from the balance by up to
// $1.00 — the agency's own form computes on the period's totals (Form 941
// line 7, CDLE per rate part) while each paycheck rounded its own share —
// and the difference posts to Payroll Taxes as a rounding adjustment, so
// the liability clears to zero. A larger overpayment is refused; a smaller
// payment is a partial payment, shared across the accounts in proportion.

#include "books_types.h"
#include "journal.h"
#include "money.h"

#include "payroll_calendar.h"

#include <QDate>
#include <QList>
#include <QString>

namespace QindaOffice::Books {

class CompanyFile;

struct LiabilityPart {
    QString role;      // PayrollRole::*
    qint64 accountId = 0;
    Money accrued;
    Money paid;
    Money balance;
};

struct LiabilityDue {
    QString tax;       // the group key ("federal941", "opt:denver", …)
    QString label;     // "Federal: withholding, social security, Medicare (Form 941)"
    QString agency;    // where it is paid ("EFTPS", "Colorado Revenue Online", …)
    QDate start;
    QDate end;
    QDate due;         // invalid for deductions (paid as billed)
    QString period;    // "September 2026", "Q3 2026"
    Money accrued;
    Money paid;
    Money balance;
    QList<LiabilityPart> parts;
    QStringList notes; // the $100,000 day, the $2,500 quarter, the FUTA $500 carry
};

struct LiabilityPaymentRequest {
    QString tax;
    QDate start;
    QDate end;
    qint64 bankAccountId = 0;   // 0 = the payroll bank account
    QDate date;
    std::optional<Money> amount; // none = the balance
    QString reference;          // EFTPS / Revenue Online confirmation number
    QString memo;
};

struct LiabilityPaymentRecord {
    qint64 docId = 0;
    QString tax;
    QDate start;
    QDate end;
    QDate date;
    Money amount;
    Money adjustment;           // the rounding difference posted to Payroll Taxes
    QString reference;
    bool voided = false;
};

class PayrollLiabilities {
public:
    explicit PayrollLiabilities(CompanyFile &file);

    // Every group and period with paychecks paid between the dates (or
    // payments for it), oldest due first; with `openOnly`, only those with
    // a balance.
    [[nodiscard]] QList<LiabilityDue> liabilities(const QDate &from, const QDate &to, bool openOnly = false) const;
    // One group and period (balance as of now).
    [[nodiscard]] std::optional<LiabilityDue> liability(const QString &tax, const QDate &start, const QDate &end) const;
    Result pay(const LiabilityPaymentRequest &request, const PostOptions &options = {});
    Result voidPayment(qint64 docId, const QString &reason, const PostOptions &options = {});
    [[nodiscard]] QList<LiabilityPaymentRecord> payments(const QDate &from = {}, const QDate &to = {},
                                                         bool includeVoid = false) const;
    // The plain names of a built-in group (label, agency).
    [[nodiscard]] static QString groupLabel(const QString &tax);
    [[nodiscard]] static QString groupAgency(const QString &tax);
    // Any group's label, the company's own payroll tax items ("custom:<id>",
    // payroll_items.h) included.
    [[nodiscard]] QString labelOf(const QString &tax) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
