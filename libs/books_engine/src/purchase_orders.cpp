// SPDX-License-Identifier: GPL-3.0-or-later
// Purchase orders (purchase_orders.h): create and edit, close by hand, what
// each line received (derived from live bills), receiving into bill lines.
#include "purchase_orders.h"

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "sql_util.h"

#include <QHash>
#include <QJsonDocument>
#include <QSqlQuery>

#include <algorithm>

namespace QindaOffice::Books {

namespace {

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Decimal plus(const Decimal &a, const Decimal &b)
{
    return Decimal::sum(a, b).value_or(a);
}

// Live bills' quantities per purchase-order line: `where` selects the
// lines ("dl.po_line_id IN (…)"), `asOf` limits the bills by date and
// `exceptBill` leaves one bill out (its own edit).
QHash<qint64, Decimal> receivedBy(CompanyFile &file, const QString &where, const QVariantList &binds,
                                  const QDate &asOf = {}, qint64 exceptBill = 0)
{
    QHash<qint64, Decimal> out;
    QVariantList all = binds;
    all << int(DocKind::Bill) << int(DocState::Draft) << int(DocState::Void) << Sql::isoDate(asOf)
        << Sql::isoDate(asOf) << exceptBill;
    QSqlQuery q(file.database());
    Sql::run(q, QStringLiteral("SELECT dl.po_line_id, dl.quantity FROM document_lines dl JOIN documents d"
                               " ON d.id = dl.doc_id WHERE ") + where
                    + QStringLiteral(" AND d.kind = ? AND d.state NOT IN (?, ?) AND (? = '' OR d.date <= ?)"
                                     " AND d.id <> ?"),
             all);
    while (q.next()) {
        const qint64 line = q.value(0).toLongLong();
        const Decimal quantity = Decimal::parse(q.value(1).toString()).value_or(Decimal(1, 0));
        out.insert(line, plus(out.value(line), quantity));
    }
    return out;
}

// Aligns two decimals to one scale for a ratio (amount × part ÷ whole).
std::pair<qint64, qint64> ratio(const Decimal &part, const Decimal &whole)
{
    const int scale = qMax(part.scale(), whole.scale());
    return {part.mantissa() * pow10i(scale - part.scale()), whole.mantissa() * pow10i(scale - whole.scale())};
}

} // namespace

Decimal OrderLine::remaining() const
{
    const Decimal left = Decimal::difference(ordered, received).value_or(Decimal());
    return left.isNegative() ? Decimal() : left;
}

Money OrderLine::remainingAmount() const
{
    const Decimal left = remaining();
    if (!rate.isZero()) {
        return rate.multiplied(left, RoundingMode::HalfUp);
    }
    if (ordered.isZero() || ordered.isNegative()) {
        return Money::zero(amount.currency());
    }
    const auto [part, whole] = ratio(left, ordered);
    return amount.scaled(part, whole, RoundingMode::HalfUp);
}

PurchaseOrders::PurchaseOrders(CompanyFile &file)
    : m_file(file)
{
}

Result PurchaseOrders::validate(BusinessDocument &order) const
{
    Lists lists(m_file);
    const std::optional<Party> vendor = lists.party(order.partyId);
    if (!vendor || vendor->kind != PartyKind::Vendor) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("choose a vendor"));
    }
    if (!order.date.isValid() || order.lines.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a date and at least one line are required"));
    }
    order.discount = Discount::none();
    order.taxRateId = 0;
    Documents documents(m_file);
    documents.fillFromItems(order, true);
    ChartOfAccounts chart(m_file);
    for (DocumentLine &line : order.lines) {
        if (line.accountId == 0) {
            line.accountId = vendor->defaultAccountId;
        }
        if (line.accountId == 0 || !chart.account(line.accountId)) {
            return Result::failure(ErrorCode::UnknownAccount,
                                   QStringLiteral("line \"%1\" needs an item with an expense account, or a category")
                                       .arg(line.description));
        }
        if (line.quantity.isZero() || line.quantity.isNegative()) {
            return Result::failure(ErrorCode::Invalid,
                                   QStringLiteral("line \"%1\" needs a quantity above zero").arg(line.description));
        }
        if (line.partyId != 0) {
            const std::optional<Party> job = lists.party(line.partyId);
            if (!job || job->kind != PartyKind::Customer) {
                return Result::failure(ErrorCode::UnknownParty,
                                       QStringLiteral("line \"%1\": the customer:job is not a customer").arg(line.description));
            }
        }
        if (!line.amount.sameCurrency(m_file.money(0)) || !line.unitPrice.sameCurrency(m_file.money(0))) {
            return Result::failure(ErrorCode::CurrencyMismatch, QStringLiteral("amounts must be in %1").arg(m_file.currency()));
        }
        line.taxable = false;
        line.taxableFromItem = false;
    }
    if (documents.finalize(order).total.isNegative()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("the total is negative"));
    }
    return Result::success();
}

Result PurchaseOrders::write(BusinessDocument &order, bool isNew, const QString &reason)
{
    if (const Result valid = validate(order); !valid.ok) {
        return valid;
    }
    Documents documents(m_file);
    const std::optional<BusinessDocument> before = isNew ? std::nullopt : documents.load(order.id);
    if (!isNew) {
        // A line with receipts stays, and stays ordered at least as far as
        // received; the vendor stays once anything was received.
        for (const OrderLine &old : lines(order.id)) {
            if (!old.received.isZero() && before && before->partyId != order.partyId) {
                return Result::failure(ErrorCode::Invalid,
                                       QStringLiteral("bills from %1 received this order; the vendor cannot change")
                                           .arg(Lists(m_file).party(before->partyId)->displayName));
            }
            if (old.received.isZero()) {
                continue;
            }
            const auto kept = std::find_if(order.lines.cbegin(), order.lines.cend(),
                                           [&old](const DocumentLine &l) { return l.id == old.lineId; });
            if (kept == order.lines.cend()) {
                return Result::failure(ErrorCode::Invalid,
                                       QStringLiteral("\"%1\" has been received on a bill and cannot be removed; close the "
                                                      "line instead").arg(old.description));
            }
            if (Decimal::compare(kept->quantity, old.received) < 0) {
                return Result::failure(ErrorCode::Invalid,
                                       QStringLiteral("\"%1\": %2 received already; the order cannot be for less")
                                           .arg(old.description, old.received.toString()));
            }
        }
    }
    SqlTransaction txn(m_file);
    if (order.number.isEmpty()) {
        order.number = m_file.takeNextNumber(DocKind::PurchaseOrder);
    } else if (Sql::scalarInt(m_file.database(),
                              QStringLiteral("SELECT COUNT(*) FROM documents WHERE kind = ? AND number = ? AND id <> ?"),
                              {int(DocKind::PurchaseOrder), order.number, order.id})
               > 0) {
        return Result::failure(ErrorCode::Duplicate,
                               QStringLiteral("purchase order %1 already exists").arg(order.number));
    }
    if (const Result w = documents.write(order); !w.ok) {
        return w;
    }
    m_file.audit(isNew ? QStringLiteral("create") : QStringLiteral("edit"), QStringLiteral("document"), order.id,
                 before ? compact(documents.toJson(*before)) : QString(), compact(documents.toJson(order)), reason);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(order.id);
}

Result PurchaseOrders::create(BusinessDocument order)
{
    order.kind = DocKind::PurchaseOrder;
    order.id = 0;
    order.state = DocState::Open;
    order.txnId = 0;
    order.toPrint = false;
    for (DocumentLine &line : order.lines) {
        line.id = 0;
        line.poLineId = 0;
        line.timeEntryId = 0;
    }
    return write(order, true, {});
}

Result PurchaseOrders::update(BusinessDocument order, const QString &reason)
{
    const std::optional<BusinessDocument> before = Documents(m_file).load(order.id);
    if (!before || before->kind != DocKind::PurchaseOrder) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such purchase order"));
    }
    if (before->state == DocState::Void) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("a void purchase order cannot be edited"));
    }
    order.kind = DocKind::PurchaseOrder;
    order.state = before->state;
    order.number = order.number.isEmpty() ? before->number : order.number;
    return write(order, false, reason);
}

Result PurchaseOrders::setClosed(qint64 orderId, bool closed)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> order = documents.load(orderId);
    if (!order || order->kind != DocKind::PurchaseOrder) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such purchase order"));
    }
    if (order->state == DocState::Void) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("the purchase order is void"));
    }
    SqlTransaction txn(m_file);
    if (const Result r = documents.setState(orderId, closed ? DocState::Closed : DocState::Open); !r.ok) {
        return r;
    }
    m_file.audit(closed ? QStringLiteral("close") : QStringLiteral("reopen"), QStringLiteral("document"), orderId, {}, {});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(orderId);
}

Result PurchaseOrders::setLineClosed(qint64 lineId, bool closed)
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT d.id, d.kind, d.state FROM document_lines dl JOIN documents d ON d.id = dl.doc_id"
                               " WHERE dl.id = ?"),
             {lineId});
    if (!q.next() || DocKind(q.value(1).toInt()) != DocKind::PurchaseOrder) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such purchase-order line"));
    }
    const qint64 orderId = q.value(0).toLongLong();
    const bool isVoid = DocState(q.value(2).toInt()) == DocState::Void;
    q.finish();
    if (isVoid) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("the purchase order is void"));
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(), QStringLiteral("UPDATE document_lines SET closed = ? WHERE id = ?"),
                   {closed ? 1 : 0, lineId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(closed ? QStringLiteral("close-line") : QStringLiteral("reopen-line"), QStringLiteral("document"),
                 orderId, {}, QStringLiteral("{\"line\":%1}").arg(lineId));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(orderId);
}

QList<OrderLine> PurchaseOrders::lines(qint64 orderId, const QDate &asOf) const
{
    QList<OrderLine> out;
    const std::optional<BusinessDocument> order = Documents(m_file).load(orderId);
    if (!order || order->kind != DocKind::PurchaseOrder) {
        return out;
    }
    const QHash<qint64, Decimal> received =
        receivedBy(m_file, QStringLiteral("dl.po_line_id IN (SELECT id FROM document_lines WHERE doc_id = ?)"),
                   {orderId}, asOf);
    const DocumentTotals totals = Documents(m_file).computeTotals(*order);
    for (qsizetype i = 0; i < order->lines.size(); ++i) {
        const DocumentLine &l = order->lines.at(i);
        OrderLine o;
        o.lineId = l.id;
        o.orderId = orderId;
        o.orderNumber = order->number;
        o.orderDate = order->date;
        o.expected = order->dueDate;
        o.vendorId = order->partyId;
        o.itemId = l.itemId;
        o.accountId = l.accountId;
        o.customerId = l.partyId;
        o.classId = l.classId;
        o.description = l.description;
        o.ordered = l.quantity;
        o.received = received.value(l.id);
        o.rate = l.unitPrice;
        o.amount = i < totals.lineAmounts.size() ? totals.lineAmounts.at(i) : l.amount;
        o.closed = l.closed || order->state == DocState::Closed;
        out << o;
    }
    return out;
}

QList<OrderLine> PurchaseOrders::openLines(qint64 vendorId) const
{
    QList<qint64> orders;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM documents WHERE kind = ? AND party_id = ? AND state = ?"
                                   " ORDER BY date, id"),
                 {int(DocKind::PurchaseOrder), vendorId, int(DocState::Open)});
        while (q.next()) {
            orders << q.value(0).toLongLong();
        }
    }
    QList<OrderLine> out;
    for (qint64 id : std::as_const(orders)) {
        for (const OrderLine &line : lines(id)) {
            if (line.receivable()) {
                out << line;
            }
        }
    }
    return out;
}

DocumentStatus PurchaseOrders::status(const BusinessDocument &order, const QDate &asOf) const
{
    if (order.state == DocState::Void) {
        return DocumentStatus::Void;
    }
    if (order.state == DocState::Closed) {
        return DocumentStatus::Closed;
    }
    bool anyReceived = false;
    bool allDone = true;
    for (const OrderLine &line : lines(order.id, asOf)) {
        anyReceived = anyReceived || !line.received.isZero();
        allDone = allDone && (line.closed || line.remaining().isZero());
    }
    if (allDone) {
        return DocumentStatus::Closed;
    }
    return anyReceived ? DocumentStatus::PartiallyReceived : DocumentStatus::Open;
}

Money PurchaseOrders::remainingAmount(qint64 orderId, const QDate &asOf) const
{
    Money total = m_file.money(0);
    for (const OrderLine &line : lines(orderId, asOf)) {
        if (!line.closed) {
            total += line.remainingAmount();
        }
    }
    return total;
}

QString PurchaseOrders::orderNumberOf(qint64 lineId) const
{
    return Sql::scalar(m_file.database(),
                       QStringLiteral("SELECT d.number FROM document_lines dl JOIN documents d ON d.id = dl.doc_id"
                                      " WHERE dl.id = ? AND d.kind = ?"),
                       {lineId, int(DocKind::PurchaseOrder)})
        .toString();
}

std::optional<QList<DocumentLine>> PurchaseOrders::billLines(const QList<OrderReceipt> &receipts, QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    QList<DocumentLine> out;
    qint64 vendor = 0;
    for (const OrderReceipt &r : receipts) {
        const qint64 orderId = Sql::scalarInt(m_file.database(),
                                              QStringLiteral("SELECT doc_id FROM document_lines WHERE id = ?"), {r.lineId});
        const QList<OrderLine> all = lines(orderId);
        const auto it = std::find_if(all.cbegin(), all.cend(), [&r](const OrderLine &l) { return l.lineId == r.lineId; });
        if (it == all.cend()) {
            return fail(QStringLiteral("that purchase-order line no longer exists"));
        }
        if (vendor != 0 && it->vendorId != vendor) {
            return fail(QStringLiteral("one bill receives from one vendor's orders"));
        }
        vendor = it->vendorId;
        if (it->closed) {
            return fail(QStringLiteral("%1 \"%2\" is closed").arg(it->orderNumber, it->description));
        }
        if (r.quantity.isZero() || r.quantity.isNegative() || Decimal::compare(r.quantity, it->remaining()) > 0) {
            return fail(QStringLiteral("%1 \"%2\": receive more than 0 and at most %3")
                            .arg(it->orderNumber, it->description, it->remaining().toString()));
        }
        DocumentLine line;
        line.itemId = it->itemId;
        line.accountId = it->accountId;
        line.description = it->description;
        line.quantity = r.quantity;
        line.unitPrice = it->rate;
        line.amount = m_file.money(0);
        if (it->rate.isZero()) {
            const auto [part, whole] = ratio(r.quantity, it->ordered);
            line.amount = it->amount.scaled(part, whole, RoundingMode::HalfUp);
        }
        line.classId = it->classId;
        line.partyId = it->customerId;
        line.poLineId = it->lineId;
        line.taxable = false;
        line.taxableFromItem = false;
        out << line;
    }
    return out;
}

QList<DocumentLine> PurchaseOrders::remainingAsBillLines(qint64 orderId) const
{
    QList<OrderReceipt> receipts;
    for (const OrderLine &line : lines(orderId)) {
        if (line.receivable()) {
            receipts << OrderReceipt{line.lineId, line.remaining()};
        }
    }
    return billLines(receipts).value_or(QList<DocumentLine>());
}

Result PurchaseOrders::checkReceipts(const BusinessDocument &bill) const
{
    QHash<qint64, Decimal> wanted;
    for (const DocumentLine &line : bill.lines) {
        if (line.poLineId == 0) {
            continue;
        }
        if (line.quantity.isZero() || line.quantity.isNegative()) {
            return Result::failure(ErrorCode::Invalid,
                                   QStringLiteral("\"%1\" receives a quantity above zero").arg(line.description));
        }
        wanted.insert(line.poLineId, plus(wanted.value(line.poLineId), line.quantity));
    }
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT d.kind, d.state, d.party_id, dl.closed, dl.quantity, d.number, dl.description"
                                   " FROM document_lines dl JOIN documents d ON d.id = dl.doc_id WHERE dl.id = ?"),
                 {it.key()});
        if (!q.next() || DocKind(q.value(0).toInt()) != DocKind::PurchaseOrder
            || DocState(q.value(1).toInt()) == DocState::Void) {
            return Result::failure(ErrorCode::NotFound, QStringLiteral("a line receives a purchase order that is gone"));
        }
        const QString name = q.value(5).toString() + QStringLiteral(" \"") + q.value(6).toString() + QLatin1Char('"');
        if (q.value(2).toLongLong() != bill.partyId) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("%1 is another vendor's order").arg(name));
        }
        const bool closed = q.value(3).toBool() || DocState(q.value(1).toInt()) == DocState::Closed;
        const Decimal ordered = Decimal::parse(q.value(4).toString()).value_or(Decimal(1, 0));
        q.finish();
        const QString one = QStringLiteral("dl.po_line_id = ?");
        const Decimal elsewhere = receivedBy(m_file, one, {it.key()}, {}, bill.id).value(it.key());
        const bool hadIt = bill.id != 0
            && Sql::scalarInt(m_file.database(),
                              QStringLiteral("SELECT COUNT(*) FROM document_lines WHERE doc_id = ? AND po_line_id = ?"),
                              {bill.id, it.key()})
                > 0;
        if (closed && !hadIt) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("%1 is closed").arg(name));
        }
        const Decimal left = Decimal::difference(ordered, elsewhere).value_or(Decimal());
        if (Decimal::compare(it.value(), left) > 0) {
            return Result::failure(ErrorCode::OverApplied, QStringLiteral("%1: %2 to receive, only %3 remain")
                                                               .arg(name, it.value().toString(),
                                                                    (left.isNegative() ? Decimal() : left).toString()));
        }
    }
    return Result::success();
}

Result PurchaseOrders::checkVoidable(qint64 orderId) const
{
    const QHash<qint64, Decimal> received =
        receivedBy(m_file, QStringLiteral("dl.po_line_id IN (SELECT id FROM document_lines WHERE doc_id = ?)"), {orderId});
    for (const Decimal &quantity : received) {
        if (!quantity.isZero()) {
            return Result::failure(ErrorCode::Invalid,
                                   QStringLiteral("bills have received items on this purchase order; close it instead"));
        }
    }
    return Result::success();
}

DocumentStatus purchaseOrderStatus(CompanyFile &file, const BusinessDocument &order)
{
    return PurchaseOrders(file).status(order);
}

} // namespace QindaOffice::Books
