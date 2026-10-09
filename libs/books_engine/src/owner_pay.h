// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Owner pay (M71, D-081; docs/books.md "Owner pay"): how the owner of a
// single-member LLC on Schedule C, or a partner, takes money out — draws —
// and records the personal estimated-tax payments the business pays for
// them; the set-aside moved to a tax savings account after a draw; and an S
// corporation's shareholder distributions.
//
// Each is an ordinary document, so registers, reconciliation, reports and
// check printing need nothing new, plus a row in `owner_payments` naming
// which it is:
//   draw / distribution  a Check to the owner (printed through the check
//                        queue, check_queue.h) or a Transfer, debiting
//                        Owner Draws / Shareholder Distributions (equity)
//   federal / Colorado   an Expense paid electronically (EFTPS, Revenue
//   estimated tax        Online), debiting Owner Draws: for a disregarded
//                        LLC these are the owner's own taxes, never a
//                        business expense — the memo says which tax,
//                        year and installment
//   set-aside            a Transfer from the bank to the tax savings account
//
// AGENT-NOTE: an S corporation's owner is paid wages through payroll
// (payroll_ledger.h); a draw is refused for one — distributions are its
// owner payments. A partnership's partners each have their own estimates;
// QindaBooks records them per owner.

#include "books_types.h"
#include "journal.h"
#include "money.h"

#include "estimated_tax.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class OwnerPaymentKind { Draw, FederalEstimate, ColoradoEstimate, SetAside, Distribution };
[[nodiscard]] QString ownerPaymentKindKey(OwnerPaymentKind kind);
[[nodiscard]] std::optional<OwnerPaymentKind> ownerPaymentKindFromKey(const QString &key);

struct OwnerPaymentRequest {
    OwnerPaymentKind kind = OwnerPaymentKind::Draw;
    qint64 ownerId = 0;          // a PartyKind::Owner party (required for a check)
    qint64 bankAccountId = 0;
    qint64 toAccountId = 0;      // set-aside: the tax savings account (0 = the setup's)
    Money amount;
    QDate date;
    enum class Method { Check, Transfer };
    Method method = Method::Transfer; // draws and distributions; estimates are electronic payments
    QString checkNumber;
    bool toPrint = false;        // a check waiting in the print queue
    int taxYear = 0;             // estimates: the tax year and installment (1–4)
    int installment = 0;
    QString reference;           // EFTPS / Revenue Online confirmation
    QString memo;
};

struct OwnerPaymentRecord {
    qint64 docId = 0;
    OwnerPaymentKind kind = OwnerPaymentKind::Draw;
    qint64 ownerId = 0;
    QString ownerName;
    DocKind docKind = DocKind::Transfer;
    QDate date;
    Money amount;
    QString number;
    qint64 bankAccountId = 0;
    qint64 toAccountId = 0;
    int taxYear = 0;
    int installment = 0;
    QString reference;
    QString memo;
    bool voided = false;
};

class OwnerPay {
public:
    explicit OwnerPay(CompanyFile &file);

    // Records one owner payment; Result.id = the document.
    Result record(const OwnerPaymentRequest &request, const PostOptions &options = {});
    Result voidPayment(qint64 docId, const QString &reason, const PostOptions &options = {});
    // Oldest first; `kind` narrows it.
    [[nodiscard]] QList<OwnerPaymentRecord> payments(const QDate &from = {}, const QDate &to = {},
                                                     std::optional<OwnerPaymentKind> kind = {},
                                                     bool includeVoid = false) const;
    // The year's live federal and Colorado estimated payments, as the
    // planner takes them.
    [[nodiscard]] QList<Payroll::EstimatedPayment> estimatedPayments(int taxYear) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
