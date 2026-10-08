// SPDX-License-Identifier: GPL-3.0-or-later
// IIF import: one checked transaction → the matching QindaBooks document
// (iif_import.h). Each builder returns false when the rows do not have the
// document's shape; the caller then verifies that what DID post equals the
// rows, and falls back to a journal entry otherwise.
#include "iif_import_tx_p.h"

#include "accounts.h"
#include "banking.h"
#include "company_file.h"
#include "documents.h"
#include "lists.h"
#include "purchases.h"
#include "sales.h"

#include <algorithm>

namespace QindaOffice::Books::IifInternal {

namespace {

bool isKind(CompanyFile &file, qint64 partyId, PartyKind kind)
{
    const std::optional<Party> p = Lists(file).party(partyId);
    return p && p->kind == kind;
}

// The SPL rows as document lines; `sign` −1 for sales (a credit is a
// positive line), +1 for purchases and credit memos (a debit is).
QList<DocumentLine> linesOf(const Context &ctx, const TxPlan &plan, int sign)
{
    QList<DocumentLine> out;
    for (qsizetype i = 1; i < plan.rows.size(); ++i) {
        const TxRow &r = plan.rows.at(i);
        DocumentLine l;
        l.accountId = r.accountId;
        l.amount = ctx.file.money(sign * r.amount);
        l.unitPrice = ctx.file.money(0);
        l.description = r.memo;
        l.classId = r.classId;
        // AGENT-GUARD: fillFromItems() prices a line from its item only when
        // the line has no amount; a zero row must not pick up a price.
        l.itemId = r.amount != 0 ? r.itemId : 0;
        l.taxable = false;
        l.taxableFromItem = false;
        out << l;
    }
    return out;
}

BusinessDocument header(const TxPlan &plan, DocKind kind, qint64 party)
{
    BusinessDocument d;
    d.kind = kind;
    d.state = DocState::Open;
    d.partyId = party;
    d.date = plan.date;
    d.dueDate = plan.due;
    d.memo = plan.memo;
    return d;
}

// Oldest first: what `amount` pays of the party's open targets dated on or
// before the date (invoices or bills).
QList<PaymentApplication> oldestFirst(CompanyFile &file, const QList<BusinessDocument> &open, qint64 amount)
{
    QList<BusinessDocument> sorted = open;
    std::stable_sort(sorted.begin(), sorted.end(), [](const BusinessDocument &a, const BusinessDocument &b) {
        return a.date != b.date ? a.date < b.date : a.id < b.id;
    });
    Documents documents(file);
    QList<PaymentApplication> out;
    for (const BusinessDocument &d : std::as_const(sorted)) {
        if (amount <= 0) {
            break;
        }
        const qint64 take = std::min(amount, documents.openBalance(d.id).minor());
        if (take > 0) {
            out << PaymentApplication{d.id, file.money(take)};
            amount -= take;
        }
    }
    return out;
}

qint64 sumOf(const QList<PaymentApplication> &apps)
{
    qint64 total = 0;
    for (const PaymentApplication &a : apps) {
        total += a.amount.minor();
    }
    return total;
}

} // namespace

bool postDocument(Context &ctx, const TxPlan &plan, QList<qint64> *docs)
{
    CompanyFile &file = ctx.file;
    ChartOfAccounts chart(file);
    const qint64 ar = chart.idForRole(SystemRole::AccountsReceivable);
    const qint64 ap = chart.idForRole(SystemRole::AccountsPayable);
    const TxRow &head = plan.rows.first();
    const auto typeOf = [&chart](qint64 id) {
        const std::optional<Account> a = chart.account(id);
        return a ? int(a->type) : -1;
    };
    const QString &t = plan.type;
    Result r;
    if (t == QLatin1String("INVOICE") || t == QLatin1String("CASHSALE")) {
        const bool invoice = t == QLatin1String("INVOICE");
        if (head.amount < 0 || !isKind(file, head.partyId, PartyKind::Customer) || (invoice && head.accountId != ar)
            || (!invoice && head.accountId == ar)) {
            return false;
        }
        BusinessDocument d = header(plan, invoice ? DocKind::Invoice : DocKind::SalesReceipt, head.partyId);
        d.number = plan.number;
        d.accountId = invoice ? 0 : head.accountId;
        d.lines = linesOf(ctx, plan, -1);
        r = invoice ? Sales(file).createInvoice(d) : Sales(file).createSalesReceipt(d);
    } else if (t == QLatin1String("CREDITMEMO")) {
        if (head.accountId != ar || head.amount > 0 || !isKind(file, head.partyId, PartyKind::Customer)) {
            return false;
        }
        BusinessDocument d = header(plan, DocKind::CreditMemo, head.partyId);
        d.number = plan.number;
        d.lines = linesOf(ctx, plan, 1);
        r = Sales(file).createCreditMemo(d);
        if (r.ok && ctx.options.autoApply) {
            // A credit memo goes against the customer's oldest open invoices.
            for (const PaymentApplication &a :
                 oldestFirst(file, Sales(file).openInvoices(head.partyId, plan.date), -head.amount)) {
                if (!Sales(file).applyCredit(r.id, a.docId, a.amount, plan.date).ok) {
                    ctx.report.note(QStringLiteral("Some credit memos could not be applied and remain open credits."));
                }
            }
        }
    } else if (t == QLatin1String("PAYMENT")) {
        const qint64 customer = plan.rows.size() == 2 ? (plan.rows.at(1).partyId != 0 ? plan.rows.at(1).partyId
                                                                                     : head.partyId)
                                                      : 0;
        if (plan.rows.size() != 2 || head.amount <= 0 || plan.rows.at(1).accountId != ar || head.accountId == ar
            || !isKind(file, customer, PartyKind::Customer)) {
            return false;
        }
        ReceivePaymentInput in;
        in.customerId = customer;
        in.date = plan.date;
        in.amount = file.money(head.amount);
        in.depositAccountId = head.accountId;
        in.reference = plan.number;
        in.memo = plan.memo;
        if (ctx.options.autoApply) {
            in.applications = oldestFirst(file, Sales(file).openInvoices(customer, plan.date), head.amount);
        }
        r = Sales(file).receivePayment(in);
    } else if (t == QLatin1String("BILL")) {
        if (head.accountId != ap || head.amount > 0 || !isKind(file, head.partyId, PartyKind::Vendor)) {
            return false;
        }
        BusinessDocument d = header(plan, DocKind::Bill, head.partyId);
        d.reference = plan.number;
        d.lines = linesOf(ctx, plan, 1);
        r = Purchases(file).createBill(d);
    } else if (t.startsWith(QLatin1String("BILLPMT")) || t == QLatin1String("BILLPAYMENT")) {
        const qint64 vendor = plan.rows.size() == 2 ? (plan.rows.at(1).partyId != 0 ? plan.rows.at(1).partyId
                                                                                   : head.partyId)
                                                    : 0;
        if (plan.rows.size() != 2 || head.amount >= 0 || plan.rows.at(1).accountId != ap || !ctx.options.autoApply
            || !isKind(file, vendor, PartyKind::Vendor)) {
            return false;
        }
        const QList<PaymentApplication> apps =
            oldestFirst(file, Purchases(file).unpaidBills(vendor, plan.date), -head.amount);
        if (sumOf(apps) != -head.amount) {
            ctx.report.note(QStringLiteral("Bill payments larger than the vendor's open bills were imported as "
                                           "journal entries."));
            return false;
        }
        r = Purchases(file).payBills(plan.date, head.accountId, apps, plan.number, {}, docs);
        if (r.ok) {
            return true; // payBills filled `docs`
        }
    } else if (t == QLatin1String("CHECK") || t == QLatin1String("CREDITCARD") || t == QLatin1String("CCARD")) {
        const bool check = t == QLatin1String("CHECK");
        const int type = typeOf(head.accountId);
        if (head.amount > 0 || type != int(check ? AccountType::Bank : AccountType::CreditCard)) {
            return false;
        }
        BusinessDocument d = header(plan, check ? DocKind::Check : DocKind::CreditCardCharge, head.partyId);
        d.accountId = head.accountId;
        d.reference = plan.number;
        d.lines = linesOf(ctx, plan, 1);
        r = Purchases(file).createExpense(d);
    } else if (t == QLatin1String("DEPOSIT")) {
        if (head.amount <= 0 || typeOf(head.accountId) != int(AccountType::Bank)) {
            return false;
        }
        // Undeposited Funds rows with a customer are payments waiting in
        // the queue (oldest first, same customer and amount); the rest are
        // other deposit lines.
        const qint64 uf = chart.idForRole(SystemRole::UndepositedFunds);
        Banking banking(file);
        Documents documents(file);
        QList<qint64> waiting = banking.undepositedPayments();
        QList<qint64> payments;
        QList<DepositLine> other;
        for (qsizetype i = 1; i < plan.rows.size(); ++i) {
            const TxRow &row = plan.rows.at(i);
            qint64 match = 0;
            if (row.accountId == uf && row.amount < 0) {
                for (qint64 id : std::as_const(waiting)) {
                    const std::optional<BusinessDocument> p = documents.load(id);
                    if (p && p->partyId == row.partyId && p->total.minor() == -row.amount) {
                        match = id;
                        break;
                    }
                }
            }
            if (match != 0) {
                payments << match;
                waiting.removeOne(match);
            } else {
                other << DepositLine{row.accountId, file.money(-row.amount), row.partyId, row.classId, row.memo};
            }
        }
        r = banking.deposit(head.accountId, plan.date, payments, plan.memo, {}, other);
    } else if (t == QLatin1String("TRANSFER")) {
        if (plan.rows.size() != 2 || head.amount >= 0 || plan.rows.at(1).amount != -head.amount) {
            return false;
        }
        r = Banking(file).transfer(head.accountId, plan.rows.at(1).accountId, file.money(-head.amount), plan.date,
                                   plan.memo, {}, plan.number);
    } else {
        return false;
    }
    if (!r.ok) {
        return false;
    }
    docs->append(r.id);
    return true;
}

} // namespace QindaOffice::Books::IifInternal
