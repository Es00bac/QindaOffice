// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Purchase orders (M68, D-077; docs/books.md "Purchase orders"): what the
// business ordered from a vendor and how much of it bills have received.
//
// A purchase order is a BusinessDocument of kind PurchaseOrder: vendor,
// date, number (prefix + counter, "PO-1001"), expected date (`dueDate`),
// ship-to address (`shipTo`), vendor message (`message`, printed), memo and
// lines — an item or an account, description, quantity, rate, amount and a
// customer:job (`DocumentLine::partyId`). It NEVER posts: the ledger does
// not know it exists.
//
// AGENT-CONTRACT (receiving; schema v4, documents.h): a bill line that
// receives a purchase-order line names it in `poLineId`. A line's received
// quantity is the sum of the quantities of LIVE bill lines naming it (a
// void bill counts nothing), so voiding a bill, or editing a bill so the
// line is gone, puts the quantity back with no bookkeeping of its own.
// Nothing here stores a received count.
//
// Status (derived, Documents::status): Void; Closed when closed by hand
// (state Closed) or when every line is received in full or closed by hand;
// PartiallyReceived when any line has received something; else Open.
//
// Rules: receipts are positive and never exceed what remains on the line;
// a closed line or order receives nothing new (a bill that already
// received it may keep its quantity); a line with receipts cannot be
// removed from the order nor ordered below what was received; an order a
// live bill receives cannot be voided — close it instead.

#include "books_types.h"
#include "decimal.h"
#include "documents.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

// One purchase-order line as receiving and the reports see it.
struct OrderLine {
    qint64 lineId = 0;
    qint64 orderId = 0;
    QString orderNumber;
    QDate orderDate;
    QDate expected;
    qint64 vendorId = 0;
    qint64 itemId = 0;
    qint64 accountId = 0;  // the line's account; the item's expense account when it names none
    qint64 customerId = 0; // customer:job
    qint64 classId = 0;
    QString description;
    Decimal ordered;
    Decimal received;
    Money rate;            // unit price; zero for an amount-only line
    Money amount;          // the line amount
    bool closed = false;   // closed by hand: the line, or its whole order

    [[nodiscard]] Decimal remaining() const; // ordered − received, never below zero
    [[nodiscard]] bool receivable() const { return !closed && !remaining().isZero(); }
    // What is still to receive, in money: rate × remaining, or for an
    // amount-only line amount × remaining ÷ ordered (half-up).
    [[nodiscard]] Money remainingAmount() const;
};

// One receipt chosen in the receive dialog: `quantity` of a line.
struct OrderReceipt {
    qint64 lineId = 0;
    Decimal quantity;
};

class PurchaseOrders {
public:
    explicit PurchaseOrders(CompanyFile &file);

    // Kind and state are forced; an empty number takes the next one (a
    // used number is refused, Duplicate). Audited. Result.id = the order.
    Result create(BusinessDocument order);
    // Edit in place; line ids are kept (documents.h), so receipts stay linked.
    Result update(BusinessDocument order, const QString &reason = {});
    // Closes (or reopens) the whole order by hand.
    Result setClosed(qint64 orderId, bool closed);
    Result setLineClosed(qint64 lineId, bool closed);

    // Every line of one order, with what it received as of `asOf` (bills
    // dated on or before it; all bills when invalid).
    [[nodiscard]] QList<OrderLine> lines(qint64 orderId, const QDate &asOf = {}) const;
    // The receivable lines of a vendor's orders that are neither void nor
    // closed, oldest order first.
    [[nodiscard]] QList<OrderLine> openLines(qint64 vendorId) const;
    [[nodiscard]] DocumentStatus status(const BusinessDocument &order, const QDate &asOf = {}) const;
    // Σ remainingAmount() of the order's open lines.
    [[nodiscard]] Money remainingAmount(qint64 orderId, const QDate &asOf = {}) const;
    // The number of the order a line belongs to ("" when there is none) —
    // the badge on a bill line that receives it.
    [[nodiscard]] QString orderNumberOf(qint64 lineId) const;

    // Bill lines that receive `receipts`: item or account, description,
    // rate and customer:job from the order line, the chosen quantity, the
    // link. nullopt with `error` when a receipt is not allowed.
    [[nodiscard]] std::optional<QList<DocumentLine>> billLines(const QList<OrderReceipt> &receipts,
                                                               QString *error = nullptr) const;
    // Everything still to receive on one order, as bill lines (Create Bill).
    [[nodiscard]] QList<DocumentLine> remainingAsBillLines(qint64 orderId) const;

    // ---- engine-internal ----
    // Purchases::validate: every linked line of `bill` fits its order.
    [[nodiscard]] Result checkReceipts(const BusinessDocument &bill) const;
    // Ledger::voidDocument: refused while a live bill receives the order.
    [[nodiscard]] Result checkVoidable(qint64 orderId) const;

private:
    Result validate(BusinessDocument &order) const;
    Result write(BusinessDocument &order, bool isNew, const QString &reason);
    CompanyFile &m_file;
};

// Documents::status() for kind PurchaseOrder.
[[nodiscard]] DocumentStatus purchaseOrderStatus(CompanyFile &file, const BusinessDocument &order);

} // namespace QindaOffice::Books
