// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: purchase orders (M68, D-077; purchase_orders.h) — the
// lines with what bills received, the bill editor's receive offer and
// lines, Create Bill, closing an order or a line by hand. Saving, printing,
// e-mailing and voiding go through the document calls.
#include "books_service.h"

#include "company_file.h"
#include "documents.h"
#include "lists.h"
#include "purchase_orders.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QVariantMap orderLineMap(const OrderLine &o, Lists &lists, const MoneyFormat &money)
{
    const std::optional<Item> item = lists.item(o.itemId);
    const std::optional<Party> job = lists.party(o.customerId);
    const qint64 remaining = o.closed ? 0 : o.remainingAmount().minor();
    return {{QStringLiteral("lineId"), o.lineId},
            {QStringLiteral("orderId"), o.orderId},
            {QStringLiteral("orderNumber"), o.orderNumber},
            {QStringLiteral("orderDate"), Format::isoDate(o.orderDate)},
            {QStringLiteral("expected"), Format::isoDate(o.expected)},
            {QStringLiteral("itemId"), o.itemId},
            {QStringLiteral("itemName"), item ? item->name : QString()},
            {QStringLiteral("accountId"), o.accountId},
            {QStringLiteral("description"), o.description},
            {QStringLiteral("customerId"), o.customerId},
            {QStringLiteral("customerName"), job ? job->displayName : QString()},
            {QStringLiteral("ordered"), o.ordered.toString()},
            {QStringLiteral("received"), o.received.toString()},
            {QStringLiteral("remaining"), o.remaining().toString()},
            {QStringLiteral("rate"), o.rate.isZero() ? QString() : money.plain(o.rate.minor())},
            {QStringLiteral("rateText"), o.rate.isZero() ? QString() : money.text(o.rate.minor())},
            {QStringLiteral("amountText"), money.text(o.amount.minor())},
            {QStringLiteral("remainingAmount"), remaining},
            {QStringLiteral("remainingText"), money.text(remaining)},
            {QStringLiteral("closed"), o.closed}};
}

// A bill line as the document editor holds it (document() line keys, and
// the order it receives for the line's badge).
QVariantMap editorLine(CompanyFile &file, const DocumentLine &l, const MoneyFormat &money)
{
    return {{QStringLiteral("orderNumber"), PurchaseOrders(file).orderNumberOf(l.poLineId)},
            {QStringLiteral("itemId"), l.itemId},
            {QStringLiteral("accountId"), l.accountId},
            {QStringLiteral("description"), l.description},
            {QStringLiteral("quantity"), l.quantity.toString()},
            {QStringLiteral("rate"), l.unitPrice.isZero() ? QString() : money.plain(l.unitPrice.minor())},
            {QStringLiteral("amount"), l.unitPrice.isZero() ? money.plain(l.amount.minor()) : QString()},
            {QStringLiteral("customerId"), l.partyId},
            {QStringLiteral("classId"), l.classId},
            {QStringLiteral("poLineId"), l.poLineId},
            {QStringLiteral("timeEntryId"), l.timeEntryId},
            {QStringLiteral("taxable"), l.taxable}};
}

} // namespace

QVariantList BooksService::orderLines(qint64 orderId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    Lists lists(*c);
    const MoneyFormat money = moneyFormat();
    for (const OrderLine &o : PurchaseOrders(*c).lines(orderId)) {
        out << orderLineMap(o, lists, money);
    }
    return out;
}

QVariantList BooksService::openOrderLines(qint64 vendorId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr || vendorId == 0) {
        return out;
    }
    Lists lists(*c);
    const MoneyFormat money = moneyFormat();
    for (const OrderLine &o : PurchaseOrders(*c).openLines(vendorId)) {
        out << orderLineMap(o, lists, money);
    }
    return out;
}

QVariantMap BooksService::receiveOrderLines(const QVariantList &receipts) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QList<OrderReceipt> chosen;
    for (const QVariant &v : receipts) {
        const QVariantMap r = v.toMap();
        const QString text = r.value(QStringLiteral("quantity")).toString().trimmed();
        if (text.isEmpty() || text == QLatin1String("0")) {
            continue;
        }
        const std::optional<Decimal> quantity = Decimal::parse(text);
        if (!quantity || text.endsWith(QLatin1Char('%'))) {
            return Format::failure(tr("\"%1\" is not a quantity.").arg(text));
        }
        chosen << OrderReceipt{Format::id(r.value(QStringLiteral("lineId"))), *quantity};
    }
    if (chosen.isEmpty()) {
        return Format::failure(tr("Choose what was received."));
    }
    QString error;
    const std::optional<QList<DocumentLine>> lines = PurchaseOrders(*c).billLines(chosen, &error);
    if (!lines) {
        return Format::failure(error);
    }
    const MoneyFormat money = moneyFormat();
    QVariantList out;
    for (const DocumentLine &l : *lines) {
        out << editorLine(*c, l, money);
    }
    QVariantMap result = Format::success();
    result.insert(QStringLiteral("lines"), out);
    return result;
}

QVariantMap BooksService::billFromOrder(qint64 orderId) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const std::optional<BusinessDocument> order = Documents(*c).load(orderId);
    if (!order || order->kind != DocKind::PurchaseOrder || order->state == DocState::Void) {
        return Format::failure(tr("Choose a purchase order."));
    }
    const QList<DocumentLine> lines = PurchaseOrders(*c).remainingAsBillLines(orderId);
    if (lines.isEmpty()) {
        return Format::failure(tr("%1 has nothing left to receive.").arg(order->number));
    }
    const MoneyFormat money = moneyFormat();
    QVariantList editorLines;
    for (const DocumentLine &l : lines) {
        editorLines << editorLine(*c, l, money);
    }
    QVariantMap result = Format::success(orderId);
    result.insert(QStringLiteral("draft"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("bill")},
                                                       {QStringLiteral("partyId"), order->partyId},
                                                       {QStringLiteral("date"), today()},
                                                       {QStringLiteral("memo"), tr("Received on %1").arg(order->number)},
                                                       {QStringLiteral("lines"), editorLines}});
    return result;
}

QVariantMap BooksService::setOrderClosed(qint64 orderId, bool closed)
{
    return file() != nullptr ? Format::result(PurchaseOrders(*file()).setClosed(orderId, closed)) : noCompany();
}

QVariantMap BooksService::setOrderLineClosed(qint64 lineId, bool closed)
{
    return file() != nullptr ? Format::result(PurchaseOrders(*file()).setLineClosed(lineId, closed)) : noCompany();
}

} // namespace QindaOffice::Apps::Books
