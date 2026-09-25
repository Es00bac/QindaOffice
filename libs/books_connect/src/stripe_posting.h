// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// StripePosting: how one Stripe balance transaction enters the books
// (M48, D-048; docs/books.md "Stripe: the accounting model"). Pure over a
// CompanyFile — no network — so the model is tested with plain values.
//
// The Stripe balance is a bank-type **clearing account** ("Stripe (USD)")
// whose statement is Stripe's balance history. Every balance transaction
// becomes bank-feed lines on that account, FITID = the Stripe id, so the
// account's balance equals Stripe's available + pending balance and a
// second sync imports nothing:
//
//  | Stripe type                | Feed lines (sign = into the account) | Automatic posting                          |
//  |----------------------------|--------------------------------------|--------------------------------------------|
//  | charge, payment            | +gross (FITID txn_…), −fee (txn_…:fee) | fee → Merchant Fees; with invoice metadata of THIS company: a customer payment applied to the invoice, matched to the gross line |
//  | refund, payment_refund     | −amount (and a fee line if any)      | none: left for review                      |
//  | payout                     | −amount                              | Transfer clearing → payout account (reference po_…), matched; the bank's STRIPE deposit is matched to it (payout_matching.h) |
//  | stripe_fee, stripe_fx_fee, tax_fee, application_fee | −amount | → Merchant Fees                         |
//  | adjustment (disputes), payout_failure, everything else | ±amount (+ fee line) | none: left for review |
//
// Automatic postings follow the settings' autoPostFees / autoPostPayouts /
// autoApplyInvoices. A posting that the books refuse (a closed period, a
// voided invoice) leaves the line New and reports why; the import itself
// is never undone by it.
//
// AGENT-GUARD (idempotency): postings happen only for lines this call
// newly imported (the feed dedups on FITID), inside one SQL transaction
// with the import — a crash cannot leave a line imported but unposted.

#include "connect_settings.h"
#include "stripe_client.h"

namespace QindaOffice::Books::Connect {

class StripePosting {
public:
    StripePosting(CompanyFile &file, StripeSettings settings, QString companyUuid);

    // Imports and posts one balance transaction; adds to `report`.
    void post(const StripeBalanceTransaction &txn, SyncReport *report);

    // Metadata keys a payment carries back to its invoice (payment links
    // set them; payment_link.cpp).
    static inline const QString kInvoiceKey = QStringLiteral("qindabooks_invoice");
    static inline const QString kCompanyKey = QStringLiteral("qindabooks_company");
    static inline const QString kNumberKey = QStringLiteral("invoice_number");

private:
    bool applyToInvoice(const StripeBalanceTransaction &txn, qint64 feedId, const QDate &date, SyncReport *report);
    void bookPayout(const StripeBalanceTransaction &txn, qint64 feedId, const QDate &date, SyncReport *report);
    void bookFee(qint64 feedId, const QString &memo, SyncReport *report);
    [[nodiscard]] qint64 lineOf(qint64 docId, qint64 accountId) const;

    CompanyFile &m_file;
    StripeSettings m_settings;
    QString m_companyUuid;
};

// The local calendar date of a Stripe unix timestamp.
[[nodiscard]] QDate stripeDate(qint64 unixSeconds);

} // namespace QindaOffice::Books::Connect
