// SPDX-License-Identifier: GPL-3.0-or-later
#include "purchases.h"

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "purchase_orders.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QMap>

namespace QindaOffice::Books {

namespace {

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool isPaymentAccount(const std::optional<Account> &a)
{
    return a && (a->type == AccountType::Bank || a->type == AccountType::CreditCard
                 || a->type == AccountType::OtherCurrentAsset || a->type == AccountType::OtherCurrentLiability);
}

} // namespace

Purchases::Purchases(CompanyFile &file)
    : m_file(file)
{
}

Result Purchases::validate(BusinessDocument &doc) const
{
    const bool needsVendor = doc.kind == DocKind::Bill || doc.kind == DocKind::VendorCredit;
    const std::optional<Party> vendor = Lists(m_file).party(doc.partyId);
    if (needsVendor && (!vendor || vendor->kind != PartyKind::Vendor)) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("choose a vendor"));
    }
    if (doc.partyId != 0 && !vendor) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("unknown payee"));
    }
    if (!doc.date.isValid() || doc.lines.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a date and at least one line are required"));
    }
    doc.discount = Discount::none();
    doc.taxRateId = 0;
    Documents documents(m_file);
    documents.fillFromItems(doc, true);
    ChartOfAccounts chart(m_file);
    for (DocumentLine &line : doc.lines) {
        if (line.accountId == 0 && vendor) {
            line.accountId = vendor->defaultAccountId;
        }
        if (line.accountId == 0 || !chart.account(line.accountId)) {
            return Result::failure(ErrorCode::UnknownAccount,
                                   QStringLiteral("line \"%1\" needs a category account").arg(line.description));
        }
        if (!line.amount.sameCurrency(m_file.money(0)) || !line.unitPrice.sameCurrency(m_file.money(0))) {
            return Result::failure(ErrorCode::CurrencyMismatch, QStringLiteral("amounts must be in %1").arg(m_file.currency()));
        }
    }
    if (doc.kind == DocKind::Bill) {
        if (doc.termsId == 0) {
            doc.termsId = vendor && vendor->termsId != 0 ? vendor->termsId : m_file.settings().defaultTermsId;
        }
        if (!doc.dueDate.isValid()) {
            doc.dueDate = documents.dueDateFor(doc.date, doc.termsId);
        }
    }
    if (doc.kind == DocKind::Expense || doc.kind == DocKind::Check || doc.kind == DocKind::CreditCardCharge) {
        if (!isPaymentAccount(chart.account(doc.accountId))) {
            return Result::failure(ErrorCode::UnknownAccount,
                                   QStringLiteral("choose the bank or credit card account it was paid from"));
        }
    }
    // M68: only a check drawn on a bank account waits to print; its number
    // comes from the print run (check_queue.h).
    if (doc.toPrint) {
        const std::optional<Account> bank = chart.account(doc.accountId);
        if (doc.kind != DocKind::Check || !bank || bank->type != AccountType::Bank) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("only a check from a bank account can be printed later"));
        }
        doc.number.clear();
    }
    if (doc.kind == DocKind::Bill) {
        if (const Result received = PurchaseOrders(m_file).checkReceipts(doc); !received.ok) {
            return received;
        }
    }
    const DocumentTotals totals = documents.finalize(doc);
    if (totals.total.isNegative()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("the total is negative"));
    }
    return Result::success();
}

Result Purchases::postPurchase(BusinessDocument &doc, const PostOptions &options, bool isNew, const QString &reason)
{
    if (const Result valid = validate(doc); !valid.ok) {
        return valid;
    }
    Documents documents(m_file);
    Ledger ledger(m_file);
    ChartOfAccounts chart(m_file);
    const std::optional<BusinessDocument> before = isNew ? std::nullopt : documents.load(doc.id);
    if (before && doc.kind == DocKind::Bill && documents.appliedTo(doc.id) > doc.total) {
        return Result::failure(ErrorCode::OverApplied,
                               QStringLiteral("payments already applied exceed the new total"));
    }
    // Bills and vendor credits sit on A/P; the rest on the payment account.
    const bool onPayable = doc.kind == DocKind::Bill || doc.kind == DocKind::VendorCredit;
    const qint64 counterAccount = onPayable ? chart.idForRole(SystemRole::AccountsPayable) : doc.accountId;
    // Vendor credits reverse a bill: Dr A/P, Cr the lines.
    const bool reverse = doc.kind == DocKind::VendorCredit;
    SqlTransaction txn(m_file);
    qint64 voidedTxn = 0;
    if (!isNew) {
        const Result voided = ledger.voidLiveTransaction(doc.id, reason, options);
        if (!voided.ok) {
            return voided;
        }
        voidedTxn = voided.id;
    }
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    QList<PostingLine> lines;
    auto side = [reverse](qint64 amount, bool debit, PostingLine &p) {
        const bool isDebit = (reverse ? !debit : debit) != (amount < 0);
        const qint64 value = amount < 0 ? -amount : amount;
        p.debit = isDebit ? value : 0;
        p.credit = isDebit ? 0 : value;
    };
    PostingLine counter;
    counter.accountId = counterAccount;
    counter.partyId = doc.partyId;
    counter.memo = doc.memo;
    side(doc.total.minor(), false, counter);
    lines << counter;
    for (const DocumentLine &line : std::as_const(doc.lines)) {
        PostingLine p;
        p.accountId = line.accountId;
        p.partyId = doc.partyId;
        p.itemId = line.itemId;
        p.classId = line.classId;
        p.memo = line.description;
        side(line.amount.minor(), true, p);
        lines << p;
    }
    const Result posted = ledger.postLines(doc.id, doc.kind, doc.date, doc.number.isEmpty() ? doc.reference : doc.number,
                                           doc.memo, lines, options);
    if (!posted.ok) {
        return posted;
    }
    if (voidedTxn != 0) {
        Sql::exec(m_file.database(), QStringLiteral("UPDATE transactions SET replaced_by = ? WHERE id = ?"),
                  {posted.id, voidedTxn});
    }
    m_file.audit(isNew ? QStringLiteral("create") : QStringLiteral("edit"), QStringLiteral("document"), doc.id,
                 before ? compact(documents.toJson(*before)) : QString(), compact(documents.toJson(doc)), reason);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

Result Purchases::createBill(BusinessDocument bill, const PostOptions &options)
{
    bill.kind = DocKind::Bill;
    bill.id = 0;
    bill.state = DocState::Open;
    bill.toPrint = false;
    return postPurchase(bill, options, true, {});
}

Result Purchases::updateBill(BusinessDocument bill, const QString &reason, const PostOptions &options)
{
    const std::optional<BusinessDocument> before = Documents(m_file).load(bill.id);
    if (!before || before->kind != DocKind::Bill) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such bill"));
    }
    if (before->state == DocState::Void) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("a void bill cannot be edited"));
    }
    bill.kind = DocKind::Bill;
    bill.state = DocState::Open;
    bill.toPrint = false;
    return postPurchase(bill, options, false, reason);
}

Result Purchases::createExpense(BusinessDocument expense, const PostOptions &options)
{
    if (expense.kind != DocKind::Check && expense.kind != DocKind::CreditCardCharge) {
        expense.kind = DocKind::Expense;
    }
    expense.id = 0;
    expense.state = DocState::Open;
    return postPurchase(expense, options, true, {});
}

Result Purchases::createVendorCredit(BusinessDocument credit, const PostOptions &options)
{
    credit.kind = DocKind::VendorCredit;
    credit.id = 0;
    credit.state = DocState::Open;
    credit.toPrint = false;
    return postPurchase(credit, options, true, {});
}

Result Purchases::payBills(const QDate &date, qint64 paymentAccountId, const QList<PaymentApplication> &payments,
                           const QString &reference, const PostOptions &options, QList<qint64> *paymentIds,
                           bool toPrint)
{
    ChartOfAccounts chart(m_file);
    const std::optional<Account> from = chart.account(paymentAccountId);
    if (!isPaymentAccount(from)) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose a bank or credit card account"));
    }
    if (toPrint && from->type != AccountType::Bank) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("only a payment from a bank account prints as a check"));
    }
    const QString ref = toPrint ? QString() : reference;
    if (payments.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("choose the bills to pay"));
    }
    Documents documents(m_file);
    QMap<qint64, QList<PaymentApplication>> byVendor;
    for (const PaymentApplication &p : payments) {
        const std::optional<BusinessDocument> bill = documents.load(p.docId);
        if (!bill || bill->kind != DocKind::Bill || bill->state != DocState::Open) {
            return Result::failure(ErrorCode::NotFound, QStringLiteral("only open bills can be paid"));
        }
        if (!p.amount.isPositive() || p.amount > documents.openBalance(p.docId)) {
            return Result::failure(ErrorCode::OverApplied,
                                   QStringLiteral("%1 is more than the open balance of bill %2")
                                       .arg(p.amount.toPlainString(), bill->number));
        }
        byVendor[bill->partyId] << p;
    }
    const qint64 payable = chart.idForRole(SystemRole::AccountsPayable);
    SqlTransaction txn(m_file);
    QList<qint64> ids;
    for (auto it = byVendor.cbegin(); it != byVendor.cend(); ++it) {
        Money total = m_file.money(0);
        for (const PaymentApplication &p : it.value()) {
            total += p.amount;
        }
        BusinessDocument doc;
        doc.kind = DocKind::BillPayment;
        doc.partyId = it.key();
        doc.date = date;
        doc.reference = ref;
        doc.accountId = paymentAccountId;
        doc.total = total;
        doc.subtotal = total;
        doc.toPrint = toPrint;
        if (const Result w = documents.write(doc); !w.ok) {
            return w;
        }
        const QList<PostingLine> lines{
            PostingLine{payable, total.minor(), 0, it.key(), 0, 0, 0, 0, ref},
            PostingLine{paymentAccountId, 0, total.minor(), it.key(), 0, 0, 0, 0, ref},
        };
        if (const Result p = Ledger(m_file).postLines(doc.id, doc.kind, date, ref, {}, lines, options); !p.ok) {
            return p;
        }
        for (const PaymentApplication &p : it.value()) {
            if (const Result a = documents.addApplication(doc.id, p.docId, p.amount, date); !a.ok) {
                return a;
            }
        }
        m_file.audit(QStringLiteral("create"), QStringLiteral("document"), doc.id, {},
                     QStringLiteral("{\"bill_payment\":\"%1\"}").arg(total.toPlainString()));
        ids << doc.id;
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    if (paymentIds != nullptr) {
        *paymentIds = ids;
    }
    return Result::success(ids.value(0));
}

Result Purchases::applyVendorCredit(qint64 creditId, qint64 billId, const Money &amount, const QDate &date)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> credit = documents.load(creditId);
    const std::optional<BusinessDocument> bill = documents.load(billId);
    if (!credit || credit->kind != DocKind::VendorCredit || credit->state != DocState::Open || !bill
        || bill->kind != DocKind::Bill || bill->state != DocState::Open || bill->partyId != credit->partyId) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("a vendor credit applies to the same vendor's open bills"));
    }
    if (!amount.isPositive() || amount > documents.openBalance(creditId) || amount > documents.openBalance(billId)) {
        return Result::failure(ErrorCode::OverApplied, QStringLiteral("more than the open credit or bill balance"));
    }
    if (const Result open = Ledger(m_file).checkLock(date, {}); !open.ok) {
        return open;
    }
    SqlTransaction txn(m_file);
    const Result a = documents.addApplication(creditId, billId, amount, date);
    if (!a.ok || !txn.commit()) {
        return a.ok ? Result::failure(ErrorCode::Storage, txn.error()) : a;
    }
    return a;
}

QList<BusinessDocument> Purchases::unpaidBills(qint64 vendorId, const QDate &asOf) const
{
    DocumentFilter filter;
    filter.kinds = {DocKind::Bill};
    filter.partyId = vendorId;
    filter.to = asOf;
    Documents documents(m_file);
    QList<BusinessDocument> out;
    for (const BusinessDocument &doc : documents.list(filter)) {
        if (documents.openBalance(doc.id, asOf).isPositive()) {
            out << doc;
        }
    }
    return out;
}

Money Purchases::vendorBalance(qint64 vendorId, const QDate &asOf) const
{
    return m_file.money(Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(SUM(l.credit) - SUM(l.debit), 0) FROM journal_lines l"
                       " JOIN accounts a ON a.id = l.account_id WHERE a.type = ? AND l.voided = 0"
                       " AND l.party_id = ? AND (? = '' OR l.date <= ?)"),
        {int(AccountType::AccountsPayable), vendorId, Sql::isoDate(asOf), Sql::isoDate(asOf)}));
}

} // namespace QindaOffice::Books
