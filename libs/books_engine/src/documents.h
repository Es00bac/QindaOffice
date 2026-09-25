// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Business documents (invoice, bill, payment, journal entry, …) and the
// arithmetic shared by every sales and purchase form.
//
// A document is what the user edits; its posting is a JournalTransaction
// (journal.h). Sales/Purchases/Banking build the posting lines; this
// module stores documents, computes totals and tracks applications
// (payment → invoice, credit memo → invoice, bill payment → bill …).
//
// AGENT-CONTRACT (open balances): for a TARGET document (invoice, bill,
// refund, vendor refund) open = total − Σ live applications where it is
// the target. For a SOURCE document (payment, credit memo, bill payment,
// vendor credit) unapplied = total − Σ live applications where it is the
// source. Aging, statements, status and cash-basis recognition all read
// these two numbers, so they cannot disagree.

#include "books_types.h"
#include "decimal.h"
#include "money.h"

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

struct DocumentLine {
    qint64 id = 0;
    qint64 itemId = 0;
    qint64 accountId = 0;   // income/expense/asset account; 0 = the item's
    QString description;
    Decimal quantity = Decimal(1, 0);
    Money unitPrice;        // non-zero: amount = unitPrice × quantity (half-up)
    Money amount;           // line total (authoritative when unitPrice is zero)
    bool taxable = false;
    // fillFromItems() copies Item::taxable into `taxable` while this is
    // true; set it false to override the item on one line. Lines read
    // back from the file carry false (the stored flag is authoritative).
    bool taxableFromItem = true;
    qint64 classId = 0;
    qint64 partyId = 0;     // journal entries: per-line name
    qint64 linkDocId = 0;   // deposits: the payment being deposited
    Money debit;            // journal entries only
    Money credit;
};

struct Discount {
    enum class Kind { None, Percent, Amount };
    Kind kind = Kind::None;
    Decimal percent;
    Money amount;

    [[nodiscard]] static Discount none() { return {}; }
    [[nodiscard]] static Discount ofPercent(const Decimal &p) { return {Kind::Percent, p, {}}; }
    [[nodiscard]] static Discount ofAmount(const Money &m) { return {Kind::Amount, {}, m}; }
    // Storage/text form: "" | "10%" | "25.00".
    [[nodiscard]] QString toText() const;
    [[nodiscard]] static Discount fromText(const QString &text, const QString &currency);
};

struct TaxShare {
    qint64 componentId = 0;
    qint64 agencyId = 0;
    QString name;
    Decimal rate;
    Money amount;
};

struct DocumentTotals {
    Money subtotal;      // Σ line amounts
    Money discount;      // pre-tax discount (positive)
    Money taxableBase;   // taxable lines after their share of the discount
    QList<TaxShare> taxes;
    Money tax;
    Money total;         // subtotal − discount + tax
    QList<Money> lineAmounts;
};

struct BusinessDocument {
    qint64 id = 0;
    DocKind kind = DocKind::Invoice;
    DocState state = DocState::Open;
    QString number;
    qint64 partyId = 0;
    QDate date;
    QDate dueDate;          // invalid = from terms
    qint64 termsId = 0;
    QString memo;           // internal
    QString message;        // printed on the form
    QString reference;      // cheque number, payment reference, vendor ref
    qint64 accountId = 0;   // bank/credit card/undeposited funds; transfer "from"
    qint64 toAccountId = 0; // transfer "to"
    Discount discount;
    qint64 taxRateId = 0;
    QList<DocumentLine> lines;
    qint64 sourceDocId = 0; // estimate an invoice came from
    qint64 templateId = 0;  // recurring template that generated it
    qint64 txnId = 0;       // live posting (0 for drafts, estimates, void)
    // Stored totals (written by the engine from DocumentTotals):
    Money subtotal;
    Money discountAmount;
    Money taxAmount;
    Money total;
};

struct Application {
    qint64 id = 0;
    qint64 sourceDocId = 0;
    qint64 targetDocId = 0;
    Money amount;
    QDate date;
    bool voided = false;
};

struct DocumentFilter {
    QList<DocKind> kinds;
    qint64 partyId = 0;
    QDate from;
    QDate to;
    bool includeVoid = false;
    bool includeDrafts = true;
};

class Documents {
public:
    explicit Documents(CompanyFile &file);

    [[nodiscard]] std::optional<BusinessDocument> load(qint64 id) const;
    [[nodiscard]] QList<BusinessDocument> list(const DocumentFilter &filter) const;

    // Line amounts, discount, tax per component and total. Tax: each
    // component of the document's rate × the taxable base, rounded ONCE per
    // document with the agency's rounding mode.
    [[nodiscard]] DocumentTotals computeTotals(const BusinessDocument &doc) const;
    // computeTotals() and store the result on the document (line amounts,
    // subtotal, discount, tax, total) — what every form calls before write().
    DocumentTotals finalize(BusinessDocument &doc) const;
    // Fills unit price/description/account/taxable from the item when the
    // line names one and leaves them empty. `purchase` picks the item's
    // expense account instead of its income account.
    void fillFromItems(BusinessDocument &doc, bool purchase) const;
    [[nodiscard]] QDate dueDateFor(const QDate &date, qint64 termsId) const;

    // ---- applications ----
    [[nodiscard]] Money appliedTo(qint64 targetDocId, const QDate &asOf = {}) const;
    [[nodiscard]] Money appliedFrom(qint64 sourceDocId, const QDate &asOf = {}) const;
    // Target: total − applied to it. Source: total − applied from it.
    [[nodiscard]] Money openBalance(qint64 docId, const QDate &asOf = {}) const;
    [[nodiscard]] QList<Application> applicationsFor(qint64 docId) const;
    [[nodiscard]] DocumentStatus status(const BusinessDocument &doc, const QDate &today) const;

    [[nodiscard]] QJsonObject toJson(const BusinessDocument &doc) const;

    // ---- engine-internal: callers hold a SqlTransaction ----
    // Inserts (id == 0) or rewrites header + lines; sets doc.id.
    Result write(BusinessDocument &doc);
    Result setState(qint64 docId, DocState state);
    Result addApplication(qint64 sourceDocId, qint64 targetDocId, const Money &amount,
                          const QDate &date);
    Result voidApplicationsOf(qint64 docId);

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
