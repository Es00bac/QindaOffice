// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: business documents — the lists the Sales and Expenses
// workspaces show, the editor's load and save, drafts, estimates, void.
// The draft map and the list row are books_service_document_map.cpp.
#include "books_service.h"

#include "books_service_kinds.h"

#include "accounts.h"
#include "company_file.h"
#include "connections.h"
#include "documents.h"
#include "journal.h"
#include "lists.h"
#include "purchase_orders.h"
#include "purchases.h"
#include "sales.h"

#include <QHash>

#include <algorithm>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

using namespace Kinds;

const QList<DocKind> kEverything = kSalesGroup + QList<DocKind>{DocKind::Estimate, DocKind::Refund} + kExpenseKinds
    + kJournalGroup + QList<DocKind>{DocKind::PurchaseOrder};

QList<DocKind> kindsFrom(const QVariant &value)
{
    QStringList keys = value.typeId() == QMetaType::QString ? QStringList{value.toString()} : value.toStringList();
    QList<DocKind> kinds;
    for (const QString &key : keys) {
        if (key == QLatin1String("sales")) {
            kinds << kSalesGroup;
        } else if (key == QLatin1String("purchases") || key == QLatin1String("expenses")) {
            kinds << kExpenseKinds;
        } else if (key == QLatin1String("orders")) {
            kinds << DocKind::PurchaseOrder;
        } else if (key == QLatin1String("journal")) {
            kinds << kJournalGroup;
        } else if (key == QLatin1String("all")) {
            kinds << kEverything;
        } else if (const std::optional<DocKind> k = Format::kindFromKey(key)) {
            kinds << *k;
        }
    }
    return kinds;
}

PostOptions postOptions(const QVariantMap &map)
{
    const QString reason = map.value(QStringLiteral("lockOverrideReason")).toString().trimmed();
    return reason.isEmpty() ? PostOptions{} : PostOptions{true, reason};
}

} // namespace

QVariantList BooksService::documents(const QVariantMap &filter) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    DocumentFilter f;
    f.kinds = kindsFrom(filter.value(QStringLiteral("kind")));
    if (f.kinds.isEmpty()) {
        f.kinds = kEverything;
    }
    f.partyId = Format::id(filter.value(QStringLiteral("partyId")));
    f.from = Format::date(filter.value(QStringLiteral("from")));
    f.to = Format::date(filter.value(QStringLiteral("to")));
    const QString status = filter.value(QStringLiteral("status")).toString();
    f.includeVoid = status.isEmpty() || status == QLatin1String("all") || status == QLatin1String("void");
    f.includeDrafts = true;
    const QString search = filter.value(QStringLiteral("search")).toString().trimmed();
    QList<BusinessDocument> docs = Documents(*c).list(f);
    std::sort(docs.begin(), docs.end(), [](const BusinessDocument &a, const BusinessDocument &b) {
        return a.date != b.date ? a.date > b.date : a.id > b.id;
    });
    for (const BusinessDocument &doc : std::as_const(docs)) {
        const QVariantMap row = documentRow(doc);
        const QString s = row.value(QStringLiteral("status")).toString();
        const bool keep = status.isEmpty() || status == QLatin1String("all")
            || (status == QLatin1String("open")
                && (s == QLatin1String("open") || s == QLatin1String("partial") || s == QLatin1String("overdue")
                    || s == QLatin1String("pending") || s == QLatin1String("partiallyReceived")))
            || s == status;
        if (!keep) {
            continue;
        }
        if (!search.isEmpty()) {
            const QString hay = row.value(QStringLiteral("number")).toString() + QLatin1Char('\n')
                + row.value(QStringLiteral("partyName")).toString() + QLatin1Char('\n') + doc.memo;
            if (!hay.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
        }
        out << row;
    }
    return out;
}

QVariantMap BooksService::document(qint64 id) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return {};
    }
    Documents documents(*c);
    const std::optional<BusinessDocument> doc = documents.load(id);
    if (!doc) {
        return {};
    }
    const MoneyFormat money = moneyFormat();
    QVariantMap out = documentRow(*doc);
    const DocumentTotals totals = documents.computeTotals(*doc);
    // M68: a purchase order's lines carry what bills received.
    QHash<qint64, OrderLine> received;
    if (doc->kind == DocKind::PurchaseOrder) {
        for (const OrderLine &o : PurchaseOrders(*c).lines(id)) {
            received.insert(o.lineId, o);
        }
    }
    QVariantList lines;
    for (qsizetype i = 0; i < doc->lines.size(); ++i) {
        const DocumentLine &l = doc->lines[i];
        const qint64 amount = i < totals.lineAmounts.size() ? totals.lineAmounts[i].minor() : l.amount.minor();
        QVariantMap line{{QStringLiteral("id"), l.id},
                         {QStringLiteral("itemId"), l.itemId},
                         {QStringLiteral("description"), l.description},
                         {QStringLiteral("quantity"), l.quantity.toString()},
                         {QStringLiteral("rate"), l.unitPrice.isZero() ? QString() : money.plain(l.unitPrice.minor())},
                         {QStringLiteral("amount"), amount},
                         {QStringLiteral("amountText"), money.text(amount)},
                         {QStringLiteral("accountId"), l.accountId},
                         {QStringLiteral("taxable"), l.taxable},
                         {QStringLiteral("classId"), l.classId},
                         {QStringLiteral("customerId"), l.partyId},
                         {QStringLiteral("poLineId"), l.poLineId},
                         {QStringLiteral("timeEntryId"), l.timeEntryId},
                         {QStringLiteral("closed"), l.closed}};
        if (received.contains(l.id)) {
            const OrderLine &o = received.value(l.id);
            line.insert(QStringLiteral("received"), o.received.toString());
            line.insert(QStringLiteral("remaining"), o.remaining().toString());
        }
        if (l.poLineId != 0) {
            line.insert(QStringLiteral("orderNumber"), PurchaseOrders(*c).orderNumberOf(l.poLineId));
        }
        lines << line;
    }
    QVariantList applications;
    for (const Application &a : documents.applicationsFor(id)) {
        const qint64 other = a.sourceDocId == id ? a.targetDocId : a.sourceDocId;
        const std::optional<BusinessDocument> o = documents.load(other);
        applications << QVariantMap{{QStringLiteral("id"), a.id},
                                    {QStringLiteral("docId"), other},
                                    {QStringLiteral("kind"), o ? Format::kindKey(o->kind) : QString()},
                                    {QStringLiteral("number"), o ? o->number : QString()},
                                    {QStringLiteral("date"), Format::isoDate(a.date)},
                                    {QStringLiteral("amount"), a.amount.minor()},
                                    {QStringLiteral("amountText"), money.text(a.amount.minor())},
                                    {QStringLiteral("voided"), a.voided}};
    }
    const bool isDraft = doc->state == DocState::Draft;
    out.insert(QStringLiteral("state"), isDraft                        ? QStringLiteral("draft")
                                        : doc->state == DocState::Void ? QStringLiteral("void")
                                        : doc->state == DocState::Closed ? QStringLiteral("closed")
                                                                          : QStringLiteral("open"));
    out.insert(QStringLiteral("termsId"), doc->termsId);
    out.insert(QStringLiteral("notes"), doc->message);
    out.insert(QStringLiteral("reference"), doc->reference);
    out.insert(QStringLiteral("accountId"), doc->accountId);
    out.insert(QStringLiteral("depositAccountId"), doc->accountId);
    out.insert(QStringLiteral("paymentAccountId"), doc->accountId);
    out.insert(QStringLiteral("toAccountId"), doc->toAccountId);
    out.insert(QStringLiteral("discountPercent"),
               doc->discount.kind == Discount::Kind::Percent ? Format::percentText(doc->discount.percent) : QString());
    out.insert(QStringLiteral("discountAmount"),
               doc->discount.kind == Discount::Kind::Amount ? money.plain(doc->discount.amount.minor()) : QString());
    out.insert(QStringLiteral("taxRateId"), doc->taxRateId);
    // The online payment link while it still charges the open balance (M48).
    const std::optional<PaymentLinkRecord> link = Connections(*c).currentPaymentLink(id);
    out.insert(QStringLiteral("paymentLinkUrl"), link ? link->url : QString());
    out.insert(QStringLiteral("sourceDocId"), doc->sourceDocId);
    out.insert(QStringLiteral("shipTo"), doc->shipTo);
    out.insert(QStringLiteral("toPrint"), doc->toPrint);
    out.insert(QStringLiteral("lines"), lines);
    out.insert(QStringLiteral("applications"), applications);
    for (const auto &[key, value] : {std::pair{"subtotal", totals.subtotal.minor()},
                                     std::pair{"discount", totals.discount.minor()},
                                     std::pair{"tax", totals.tax.minor()}}) {
        out.insert(QString::fromLatin1(key), value);
        out.insert(QString::fromLatin1(key) + QStringLiteral("Text"), money.text(value));
    }
    // What the editor may do with it (the engine enforces the same rules).
    const bool isOrder = doc->kind == DocKind::PurchaseOrder;
    const bool editableKind = doc->kind == DocKind::Invoice || doc->kind == DocKind::Bill
        || doc->kind == DocKind::Estimate;
    out.insert(QStringLiteral("canEdit"), doc->state != DocState::Void
                                              && ((doc->state != DocState::Closed && editableKind) || isOrder));
    out.insert(QStringLiteral("canVoid"), doc->state != DocState::Void);
    out.insert(QStringLiteral("canPrint"), doc->state != DocState::Void
                                              && (doc->kind == DocKind::Invoice || doc->kind == DocKind::Estimate
                                                  || doc->kind == DocKind::SalesReceipt
                                                  || doc->kind == DocKind::CreditMemo || isOrder));
    out.insert(QStringLiteral("closedByHand"), isOrder && doc->state == DocState::Closed);
    return out;
}

QVariantMap BooksService::saveDocument(const QVariantMap &draft)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    BusinessDocument doc;
    QString error;
    if (!documentFromMap(draft, &doc, &error)) {
        return Format::failure(error);
    }
    const PostOptions options = postOptions(draft);
    QString reason = draft.value(QStringLiteral("reason")).toString().trimmed();
    if (reason.isEmpty()) {
        reason = tr("Edited in QindaBooks");
    }
    const bool isNew = doc.id == 0;
    if (doc.state == DocState::Draft && doc.kind != DocKind::Invoice) {
        return Format::failure(tr("Only invoices can be kept as drafts; this %1 posts when saved.")
                                   .arg(Format::kindLabel(doc.kind).toLower()));
    }
    Sales sales(*c);
    Purchases purchases(*c);
    Result r;
    switch (doc.kind) {
    case DocKind::PurchaseOrder: // M68: never posts, edits in place
        r = isNew ? PurchaseOrders(*c).create(doc) : PurchaseOrders(*c).update(doc, reason);
        break;
    case DocKind::Invoice: r = isNew ? sales.createInvoice(doc, options) : sales.updateInvoice(doc, reason, options); break;
    case DocKind::Estimate: r = sales.saveEstimate(doc); break;
    case DocKind::Bill: r = isNew ? purchases.createBill(doc, options) : purchases.updateBill(doc, reason, options); break;
    case DocKind::SalesReceipt:
    case DocKind::CreditMemo:
    case DocKind::Expense:
    case DocKind::Check:
    case DocKind::CreditCardCharge:
    case DocKind::VendorCredit:
        if (!isNew) {
            // AGENT-NOTE: the engine edits (void + repost, same id) only
            // invoices, bills and estimates; these are voided and re-entered.
            return Format::failure(tr("A posted %1 cannot be edited. Void it and enter a new one.")
                                       .arg(Format::kindLabel(doc.kind).toLower()));
        }
        if (doc.kind == DocKind::SalesReceipt) {
            r = sales.createSalesReceipt(doc, options);
        } else if (doc.kind == DocKind::CreditMemo) {
            r = sales.createCreditMemo(doc, options);
        } else if (doc.kind == DocKind::VendorCredit) {
            r = purchases.createVendorCredit(doc, options);
        } else {
            r = purchases.createExpense(doc, options);
        }
        break;
    default:
        return Format::failure(tr("%1 is entered from its own dialog.").arg(Format::kindLabel(doc.kind)));
    }
    QVariantMap out = Format::result(r);
    if (r.ok) {
        if (const std::optional<BusinessDocument> saved = Documents(*c).load(r.id)) {
            out.insert(QStringLiteral("number"), saved->number);
        }
    }
    return out;
}

QVariantMap BooksService::postDraft(qint64 id)
{
    return file() != nullptr ? Format::result(Sales(*file()).postDraft(id)) : noCompany();
}

QVariantMap BooksService::convertEstimate(qint64 id)
{
    return file() != nullptr ? Format::result(Sales(*file()).convertEstimateToInvoice(id, todayDate())) : noCompany();
}

QVariantMap BooksService::voidDocument(qint64 id, const QString &reason, bool overrideLock)
{
    if (file() == nullptr) {
        return noCompany();
    }
    const QString why = reason.trimmed().isEmpty() ? tr("Voided in QindaBooks") : reason.trimmed();
    return Format::result(Ledger(*file()).voidDocument(id, why, overrideLock ? PostOptions{true, why} : PostOptions{}));
}

} // namespace QindaOffice::Apps::Books
