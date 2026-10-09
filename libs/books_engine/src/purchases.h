// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Purchases: bills, bill payments (several bills at once), expenses /
// checks / credit-card charges, vendor credits (docs/books.md "Purchases").
//
// Postings (all lines carry the vendor as party when there is one):
//   Bill             Cr A/P total · Dr each line's expense/asset account
//   Bill payment     Dr A/P · Cr bank or credit card; applications to bills
//   Expense/Check/CC Cr the payment account · Dr each line's account
//   Vendor credit    Dr A/P · Cr each line's account
// Purchases carry no sales tax and no document discount (tax paid on
// purchases is part of the expense line); the fields are ignored.
//
// M68 (D-077): a bill line may receive a purchase-order line (poLineId,
// checked by PurchaseOrders::checkReceipts); a check, or a bill payment
// from a bank account, may be saved "to print" (toPrint): it posts without
// a number and waits in the check queue (check_queue.h).

#include "books_types.h"
#include "documents.h"
#include "journal.h"
#include "money.h"
#include "sales.h"

#include <QDate>
#include <QList>

namespace QindaOffice::Books {

class CompanyFile;

class Purchases {
public:
    explicit Purchases(CompanyFile &file);

    Result createBill(BusinessDocument bill, const PostOptions &options = {});
    Result updateBill(BusinessDocument bill, const QString &reason, const PostOptions &options = {});
    // One BillPayment document per vendor among `payments`, all dated
    // `date`, paid from `paymentAccountId` (bank or credit card). The ids of
    // the created payments go to `paymentIds`; Result.id is the first.
    // `toPrint` (M68): the payments wait in the check queue (a bank
    // account only; the reference is then left empty — printing numbers them).
    Result payBills(const QDate &date, qint64 paymentAccountId, const QList<PaymentApplication> &payments,
                    const QString &reference = {}, const PostOptions &options = {},
                    QList<qint64> *paymentIds = nullptr, bool toPrint = false);
    // kind: Expense, Check or CreditCardCharge; accountId = payment account.
    Result createExpense(BusinessDocument expense, const PostOptions &options = {});
    Result createVendorCredit(BusinessDocument credit, const PostOptions &options = {});
    Result applyVendorCredit(qint64 creditId, qint64 billId, const Money &amount, const QDate &date);

    [[nodiscard]] QList<BusinessDocument> unpaidBills(qint64 vendorId = 0, const QDate &asOf = {}) const;
    // A/P balance owed to one vendor.
    [[nodiscard]] Money vendorBalance(qint64 vendorId, const QDate &asOf = {}) const;

private:
    Result postPurchase(BusinessDocument &doc, const PostOptions &options, bool isNew, const QString &reason);
    Result validate(BusinessDocument &doc) const;
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
