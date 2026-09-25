// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: business documents — the lists the Sales and Expenses
// workspaces show, the editor's load/totals/save, drafts, estimates, void.
#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "journal.h"
#include "lists.h"
#include "purchases.h"
#include "sales.h"

#include <QHash>

#include <algorithm>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

// AGENT-CONTRACT (documents() kind groups, with the lead's views):
// "sales" = invoice, salesReceipt, creditMemo, payment (estimates are
// asked for by kind); "purchases" (alias "expenses") = bill, expense,
// check, creditCardCharge, vendorCredit, billPayment; "journal" = journal
// entries, transfers, deposits, opening balances, sales-tax payments.
const QList<DocKind> kSalesGroup{DocKind::Invoice, DocKind::SalesReceipt, DocKind::CreditMemo,
                                 DocKind::ReceivePayment};
const QList<DocKind> kExpenseKinds{DocKind::Bill,    DocKind::BillPayment,      DocKind::Expense,
                                   DocKind::Check,   DocKind::CreditCardCharge, DocKind::VendorCredit};
const QList<DocKind> kJournalGroup{DocKind::JournalEntry, DocKind::Transfer, DocKind::Deposit,
                                   DocKind::OpeningBalance, DocKind::SalesTaxPayment};
const QList<DocKind> kEverything = kSalesGroup + QList<DocKind>{DocKind::Estimate, DocKind::Refund} + kExpenseKinds
    + kJournalGroup;

bool isPurchase(DocKind kind)
{
    return kExpenseKinds.contains(kind);
}

// Kinds whose open balance means something (targets and sources of
// applications); every other kind shows no balance.
bool tracksBalance(DocKind kind)
{
    return kind == DocKind::Invoice || kind == DocKind::Bill || kind == DocKind::ReceivePayment
        || kind == DocKind::CreditMemo || kind == DocKind::BillPayment || kind == DocKind::VendorCredit;
}

QList<DocKind> kindsFrom(const QVariant &value)
{
    QStringList keys = value.typeId() == QMetaType::QString ? QStringList{value.toString()} : value.toStringList();
    QList<DocKind> kinds;
    for (const QString &key : keys) {
        if (key == QLatin1String("sales")) {
            kinds << kSalesGroup;
        } else if (key == QLatin1String("purchases") || key == QLatin1String("expenses")) {
            kinds << kExpenseKinds;
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

// The editor's empty trailing row: nothing chosen, typed or priced.
bool isBlankLine(const QVariantMap &l)
{
    const QString amount = l.value(QStringLiteral("amount")).toString().trimmed();
    return Format::id(l.value(QStringLiteral("itemId"))) == 0 && Format::id(l.value(QStringLiteral("accountId"))) == 0
        && l.value(QStringLiteral("description")).toString().trimmed().isEmpty()
        && l.value(QStringLiteral("rate")).toString().trimmed().isEmpty()
        && (amount.isEmpty() || amount == QLatin1String("0"));
}

PostOptions postOptions(const QVariantMap &map)
{
    const QString reason = map.value(QStringLiteral("lockOverrideReason")).toString().trimmed();
    return reason.isEmpty() ? PostOptions{} : PostOptions{true, reason};
}

} // namespace

bool BooksService::documentFromMap(const QVariantMap &map, BusinessDocument *doc, QString *error) const
{
    CompanyFile *c = file();
    const std::optional<DocKind> kind = Format::kindFromKey(map.value(QStringLiteral("kind")).toString());
    if (!kind) {
        *error = tr("Unknown document kind \"%1\".").arg(map.value(QStringLiteral("kind")).toString());
        return false;
    }
    const MoneyFormat money = moneyFormat();
    doc->kind = *kind;
    doc->id = Format::id(map.value(QStringLiteral("id")));
    doc->state = map.value(QStringLiteral("state")).toString() == QLatin1String("draft") ? DocState::Draft
                                                                                         : DocState::Open;
    doc->partyId = Format::id(map.value(QStringLiteral("partyId")));
    doc->date = Format::date(map.value(QStringLiteral("date")));
    if (!doc->date.isValid()) {
        doc->date = todayDate();
    }
    doc->dueDate = Format::date(map.value(QStringLiteral("dueDate")));
    doc->termsId = Format::id(map.value(QStringLiteral("termsId")));
    doc->number = map.value(QStringLiteral("number")).toString().trimmed();
    doc->memo = map.value(QStringLiteral("memo")).toString();
    doc->message = map.value(QStringLiteral("notes")).toString();
    doc->reference = map.value(QStringLiteral("reference")).toString().trimmed();
    for (const char *key : {"depositAccountId", "paymentAccountId", "accountId"}) {
        if (doc->accountId == 0) {
            doc->accountId = Format::id(map.value(QString::fromLatin1(key)));
        }
    }
    doc->taxRateId = Format::id(map.value(QStringLiteral("taxRateId")));
    const QString percent = map.value(QStringLiteral("discountPercent")).toString().trimmed();
    const QString amountOff = map.value(QStringLiteral("discountAmount")).toString().trimmed();
    if (!percent.isEmpty()) {
        const std::optional<Decimal> p =
            Decimal::parse(percent.endsWith(QLatin1Char('%')) ? percent : percent + QLatin1Char('%'));
        if (!p || p->isNegative()) {
            *error = tr("\"%1\" is not a discount percentage.").arg(percent);
            return false;
        }
        if (!p->isZero()) {
            doc->discount = Discount::ofPercent(*p);
        }
    } else if (!amountOff.isEmpty()) {
        const std::optional<qint64> minor = money.parse(amountOff, error);
        if (!minor) {
            return false;
        }
        if (*minor != 0) {
            doc->discount = Discount::ofAmount(c->money(*minor));
        }
    }
    int row = 0;
    for (const QVariant &value : map.value(QStringLiteral("lines")).toList()) {
        ++row;
        const QVariantMap l = value.toMap();
        DocumentLine line;
        line.itemId = Format::id(l.value(QStringLiteral("itemId")));
        line.accountId = Format::id(l.value(QStringLiteral("accountId")));
        line.classId = Format::id(l.value(QStringLiteral("classId")));
        line.description = l.value(QStringLiteral("description")).toString();
        line.unitPrice = c->money(0);
        line.amount = c->money(0);
        const QString rate = l.value(QStringLiteral("rate")).toString().trimmed();
        const QVariant amount = l.value(QStringLiteral("amount"));
        const bool hasAmount = amount.isValid() && !amount.toString().trimmed().isEmpty();
        if (isBlankLine(l)) {
            continue;
        }
        const QString qty = l.value(QStringLiteral("quantity")).toString().trimmed();
        if (!qty.isEmpty()) {
            const std::optional<Decimal> q = Decimal::parse(qty);
            if (!q) {
                *error = tr("Line %1: \"%2\" is not a quantity.").arg(row).arg(qty);
                return false;
            }
            line.quantity = *q;
        }
        QString lineError;
        if (!rate.isEmpty()) {
            const std::optional<qint64> minor = money.parse(rate, &lineError);
            if (!minor) {
                *error = tr("Line %1: %2").arg(row).arg(lineError);
                return false;
            }
            line.unitPrice = c->money(*minor);
        } else if (hasAmount) {
            const std::optional<qint64> minor = money.parse(amount, &lineError);
            if (!minor) {
                *error = tr("Line %1: %2").arg(row).arg(lineError);
                return false;
            }
            line.amount = c->money(*minor);
        }
        if (l.contains(QStringLiteral("taxable"))) {
            line.taxable = l.value(QStringLiteral("taxable")).toBool();
            line.taxableFromItem = false;
        }
        doc->lines << line;
    }
    return true;
}

QVariantMap BooksService::documentRow(const BusinessDocument &doc) const
{
    CompanyFile *c = file();
    const MoneyFormat money = moneyFormat();
    Documents documents(*c);
    const DocumentStatus status = documents.status(doc, todayDate());
    const qint64 balance = doc.state == DocState::Void || !tracksBalance(doc.kind)
        ? 0
        : documents.openBalance(doc.id).minor();
    return {{QStringLiteral("id"), doc.id},
            {QStringLiteral("kind"), Format::kindKey(doc.kind)},
            {QStringLiteral("kindLabel"), Format::kindLabel(doc.kind)},
            {QStringLiteral("number"), doc.number.isEmpty() ? doc.reference : doc.number},
            {QStringLiteral("date"), Format::isoDate(doc.date)},
            {QStringLiteral("dueDate"), Format::isoDate(doc.dueDate)},
            {QStringLiteral("partyId"), doc.partyId},
            {QStringLiteral("partyName"), nameOfParty(doc.partyId)},
            {QStringLiteral("total"), doc.total.minor()},
            {QStringLiteral("totalText"), money.text(doc.total.minor())},
            {QStringLiteral("balance"), balance},
            {QStringLiteral("balanceText"), money.text(balance)},
            {QStringLiteral("status"), Format::statusKey(status)},
            {QStringLiteral("statusLabel"), Format::statusLabel(status)},
            {QStringLiteral("memo"), doc.memo}};
}

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
                    || s == QLatin1String("pending")))
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
    QVariantList lines;
    for (qsizetype i = 0; i < doc->lines.size(); ++i) {
        const DocumentLine &l = doc->lines[i];
        const qint64 amount = i < totals.lineAmounts.size() ? totals.lineAmounts[i].minor() : l.amount.minor();
        lines << QVariantMap{{QStringLiteral("itemId"), l.itemId},
                             {QStringLiteral("description"), l.description},
                             {QStringLiteral("quantity"), l.quantity.toString()},
                             {QStringLiteral("rate"), l.unitPrice.isZero() ? QString() : money.plain(l.unitPrice.minor())},
                             {QStringLiteral("amount"), amount},
                             {QStringLiteral("amountText"), money.text(amount)},
                             {QStringLiteral("accountId"), l.accountId},
                             {QStringLiteral("taxable"), l.taxable},
                             {QStringLiteral("classId"), l.classId}};
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
    out.insert(QStringLiteral("sourceDocId"), doc->sourceDocId);
    out.insert(QStringLiteral("lines"), lines);
    out.insert(QStringLiteral("applications"), applications);
    for (const auto &[key, value] : {std::pair{"subtotal", totals.subtotal.minor()},
                                     std::pair{"discount", totals.discount.minor()},
                                     std::pair{"tax", totals.tax.minor()}}) {
        out.insert(QString::fromLatin1(key), value);
        out.insert(QString::fromLatin1(key) + QStringLiteral("Text"), money.text(value));
    }
    // What the editor may do with it (the engine enforces the same rules).
    const bool editableKind = doc->kind == DocKind::Invoice || doc->kind == DocKind::Bill
        || doc->kind == DocKind::Estimate;
    out.insert(QStringLiteral("canEdit"), doc->state != DocState::Void && doc->state != DocState::Closed && editableKind);
    out.insert(QStringLiteral("canVoid"), doc->state != DocState::Void);
    out.insert(QStringLiteral("canPrint"), doc->state != DocState::Void
                                              && (doc->kind == DocKind::Invoice || doc->kind == DocKind::Estimate
                                                  || doc->kind == DocKind::SalesReceipt
                                                  || doc->kind == DocKind::CreditMemo));
    return out;
}

QVariantMap BooksService::computeTotals(const QVariantMap &draft) const
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
    Documents documents(*c);
    const bool purchase = isPurchase(doc.kind);
    documents.fillFromItems(doc, purchase);
    if (purchase) {
        doc.discount = Discount::none(); // purchases carry neither (purchases.h)
        doc.taxRateId = 0;
    } else if (doc.taxRateId == 0) {
        // Mirror Sales::validate: the customer's default rate applies.
        if (const std::optional<Party> p = Lists(*c).party(doc.partyId)) {
            doc.taxRateId = p->taxRateId;
        }
    }
    const DocumentTotals totals = documents.computeTotals(doc);
    const MoneyFormat money = moneyFormat();
    QVariantMap out = Format::success();
    // The due date the save will store: the typed one, else from the
    // terms (the document's, the party's, the company default).
    QDate due = doc.dueDate;
    if (!due.isValid() && (doc.kind == DocKind::Invoice || doc.kind == DocKind::Bill)) {
        qint64 termsId = doc.termsId;
        if (termsId == 0) {
            if (const std::optional<Party> p = Lists(*c).party(doc.partyId)) {
                termsId = p->termsId;
            }
        }
        due = documents.dueDateFor(doc.date, termsId != 0 ? termsId : c->settings().defaultTermsId);
    }
    out.insert(QStringLiteral("dueDate"), Format::isoDate(due));
    for (const auto &[key, value] : {std::pair{"subtotal", totals.subtotal.minor()},
                                     std::pair{"discount", totals.discount.minor()},
                                     std::pair{"taxableBase", totals.taxableBase.minor()},
                                     std::pair{"tax", totals.tax.minor()},
                                     std::pair{"total", totals.total.minor()}}) {
        out.insert(QString::fromLatin1(key), value);
        out.insert(QString::fromLatin1(key) + QStringLiteral("Text"), money.text(value));
    }
    // One entry per INPUT line, blank rows included, so the editor can
    // index them by row.
    QVariantList lines;
    qsizetype next = 0;
    for (const QVariant &value : draft.value(QStringLiteral("lines")).toList()) {
        if (isBlankLine(value.toMap()) || next >= totals.lineAmounts.size()) {
            lines << QVariantMap{{QStringLiteral("amount"), 0}, {QStringLiteral("amountText"), QString()}};
            continue;
        }
        const qint64 m = totals.lineAmounts.at(next++).minor();
        lines << QVariantMap{{QStringLiteral("amount"), m}, {QStringLiteral("amountText"), money.text(m)}};
    }
    QVariantList taxes;
    for (const TaxShare &t : totals.taxes) {
        taxes << QVariantMap{{QStringLiteral("name"), t.name},
                             {QStringLiteral("rate"), t.rate.toPercentString()},
                             {QStringLiteral("amount"), t.amount.minor()},
                             {QStringLiteral("amountText"), money.text(t.amount.minor())}};
    }
    out.insert(QStringLiteral("lines"), lines);
    out.insert(QStringLiteral("taxes"), taxes);
    out.insert(QStringLiteral("taxRateId"), doc.taxRateId);
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
