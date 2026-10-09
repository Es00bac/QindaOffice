// SPDX-License-Identifier: GPL-3.0-or-later
// IIF import, purchase orders (M68, D-077; iif_import.h). QuickBooks
// exports an open purchase order as a PURCHORDR block on its non-posting
// "Purchase Orders" account: the TRNS row names the vendor, DOCNUM the
// order number, DUEDATE (or SHIPDATE) the expected date, SADDR1–5 the
// ship-to; each SPL row is a line — INVITEM, QNTY, PRICE, AMOUNT, MEMO as
// the description, NAME as the customer:job. A purchase order never posts,
// so these blocks skip the balancing and posting of iif_import_transactions
// and become QindaBooks purchase orders (purchase_orders.h).
#include "iif_import_tx_p.h"

#include "bank_import.h"
#include "company_file.h"
#include "documents.h"
#include "purchase_orders.h"
#include "sql_util.h"

namespace QindaOffice::Books::IifInternal {

namespace {

qint64 magnitude(qint64 v)
{
    return v < 0 ? -v : v;
}

std::optional<Decimal> positive(const QString &text)
{
    std::optional<Decimal> d = Decimal::parse(QString(text).remove(QLatin1Char(',')));
    if (d && d->isNegative()) {
        d = Decimal(-d->mantissa(), d->scale());
    }
    return d;
}

// The order as QindaBooks keeps it, or `refusal` filled.
BusinessDocument orderFrom(Context &ctx, const IifTransaction &t, QString *refusal)
{
    const QString where = QStringLiteral("lines %1–%2: ").arg(t.firstLine).arg(t.lastLine);
    BusinessDocument po;
    po.kind = DocKind::PurchaseOrder;
    po.date = IifReader::parseDate(t.trns.value(QStringLiteral("DATE")));
    po.number = t.trns.value(QStringLiteral("DOCNUM"));
    po.memo = t.trns.value(QStringLiteral("MEMO"));
    po.dueDate = IifReader::parseDate(t.trns.value(QStringLiteral("DUEDATE")));
    if (!po.dueDate.isValid()) {
        po.dueDate = IifReader::parseDate(t.trns.value(QStringLiteral("SHIPDATE")));
    }
    QStringList shipTo;
    for (int i = 1; i <= 5; ++i) {
        const QString line = t.trns.value(QStringLiteral("SADDR%1").arg(i));
        if (!line.isEmpty()) {
            shipTo << line;
        }
    }
    po.shipTo = shipTo.join(QLatin1Char('\n'));
    if (!t.error.isEmpty()) {
        *refusal = where + t.error;
        return po;
    }
    if (!po.date.isValid()) {
        *refusal = QStringLiteral("line %1: \"%2\" is not a date (MM/DD/YYYY)").arg(t.trns.line).arg(t.trns.value(QStringLiteral("DATE")));
        return po;
    }
    po.partyId = ctx.vendor(t.trns.value(QStringLiteral("NAME")), true);
    if (po.partyId == 0) {
        *refusal = where + QStringLiteral("a purchase order needs a vendor");
        return po;
    }
    for (const IifRecord &r : t.splits) {
        const std::optional<qint64> amount = r.value(QStringLiteral("AMOUNT")).isEmpty()
            ? std::optional<qint64>(0)
            : BankImport::parseAmount(r.value(QStringLiteral("AMOUNT")), ctx.exponent);
        if (!amount) {
            *refusal = QStringLiteral("line %1: \"%2\" is not an amount").arg(r.line).arg(r.value(QStringLiteral("AMOUNT")));
            return po;
        }
        DocumentLine line;
        line.itemId = ctx.item(r.value(QStringLiteral("INVITEM")));
        if (line.itemId == 0 && *amount == 0) {
            continue; // a heading or note row
        }
        line.accountId = ctx.account(r.value(QStringLiteral("ACCNT")));
        line.description = r.value(QStringLiteral("MEMO"));
        line.classId = ctx.klass(r.value(QStringLiteral("CLASS")));
        line.partyId = ctx.customers.value(r.value(QStringLiteral("NAME")).toLower()); // never creates one
        const std::optional<Decimal> qty = positive(r.value(QStringLiteral("QNTY")));
        line.quantity = qty && !qty->isZero() ? *qty : Decimal(1, 0);
        const std::optional<Decimal> price = positive(r.value(QStringLiteral("PRICE")));
        const std::optional<Money> rate = price ? Money::parse(price->toString(), ctx.file.currency()) : std::nullopt;
        // A rate only where rate × quantity is the amount the file says;
        // otherwise the amount stands (the order must total what QuickBooks had).
        if (rate && !rate->isZero() && rate->multiplied(line.quantity).minor() == magnitude(*amount)) {
            line.unitPrice = *rate;
            line.amount = ctx.file.money(0);
        } else {
            line.unitPrice = ctx.file.money(0);
            line.amount = ctx.file.money(magnitude(*amount));
        }
        line.taxableFromItem = false;
        po.lines << line;
    }
    if (po.lines.isEmpty()) {
        *refusal = where + QStringLiteral("a purchase order needs at least one line");
    }
    return po;
}

// The same vendor, date, number and total already in the books.
bool alreadyImported(CompanyFile &file, const BusinessDocument &po, qint64 total)
{
    return Sql::scalarInt(file.database(),
                          QStringLiteral("SELECT COUNT(*) FROM documents WHERE kind = ? AND party_id = ? AND date = ?"
                                         " AND COALESCE(number, '') = ? AND total = ? AND state <> ?"),
                          {int(DocKind::PurchaseOrder), po.partyId, Sql::isoDate(po.date), po.number, total,
                           int(DocState::Void)})
        > 0;
}

} // namespace

bool isPurchaseOrder(const IifTransaction &t)
{
    return IifReader::normalizedType(t.trns.value(QStringLiteral("TRNSTYPE"))) == QLatin1String("PURCHORDR");
}

void importPurchaseOrders(Context &ctx, const QList<IifTransaction> &orders)
{
    if (orders.isEmpty()) {
        return;
    }
    IifTypeCount count{QStringLiteral("PURCHORDR"), 0, 0, 0, 0, 0};
    for (const IifTransaction &t : orders) {
        ++count.count;
        QString refusal;
        BusinessDocument po = orderFrom(ctx, t, &refusal);
        if (refusal.isEmpty()) {
            Documents(ctx.file).finalize(po);
            if (alreadyImported(ctx.file, po, po.total.minor())) {
                ++count.duplicates;
                continue;
            }
            const Result r = PurchaseOrders(ctx.file).create(po);
            if (r.ok && t.trns.value(QStringLiteral("PAID")).toUpper() == QLatin1String("Y")) {
                PurchaseOrders(ctx.file).setClosed(r.id, true); // received or closed in QuickBooks
            }
            if (!r.ok) {
                refusal = QStringLiteral("lines %1–%2: %3").arg(t.firstLine).arg(t.lastLine).arg(r.error);
            }
        }
        if (!refusal.isEmpty()) {
            ++count.refused;
            ctx.report.refused << IifRefusal{t.firstLine, t.lastLine, t.trns.value(QStringLiteral("TRNSTYPE")), refusal};
            continue;
        }
        ++count.documents;
    }
    ctx.report.transactions << count;
    ctx.report.note(QStringLiteral("Purchase orders came in as purchase orders: they never post, and bills receive them."));
}

} // namespace QindaOffice::Books::IifInternal
