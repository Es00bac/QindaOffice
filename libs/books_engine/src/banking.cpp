// SPDX-License-Identifier: GPL-3.0-or-later
// Registers, transfers and deposits. The bank feed is in bank_feed.cpp.
#include "banking.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "sql_util.h"

#include <QHash>
#include <QSqlQuery>

namespace QindaOffice::Books {

Banking::Banking(CompanyFile &file)
    : m_file(file)
{
}

QList<RegisterEntry> Banking::registerFor(qint64 accountId, const QDate &from, const QDate &to) const
{
    const std::optional<Account> account = ChartOfAccounts(m_file).account(accountId);
    if (!account) {
        return {};
    }
    const bool debitNormal = accountTypeInfo(account->type).normalBalance == NormalBalance::Debit;
    const QSqlDatabase db = m_file.database();
    // Opening balance before `from`.
    qint64 running = 0;
    if (from.isValid()) {
        running = Sql::scalarInt(db, QStringLiteral("SELECT COALESCE(SUM(debit) - SUM(credit), 0) FROM journal_lines"
                                                    " WHERE account_id = ? AND voided = 0 AND date < ?"),
                                 {accountId, Sql::isoDate(from)});
        if (!debitNormal) {
            running = -running;
        }
    }
    QList<RegisterEntry> out;
    QSqlQuery q(db);
    Sql::run(q, QStringLiteral(
                    "SELECT l.id, l.txn_id, t.doc_id, l.date, t.kind, t.number, p.display_name, l.memo, l.debit,"
                    " l.credit, l.cleared,"
                    " (SELECT CASE WHEN COUNT(*) = 1 THEN MAX(a2.name) ELSE '-Split-' END FROM journal_lines o"
                    "   JOIN accounts a2 ON a2.id = o.account_id WHERE o.txn_id = l.txn_id AND o.id <> l.id)"
                    " FROM journal_lines l JOIN transactions t ON t.id = l.txn_id"
                    " LEFT JOIN parties p ON p.id = l.party_id"
                    " WHERE l.account_id = ? AND l.voided = 0 AND (? = '' OR l.date >= ?) AND (? = '' OR l.date <= ?)"
                    " ORDER BY l.date, l.txn_id, l.id"),
             {accountId, Sql::isoDate(from), Sql::isoDate(from), Sql::isoDate(to), Sql::isoDate(to)});
    while (q.next()) {
        RegisterEntry e;
        e.lineId = q.value(0).toLongLong();
        e.txnId = q.value(1).toLongLong();
        e.docId = q.value(2).toLongLong();
        e.date = Sql::fromIso(q.value(3));
        e.kind = DocKind(q.value(4).toInt());
        e.number = q.value(5).toString();
        e.payee = q.value(6).toString();
        e.memo = q.value(7).toString();
        const qint64 debit = q.value(8).toLongLong();
        const qint64 credit = q.value(9).toLongLong();
        e.cleared = q.value(10).toInt();
        e.otherAccount = q.value(11).toString();
        const qint64 up = debitNormal ? debit : credit;
        const qint64 down = debitNormal ? credit : debit;
        running += up - down;
        e.increase = m_file.money(up);
        e.decrease = m_file.money(down);
        e.balance = m_file.money(running);
        out << e;
    }
    return out;
}

Result Banking::transfer(qint64 fromAccountId, qint64 toAccountId, const Money &amount, const QDate &date,
                         const QString &memo, const PostOptions &options, const QString &reference)
{
    ChartOfAccounts chart(m_file);
    if (!chart.account(fromAccountId) || !chart.account(toAccountId) || fromAccountId == toAccountId) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose two different accounts"));
    }
    if (!amount.isPositive() || !amount.sameCurrency(m_file.money(0))) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a transfer needs a positive amount"));
    }
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::Transfer;
    doc.date = date;
    doc.memo = memo;
    doc.reference = reference;
    doc.accountId = fromAccountId;
    doc.toAccountId = toAccountId;
    doc.total = amount;
    doc.subtotal = amount;
    Documents documents(m_file);
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    const QList<PostingLine> lines{
        PostingLine{toAccountId, amount.minor(), 0, 0, 0, 0, 0, 0, memo},
        PostingLine{fromAccountId, 0, amount.minor(), 0, 0, 0, 0, 0, memo},
    };
    if (const Result p = Ledger(m_file).postLines(doc.id, doc.kind, date, reference, memo, lines, options); !p.ok) {
        return p;
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("document"), doc.id, {},
                 QStringLiteral("{\"transfer\":\"%1\"}").arg(amount.toPlainString()));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

QList<qint64> Banking::undepositedPayments() const
{
    QList<qint64> out;
    const qint64 uf = ChartOfAccounts(m_file).idForRole(SystemRole::UndepositedFunds);
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT d.id FROM documents d WHERE d.kind IN (?, ?) AND d.state = ?"
                               " AND d.account_id = ? AND NOT EXISTS (SELECT 1 FROM document_lines dl"
                               " JOIN documents dep ON dep.id = dl.doc_id WHERE dl.link_doc_id = d.id"
                               " AND dep.kind = ? AND dep.state <> ?) ORDER BY d.date, d.id"),
             {int(DocKind::ReceivePayment), int(DocKind::SalesReceipt), int(DocState::Open), uf,
              int(DocKind::Deposit), int(DocState::Void)});
    while (q.next()) {
        out << q.value(0).toLongLong();
    }
    return out;
}

Result Banking::deposit(qint64 bankAccountId, const QDate &date, const QList<qint64> &paymentDocIds,
                        const QString &memo, const PostOptions &options)
{
    const std::optional<Account> bank = ChartOfAccounts(m_file).account(bankAccountId);
    if (!bank || bank->type != AccountType::Bank) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose a bank account"));
    }
    const QList<qint64> waiting = undepositedPayments();
    Documents documents(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::Deposit;
    doc.date = date;
    doc.memo = memo;
    doc.accountId = bankAccountId;
    Money total = m_file.money(0);
    for (qint64 id : paymentDocIds) {
        const std::optional<BusinessDocument> payment = documents.load(id);
        if (!payment || !waiting.contains(id)) {
            return Result::failure(ErrorCode::NotFound,
                                   QStringLiteral("only payments still in Undeposited Funds can be deposited"));
        }
        DocumentLine line;
        line.linkDocId = id;
        line.partyId = payment->partyId;
        line.accountId = payment->accountId;
        line.amount = payment->total;
        line.description = docKindName(payment->kind) + QLatin1Char(' ')
            + (payment->number.isEmpty() ? payment->reference : payment->number);
        doc.lines << line;
        total += payment->total;
    }
    if (doc.lines.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("choose the payments to deposit"));
    }
    doc.total = total;
    doc.subtotal = total;
    SqlTransaction txn(m_file);
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    QList<PostingLine> lines{PostingLine{bankAccountId, total.minor(), 0, 0, 0, 0, 0, 0, memo}};
    for (const DocumentLine &line : std::as_const(doc.lines)) {
        lines << PostingLine{line.accountId, 0, line.amount.minor(), line.partyId, 0, 0, 0, 0, line.description};
    }
    if (const Result p = Ledger(m_file).postLines(doc.id, doc.kind, date, {}, memo, lines, options); !p.ok) {
        return p;
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("document"), doc.id, {},
                 QStringLiteral("{\"deposit\":\"%1\"}").arg(total.toPlainString()));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

} // namespace QindaOffice::Books
