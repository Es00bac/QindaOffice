// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sales: estimates, invoices, sales receipts, received payments, credit
// memos, refunds and customer statements (docs/books.md "Sales").
//
// Postings (all lines carry the customer as party):
//   Invoice        Dr A/R total · Cr income per line · Dr Discounts Given ·
//                  Cr Sales Tax Payable per component
//   Sales receipt  as an invoice, but Dr the deposit account (Undeposited
//                  Funds unless another is named) instead of A/R
//   Credit memo    the mirror image of an invoice (Cr A/R)
//   Payment        Dr deposit account · Cr A/R; applications link it to the
//                  invoices it pays; any remainder is customer credit
//   Refund         Dr A/R · Cr bank; consumes a credit (memo or unapplied
//                  payment) through an application
// Estimates never post. Drafts never post until postDraft().

#include "books_types.h"
#include "documents.h"
#include "journal.h"
#include "money.h"

#include <QDate>
#include <QList>

namespace QindaOffice::Books {

class CompanyFile;

struct PaymentApplication {
    qint64 docId = 0; // invoice (or bill, for Purchases)
    Money amount;
};

struct ReceivePaymentInput {
    qint64 customerId = 0;
    QDate date;
    Money amount;
    qint64 depositAccountId = 0; // 0 = Undeposited Funds
    QString reference;           // cheque number, transfer id
    QString memo;
    QList<PaymentApplication> applications;
};

struct RefundInput {
    qint64 customerId = 0;
    qint64 creditDocId = 0;   // credit memo or payment with unapplied credit
    qint64 bankAccountId = 0; // the account the money leaves
    Money amount;
    QDate date;
    QString memo;
};

struct StatementLine {
    QDate date;
    qint64 docId = 0;
    DocKind kind = DocKind::Invoice;
    QString number;
    QString description;
    Money amount;  // + charges (invoices, refunds), − payments and credits
    Money balance; // running
};

struct AgingBuckets {
    Money current;
    Money days1to30;
    Money days31to60;
    Money days61to90;
    Money over90;
    [[nodiscard]] Money total() const { return current + days1to30 + days31to60 + days61to90 + over90; }
};

struct CustomerStatement {
    qint64 customerId = 0;
    QDate from;
    QDate to;
    Money opening;
    QList<StatementLine> lines;
    Money closing;
    AgingBuckets aging; // of the open documents as of `to`
};

class Sales {
public:
    explicit Sales(CompanyFile &file);

    // ---- estimates ----
    // Creates (id 0) or rewrites an estimate. kind is forced to Estimate.
    Result saveEstimate(BusinessDocument estimate);
    // Accepted estimates are closed by conversion; rejected ones are voided
    // through Ledger::voidDocument (nothing posted, audit kept).
    Result convertEstimateToInvoice(qint64 estimateId, const QDate &invoiceDate,
                                    const PostOptions &options = {});

    // ---- invoices, receipts, credit memos ----
    // state Open posts immediately; Draft is stored without posting.
    Result createInvoice(BusinessDocument invoice, const PostOptions &options = {});
    // Void + repost in one SQL transaction; the id stays. Refused when the
    // new total is below what is already applied to it.
    Result updateInvoice(BusinessDocument invoice, const QString &reason,
                         const PostOptions &options = {});
    Result postDraft(qint64 docId, const PostOptions &options = {});
    Result createSalesReceipt(BusinessDocument receipt, const PostOptions &options = {});
    Result createCreditMemo(BusinessDocument memo, const PostOptions &options = {});

    // ---- money in / out ----
    Result receivePayment(const ReceivePaymentInput &input, const PostOptions &options = {});
    // Applies an existing credit (payment remainder or credit memo) to an
    // invoice of the same customer. No new posting: A/R already moved.
    Result applyCredit(qint64 sourceDocId, qint64 invoiceId, const Money &amount,
                       const QDate &date);
    Result refund(const RefundInput &input, const PostOptions &options = {});

    // ---- queries ----
    [[nodiscard]] QList<BusinessDocument> openInvoices(qint64 customerId = 0,
                                                       const QDate &asOf = {}) const;
    // Credits (payments and credit memos) with an unapplied remainder.
    [[nodiscard]] QList<BusinessDocument> openCredits(qint64 customerId = 0) const;
    // A/R balance of one customer (invoices + refunds − payments − credits).
    [[nodiscard]] Money customerBalance(qint64 customerId, const QDate &asOf = {}) const;
    [[nodiscard]] CustomerStatement statement(qint64 customerId, const QDate &from,
                                              const QDate &to) const;

private:
    Result postSalesDocument(BusinessDocument &doc, const PostOptions &options, bool isNew,
                             const QString &reason);
    Result validateSalesDocument(BusinessDocument &doc) const;
    CompanyFile &m_file;
};

// Buckets documents by days past due as of `asOf` (current = not yet due;
// 1–30 includes exactly 30 days late). Shared by statements and the aging
// reports so they cannot disagree.
[[nodiscard]] int agingBucket(const QDate &dueDate, const QDate &asOf);

} // namespace QindaOffice::Books
