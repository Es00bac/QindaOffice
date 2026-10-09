// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: the editor's draft map → engine document, a document as a
// list row, and the editor's live totals (split from
// books_service_documents.cpp at M68, the 500-line rule).
#include "books_service.h"

#include "books_service_kinds.h"

#include "company_file.h"
#include "documents.h"
#include "lists.h"
#include "purchase_orders.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
using namespace Kinds;

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
    // M68: a purchase order's ship-to; a check waiting in the print queue.
    doc->shipTo = map.value(QStringLiteral("shipTo")).toString().trimmed();
    doc->toPrint = map.value(QStringLiteral("toPrint")).toBool();
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
        line.id = Format::id(l.value(QStringLiteral("id"))); // kept on a rewrite (documents.h, M68)
        line.itemId = Format::id(l.value(QStringLiteral("itemId")));
        line.accountId = Format::id(l.value(QStringLiteral("accountId")));
        line.classId = Format::id(l.value(QStringLiteral("classId")));
        // M68: customer:job (purchase orders, bills), the receipt and the
        // billed time a line carries, a purchase-order line closed by hand.
        line.partyId = Format::id(l.value(QStringLiteral("customerId")));
        line.poLineId = Format::id(l.value(QStringLiteral("poLineId")));
        line.timeEntryId = Format::id(l.value(QStringLiteral("timeEntryId")));
        line.closed = l.value(QStringLiteral("closed")).toBool();
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
    // A purchase order's balance is what is still to receive (M68).
    const qint64 balance = doc.state == DocState::Void ? 0
        : doc.kind == DocKind::PurchaseOrder
        ? (status == DocumentStatus::Closed ? 0 : PurchaseOrders(*c).remainingAmount(doc.id).minor())
        : !tracksBalance(doc.kind) ? 0
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

} // namespace QindaOffice::Apps::Books
