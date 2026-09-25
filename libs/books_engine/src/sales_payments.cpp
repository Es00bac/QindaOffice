// SPDX-License-Identifier: GPL-3.0-or-later
// Money received and returned: payments, credit application, refunds, and
// the customer-side queries (open invoices, balances, statements).
#include "sales.h"

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

bool isCustomer(CompanyFile &file, qint64 id)
{
    const std::optional<Party> p = Lists(file).party(id);
    return p && p->kind == PartyKind::Customer;
}

} // namespace

Result Sales::receivePayment(const ReceivePaymentInput &input, const PostOptions &options)
{
    if (!isCustomer(m_file, input.customerId)) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("choose a customer"));
    }
    if (!input.amount.isPositive() || !input.amount.sameCurrency(m_file.money(0))) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a payment needs a positive amount in %1")
                                                       .arg(m_file.currency()));
    }
    Documents documents(m_file);
    Money applied = m_file.money(0);
    for (const PaymentApplication &app : input.applications) {
        const std::optional<BusinessDocument> invoice = documents.load(app.docId);
        if (!invoice || invoice->kind != DocKind::Invoice || invoice->state != DocState::Open
            || invoice->partyId != input.customerId) {
            return Result::failure(ErrorCode::NotFound,
                                   QStringLiteral("payments apply to this customer's open invoices only"));
        }
        if (!app.amount.isPositive() || app.amount > documents.openBalance(app.docId)) {
            return Result::failure(ErrorCode::OverApplied,
                                   QStringLiteral("%1 is more than the open balance of invoice %2")
                                       .arg(app.amount.toPlainString(), invoice->number));
        }
        applied += app.amount;
    }
    if (applied > input.amount) {
        return Result::failure(ErrorCode::OverApplied,
                               QStringLiteral("the applications exceed the payment"));
    }
    ChartOfAccounts chart(m_file);
    const qint64 depositTo =
        input.depositAccountId != 0 ? input.depositAccountId : chart.idForRole(SystemRole::UndepositedFunds);
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::ReceivePayment;
    doc.partyId = input.customerId;
    doc.date = input.date;
    doc.reference = input.reference;
    doc.memo = input.memo;
    doc.accountId = depositTo;
    doc.total = input.amount;
    doc.subtotal = input.amount;
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    const QList<PostingLine> lines{
        PostingLine{depositTo, input.amount.minor(), 0, input.customerId, 0, 0, 0, 0, input.memo},
        PostingLine{chart.idForRole(SystemRole::AccountsReceivable), 0, input.amount.minor(), input.customerId, 0, 0,
                    0, 0, input.memo},
    };
    if (const Result p = Ledger(m_file).postLines(doc.id, doc.kind, doc.date, doc.reference, doc.memo, lines, options);
        !p.ok) {
        return p;
    }
    for (const PaymentApplication &app : input.applications) {
        if (const Result a = documents.addApplication(doc.id, app.docId, app.amount, input.date); !a.ok) {
            return a;
        }
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("document"), doc.id, {},
                 QStringLiteral("{\"payment\":\"%1\",\"applied\":\"%2\"}")
                     .arg(input.amount.toPlainString(), applied.toPlainString()));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

Result Sales::applyCredit(qint64 sourceDocId, qint64 invoiceId, const Money &amount, const QDate &date)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> source = documents.load(sourceDocId);
    const std::optional<BusinessDocument> invoice = documents.load(invoiceId);
    if (!source || (source->kind != DocKind::ReceivePayment && source->kind != DocKind::CreditMemo)
        || source->state != DocState::Open) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such credit"));
    }
    if (!invoice || invoice->kind != DocKind::Invoice || invoice->state != DocState::Open
        || invoice->partyId != source->partyId) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("credits apply to the same customer's open invoices"));
    }
    if (!amount.isPositive() || amount > documents.openBalance(sourceDocId)
        || amount > documents.openBalance(invoiceId)) {
        return Result::failure(ErrorCode::OverApplied,
                               QStringLiteral("more than the open credit or the invoice balance"));
    }
    if (const Result open = Ledger(m_file).checkLock(date, {}); !open.ok) {
        return open;
    }
    SqlTransaction txn(m_file);
    const Result a = documents.addApplication(sourceDocId, invoiceId, amount, date);
    if (!a.ok) {
        return a;
    }
    m_file.audit(QStringLiteral("apply"), QStringLiteral("document"), invoiceId, {},
                 QStringLiteral("{\"source\":%1,\"amount\":\"%2\"}").arg(sourceDocId).arg(amount.toPlainString()));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return a;
}

Result Sales::refund(const RefundInput &input, const PostOptions &options)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> credit = documents.load(input.creditDocId);
    if (!credit || credit->partyId != input.customerId || credit->state != DocState::Open
        || (credit->kind != DocKind::CreditMemo && credit->kind != DocKind::ReceivePayment)) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("refunds pay out an open credit of the customer"));
    }
    if (!input.amount.isPositive() || input.amount > documents.openBalance(input.creditDocId)) {
        return Result::failure(ErrorCode::OverApplied, QStringLiteral("more than the open credit"));
    }
    if (input.bankAccountId == 0 || !ChartOfAccounts(m_file).account(input.bankAccountId)) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose the account the refund is paid from"));
    }
    const qint64 ar = ChartOfAccounts(m_file).idForRole(SystemRole::AccountsReceivable);
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::Refund;
    doc.partyId = input.customerId;
    doc.date = input.date;
    doc.memo = input.memo;
    doc.accountId = input.bankAccountId;
    doc.total = input.amount;
    doc.subtotal = input.amount;
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    const QList<PostingLine> lines{
        PostingLine{ar, input.amount.minor(), 0, input.customerId, 0, 0, 0, 0, input.memo},
        PostingLine{input.bankAccountId, 0, input.amount.minor(), input.customerId, 0, 0, 0, 0, input.memo},
    };
    if (const Result p = Ledger(m_file).postLines(doc.id, doc.kind, doc.date, {}, doc.memo, lines, options); !p.ok) {
        return p;
    }
    if (const Result a = documents.addApplication(input.creditDocId, doc.id, input.amount, input.date); !a.ok) {
        return a;
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("document"), doc.id, {},
                 QStringLiteral("{\"refund\":\"%1\",\"credit\":%2}").arg(input.amount.toPlainString()).arg(input.creditDocId));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

QList<BusinessDocument> Sales::openInvoices(qint64 customerId, const QDate &asOf) const
{
    DocumentFilter filter;
    filter.kinds = {DocKind::Invoice};
    filter.partyId = customerId;
    filter.to = asOf;
    filter.includeDrafts = false;
    Documents documents(m_file);
    QList<BusinessDocument> out;
    for (const BusinessDocument &doc : documents.list(filter)) {
        if (documents.openBalance(doc.id, asOf).isPositive()) {
            out << doc;
        }
    }
    return out;
}

QList<BusinessDocument> Sales::openCredits(qint64 customerId) const
{
    DocumentFilter filter;
    filter.kinds = {DocKind::ReceivePayment, DocKind::CreditMemo};
    filter.partyId = customerId;
    filter.includeDrafts = false;
    Documents documents(m_file);
    QList<BusinessDocument> out;
    for (const BusinessDocument &doc : documents.list(filter)) {
        if (documents.openBalance(doc.id).isPositive()) {
            out << doc;
        }
    }
    return out;
}

Money Sales::customerBalance(qint64 customerId, const QDate &asOf) const
{
    return m_file.money(Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(SUM(l.debit) - SUM(l.credit), 0) FROM journal_lines l"
                       " JOIN accounts a ON a.id = l.account_id WHERE a.type = ? AND l.voided = 0"
                       " AND l.party_id = ? AND (? = '' OR l.date <= ?)"),
        {int(AccountType::AccountsReceivable), customerId, Sql::isoDate(asOf), Sql::isoDate(asOf)}));
}

CustomerStatement Sales::statement(qint64 customerId, const QDate &from, const QDate &to) const
{
    CustomerStatement s;
    s.customerId = customerId;
    s.from = from;
    s.to = to;
    s.opening = customerBalance(customerId, from.addDays(-1));
    Money running = s.opening;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT l.date, t.doc_id, t.kind, d.number, t.memo, SUM(l.debit) - SUM(l.credit)"
                                   " FROM journal_lines l JOIN accounts a ON a.id = l.account_id"
                                   " JOIN transactions t ON t.id = l.txn_id JOIN documents d ON d.id = t.doc_id"
                                   " WHERE a.type = ? AND l.voided = 0 AND l.party_id = ? AND l.date >= ?"
                                   " AND l.date <= ? GROUP BY l.txn_id ORDER BY l.date, l.txn_id"),
                 {int(AccountType::AccountsReceivable), customerId, Sql::isoDate(from), Sql::isoDate(to)});
        while (q.next()) {
            StatementLine line;
            line.date = Sql::fromIso(q.value(0));
            line.docId = q.value(1).toLongLong();
            line.kind = DocKind(q.value(2).toInt());
            line.number = q.value(3).toString();
            line.description = docKindName(line.kind) + (line.number.isEmpty() ? QString() : QLatin1Char(' ') + line.number);
            line.amount = m_file.money(q.value(5).toLongLong());
            running += line.amount;
            line.balance = running;
            s.lines << line;
        }
    }
    s.closing = running;
    Documents documents(m_file);
    AgingBuckets &b = s.aging;
    b.current = b.days1to30 = b.days31to60 = b.days61to90 = b.over90 = m_file.money(0);
    for (const BusinessDocument &doc : openInvoices(customerId, to)) {
        const Money open = documents.openBalance(doc.id, to);
        switch (agingBucket(doc.dueDate.isValid() ? doc.dueDate : doc.date, to)) {
        case 0: b.current += open; break;
        case 1: b.days1to30 += open; break;
        case 2: b.days31to60 += open; break;
        case 3: b.days61to90 += open; break;
        default: b.over90 += open; break;
        }
    }
    // Unapplied credits reduce what is owed; they are shown as current.
    for (const BusinessDocument &credit : openCredits(customerId)) {
        if (credit.date <= to) {
            b.current -= documents.openBalance(credit.id, to);
        }
    }
    return s;
}

} // namespace QindaOffice::Books
