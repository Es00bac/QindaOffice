// SPDX-License-Identifier: GPL-3.0-or-later
// Purchase orders (M68, D-077): an order never posts; bills receive it in
// part and in full; voiding or editing a bill puts the quantities back;
// lines and orders close by hand; an order's received lines are kept when
// it is edited; the Open Purchase Orders report, worked by hand.
#include "books_fixture.h"

#include "purchase_orders.h"
#include "reports.h"
#include "sql_util.h"

using namespace BooksTest;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

Decimal dec(const char *text)
{
    return *Decimal::parse(QString::fromLatin1(text));
}

struct Studio : TestCompany {
    qint64 copperleaf = 0;
    qint64 pixel = 0;
    qint64 harbor = 0;
    qint64 cover = 0;
    qint64 ink = 0;

    Studio()
    {
        copperleaf = vendor(str("Copperleaf Paper"), account(str("5000")));
        pixel = vendor(str("Pixel Print Works"), account(str("6400")));
        harbor = customer(str("Harbor Coffee"));
        cover = purchaseItem(str("Cover stock"), 2500, account(str("5000")));
        ink = purchaseItem(str("Ink cartridge"), 1250, account(str("6400")));
    }
    qint64 purchaseItem(const QString &name, qint64 costCents, qint64 expense)
    {
        Item i;
        i.name = name;
        i.kind = ItemKind::NonInventory;
        i.cost = usd(costCents);
        i.expenseAccountId = expense;
        return Lists(f()).createItem(i).id;
    }
    // 10 cover stock @ 25.00 (for Harbor), 4 ink @ 12.50, delivery 20.00.
    BusinessDocument order(qint64 vendorId, const QDate &date = QDate(2026, 3, 2))
    {
        BusinessDocument po;
        po.partyId = vendorId;
        po.date = date;
        po.dueDate = QDate(2026, 3, 16);
        po.shipTo = str("Lantern Studio\n12 Ember Lane");
        po.message = str("Please ship by ground");
        po.memo = str("for the spring menus");
        DocumentLine stock = itemLine(cover, str("10"));
        stock.partyId = harbor;
        DocumentLine cartridges = itemLine(ink, str("4"));
        DocumentLine delivery = accountLine(account(str("6000")), 2000, str("Delivery"));
        po.lines = {stock, cartridges, delivery};
        return po;
    }
    OrderLine line(qint64 orderId, int index) { return PurchaseOrders(f()).lines(orderId).value(index); }
    qint64 bill(const QList<DocumentLine> &lines, const QDate &date = QDate(2026, 3, 10), qint64 vendorId = 0)
    {
        BusinessDocument b;
        b.partyId = vendorId != 0 ? vendorId : copperleaf;
        b.date = date;
        b.lines = lines;
        const Result r = Purchases(f()).createBill(b);
        if (!r.ok) {
            qWarning("bill: %s", qPrintable(r.error));
        }
        return r.id;
    }
};

} // namespace

class TstBooksOrders : public QObject {
    Q_OBJECT
private slots:
    void anOrderNeverPostsAndIsNumbered()
    {
        Studio c;
        PurchaseOrders orders(c.f());
        const qint64 linesBefore = Sql::scalarInt(c.f().database(), str("SELECT COUNT(*) FROM journal_lines"));
        const Result r = orders.create(c.order(c.copperleaf));
        QVERIFY2(r.ok, qPrintable(r.error));
        const BusinessDocument po = *Documents(c.f()).load(r.id);
        QCOMPARE(po.kind, DocKind::PurchaseOrder);
        QCOMPARE(po.number, str("PO-1001"));
        QCOMPARE(po.total.minor(), 32000); // 250.00 + 50.00 + 20.00
        QCOMPARE(po.txnId, 0);
        QCOMPARE(po.shipTo, str("Lantern Studio\n12 Ember Lane"));
        QCOMPARE(po.dueDate, QDate(2026, 3, 16)); // the expected date
        QCOMPARE(po.lines.at(0).unitPrice.minor(), 2500); // the item's cost
        QCOMPARE(po.lines.at(0).accountId, c.account(str("5000")));
        QCOMPARE(po.lines.at(0).partyId, c.harbor);       // customer:job
        QCOMPARE(Sql::scalarInt(c.f().database(), str("SELECT COUNT(*) FROM journal_lines")), linesBefore);
        QCOMPARE(Documents(c.f()).status(po, QDate(2026, 3, 31)), DocumentStatus::Open);
        // Numbers: a typed duplicate is refused, the counter moves on.
        BusinessDocument dup = c.order(c.pixel);
        dup.number = str("PO-1001");
        QCOMPARE(orders.create(dup).code, ErrorCode::Duplicate);
        QCOMPARE(Documents(c.f()).load(orders.create(c.order(c.pixel)).id)->number, str("PO-1002"));
        // A customer is no vendor; a line needs a quantity above zero.
        QCOMPARE(orders.create(c.order(c.harbor)).code, ErrorCode::UnknownParty);
        BusinessDocument zero = c.order(c.pixel);
        zero.lines[1].quantity = Decimal();
        QCOMPARE(orders.create(zero).code, ErrorCode::Invalid);
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    void billsReceiveInPartThenTheRestAndVoidingPutsItBack()
    {
        Studio c;
        PurchaseOrders orders(c.f());
        const qint64 po = orders.create(c.order(c.copperleaf)).id;
        QCOMPARE(orders.openLines(c.copperleaf).size(), 3);
        QCOMPARE(orders.openLines(c.pixel).size(), 0);
        const qint64 stockLine = c.line(po, 0).lineId;
        const qint64 deliveryLine = c.line(po, 2).lineId;

        // Receive 6 of 10 cover stock and the delivery.
        QString error;
        const std::optional<QList<DocumentLine>> first =
            orders.billLines({{stockLine, dec("6")}, {deliveryLine, dec("1")}}, &error);
        QVERIFY2(first, qPrintable(error));
        QCOMPARE(first->at(0).unitPrice.minor(), 2500);
        QCOMPARE(first->at(0).partyId, c.harbor);
        QCOMPARE(first->at(1).amount.minor(), 2000);
        const qint64 bill1 = c.bill(*first);
        QVERIFY(bill1 != 0);
        const BusinessDocument b1 = *Documents(c.f()).load(bill1);
        QCOMPARE(b1.total.minor(), 17000);
        QCOMPARE(b1.lines.at(0).poLineId, stockLine); // the bill line remembers its order line
        QCOMPARE(c.roleBalance(SystemRole::AccountsPayable), 17000);
        QCOMPARE(c.line(po, 0).received, dec("6"));
        QCOMPARE(c.line(po, 0).remaining(), dec("4"));
        const BusinessDocument order = *Documents(c.f()).load(po);
        QCOMPARE(orders.status(order), DocumentStatus::PartiallyReceived);
        QCOMPARE(orders.remainingAmount(po).minor(), 15000); // 4 × 25.00 + 4 × 12.50

        // More than remains is refused, by the dialog and by the bill.
        QVERIFY(!orders.billLines({{stockLine, dec("5")}}, &error));
        QVERIFY2(error.contains(str("at most 4")), qPrintable(error));
        DocumentLine tooMuch = first->at(0);
        tooMuch.quantity = dec("5");
        BusinessDocument over;
        over.partyId = c.copperleaf;
        over.date = QDate(2026, 3, 11);
        over.lines = {tooMuch};
        QCOMPARE(Purchases(c.f()).createBill(over).code, ErrorCode::OverApplied);
        // Another vendor's bill cannot receive this order.
        over.partyId = c.pixel;
        over.lines.first().quantity = dec("1");
        QCOMPARE(Purchases(c.f()).createBill(over).code, ErrorCode::Invalid);

        // Create Bill receives the rest: the order closes by itself.
        const QList<DocumentLine> rest = orders.remainingAsBillLines(po);
        QCOMPARE(rest.size(), 2);
        QCOMPARE(rest.at(0).quantity, dec("4"));
        const qint64 bill2 = c.bill(rest, QDate(2026, 3, 20));
        QCOMPARE(Documents(c.f()).load(bill2)->total.minor(), 15000);
        QCOMPARE(orders.status(*Documents(c.f()).load(po)), DocumentStatus::Closed);
        QVERIFY(orders.openLines(c.copperleaf).isEmpty());

        // Voiding the second bill puts its quantities back.
        QVERIFY(Ledger(c.f()).voidDocument(bill2, str("wrong vendor invoice")).ok);
        QCOMPARE(orders.status(*Documents(c.f()).load(po)), DocumentStatus::PartiallyReceived);
        QCOMPARE(c.line(po, 0).remaining(), dec("4"));
        QCOMPARE(orders.openLines(c.copperleaf).size(), 2);

        // Editing the first bill so the stock line is gone puts its 6 back.
        BusinessDocument edited = *Documents(c.f()).load(bill1);
        edited.lines.removeFirst();
        QVERIFY(Purchases(c.f()).updateBill(edited, str("stock billed separately")).ok);
        QCOMPARE(c.line(po, 0).received, Decimal());
        QCOMPARE(c.line(po, 2).received, dec("1"));
        QCOMPARE(orders.remainingAmount(po).minor(), 30000); // 10 × 25.00 + 4 × 12.50
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    void linesAndOrdersCloseByHand()
    {
        Studio c;
        PurchaseOrders orders(c.f());
        const qint64 po = orders.create(c.order(c.copperleaf)).id;
        const qint64 inkLine = c.line(po, 1).lineId;
        QVERIFY(orders.setLineClosed(inkLine, true).ok);
        QVERIFY(c.line(po, 1).closed);
        QCOMPARE(orders.openLines(c.copperleaf).size(), 2);
        QCOMPARE(orders.remainingAmount(po).minor(), 27000); // the ink no longer counts
        QString error;
        QVERIFY(!orders.billLines({{inkLine, dec("1")}}, &error));
        QVERIFY(error.contains(str("closed")));
        // The whole order, closed and reopened.
        QVERIFY(orders.setClosed(po, true).ok);
        QCOMPARE(orders.status(*Documents(c.f()).load(po)), DocumentStatus::Closed);
        QVERIFY(orders.openLines(c.copperleaf).isEmpty());
        QVERIFY(orders.setClosed(po, false).ok);
        QCOMPARE(orders.status(*Documents(c.f()).load(po)), DocumentStatus::Open);
        QCOMPARE(Ledger(c.f()).auditTrail(str("document"), po).size(), 4); // create, close-line, close, reopen
    }

    void editingKeepsReceivedLinesLinked()
    {
        Studio c;
        PurchaseOrders orders(c.f());
        const qint64 po = orders.create(c.order(c.copperleaf)).id;
        const qint64 stockLine = c.line(po, 0).lineId;
        const qint64 bill = c.bill(*orders.billLines({{stockLine, dec("6")}}));
        QVERIFY(bill != 0);
        BusinessDocument edit = *Documents(c.f()).load(po);
        // Below what was received: refused. Removing the line: refused.
        edit.lines[0].quantity = dec("5");
        QCOMPARE(orders.update(edit).code, ErrorCode::Invalid);
        BusinessDocument removed = *Documents(c.f()).load(po);
        removed.lines.removeFirst();
        QCOMPARE(orders.update(removed).code, ErrorCode::Invalid);
        // More stock and a new line: the received line keeps its id and receipts.
        edit.lines[0].quantity = dec("12");
        DocumentLine extra = accountLine(c.account(str("6000")), 3500, str("Rush fee"));
        edit.lines.prepend(extra);
        QVERIFY(orders.update(edit, str("more stock")).ok);
        const QList<OrderLine> after = orders.lines(po);
        QCOMPARE(after.size(), 4);
        QCOMPARE(after.at(1).lineId, stockLine);
        QCOMPARE(after.at(1).received, dec("6"));
        QCOMPARE(after.at(1).remaining(), dec("6"));
        QCOMPARE(Documents(c.f()).load(bill)->lines.first().poLineId, stockLine);
        QCOMPARE(Documents(c.f()).load(po)->total.minor(), 40500); // 35.00 + 300.00 + 50.00 + 20.00
        // An order bills have received cannot be voided; a fresh one can.
        QCOMPARE(Ledger(c.f()).voidDocument(po, str("cancelled")).code, ErrorCode::Invalid);
        const qint64 fresh = orders.create(c.order(c.pixel)).id;
        QVERIFY(Ledger(c.f()).voidDocument(fresh, str("cancelled")).ok);
        QCOMPARE(orders.status(*Documents(c.f()).load(fresh)), DocumentStatus::Void);
        QCOMPARE(orders.update(*Documents(c.f()).load(fresh)).code, ErrorCode::AlreadyVoided);
    }

    // Worked by hand: Copperleaf's PO-1001 320.00 with 6 cover stock and the
    // delivery received on Mar 10 (open 4 × 25.00 + 4 × 12.50 = 150.00);
    // Pixel's PO-1002 2 × 100.00, nothing received (open 200.00); PO-1003
    // received in full and PO-1004 closed by hand are left out.
    void openPurchaseOrdersReportWorkedByHand()
    {
        Studio c;
        PurchaseOrders orders(c.f());
        const qint64 a = orders.create(c.order(c.copperleaf)).id;
        BusinessDocument b;
        b.partyId = c.pixel;
        b.date = QDate(2026, 3, 3);
        b.dueDate = QDate(2026, 3, 24);
        DocumentLine posters = accountLine(c.account(str("6400")), 0, str("Posters"));
        posters.quantity = dec("2");
        posters.unitPrice = usd(10000);
        b.lines = {posters};
        const qint64 bId = orders.create(b).id;
        BusinessDocument full = b;
        full.date = QDate(2026, 3, 4);
        const qint64 cId = orders.create(full).id;
        const qint64 dId = orders.create(c.order(c.copperleaf, QDate(2026, 3, 5))).id;
        QVERIFY(c.bill(*orders.billLines({{c.line(a, 0).lineId, dec("6")}, {c.line(a, 2).lineId, dec("1")}})) != 0);
        QVERIFY(c.bill(orders.remainingAsBillLines(cId), QDate(2026, 3, 12), c.pixel) != 0);
        QVERIFY(orders.setClosed(dId, true).ok);

        const ReportTable t = Reports(c.f()).openPurchaseOrders(QDate(2026, 3, 31));
        QCOMPARE(t.columns.size(), 5);
        QCOMPARE(t.text(str("party:%1").arg(c.copperleaf), str("label")), str("Copperleaf Paper"));
        QCOMPARE(t.money(str("doc:%1").arg(a), str("amount")), 32000);
        QCOMPARE(t.money(str("doc:%1").arg(a), str("open")), 15000);
        QCOMPARE(t.money(str("doc:%1").arg(bId), str("open")), 20000);
        QVERIFY(t.row(str("doc:%1").arg(cId)) == nullptr);
        QVERIFY(t.row(str("doc:%1").arg(dId)) == nullptr);
        QCOMPARE(t.money(str("subtotal:party:%1").arg(c.copperleaf), str("open")), 15000);
        QCOMPARE(t.money(str("subtotal:party:%1").arg(c.pixel), str("amount")), 20000);
        QCOMPARE(t.money(str("total"), str("amount")), 52000);
        QCOMPARE(t.money(str("total"), str("open")), 35000);
        QCOMPARE(t.row(str("doc:%1").arg(a))->cells.at(2).date, QDate(2026, 3, 16)); // expected
        // As of Mar 5 the Mar 10 bill had not come: PO-1001 is open in full,
        // and PO-1003 (billed Mar 12) still counts.
        const ReportTable early = Reports(c.f()).openPurchaseOrders(QDate(2026, 3, 5));
        QCOMPARE(early.money(str("doc:%1").arg(a), str("open")), 32000);
        QCOMPARE(early.money(str("doc:%1").arg(cId), str("open")), 20000);
        QCOMPARE(early.money(str("total"), str("open")), 72000);
    }
};

QTEST_GUILESS_MAIN(TstBooksOrders)
#include "tst_books_orders.moc"
