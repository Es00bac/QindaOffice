// SPDX-License-Identifier: GPL-3.0-or-later
// The "to be printed" check queue (check_queue.h): queueing, numbering at
// print time, misprints recorded void, a cancelled run taken back.
#include "check_queue.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "journal.h"
#include "sql_util.h"

#include <QSqlQuery>

#include <algorithm>

namespace QindaOffice::Books {

namespace {

QString nextKey(qint64 accountId)
{
    return QStringLiteral("next_check_number:%1").arg(accountId);
}

// M71 (D-081): a paycheck drawn on a bank account is a check too — it waits
// in the same queue, takes the account's numbers and is confirmed the same
// way (check_print.cpp prints its pay stub on the vouchers).
bool isPayment(DocKind kind)
{
    return kind == DocKind::Check || kind == DocKind::BillPayment || kind == DocKind::Paycheck;
}

bool storeNext(CompanyFile &file, qint64 accountId, qint64 next, QString *error)
{
    return Sql::exec(file.database(), QStringLiteral("INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)"),
                     {nextKey(accountId), next}, error);
}

QueuedCheck queuedFrom(const BusinessDocument &d)
{
    return QueuedCheck{d.id, d.kind, d.accountId, d.date, d.partyId, d.total, d.number};
}

} // namespace

CheckQueue::CheckQueue(CompanyFile &file)
    : m_file(file)
{
}

bool CheckQueue::canQueue(CompanyFile &file, qint64 docId, QString *why)
{
    const auto fail = [why](const QString &message) {
        if (why != nullptr) {
            *why = message;
        }
        return false;
    };
    const std::optional<BusinessDocument> doc = Documents(file).load(docId);
    if (!doc || !isPayment(doc->kind)) {
        return fail(QStringLiteral("only checks and bill payments print as checks"));
    }
    if (doc->state != DocState::Open) {
        return fail(QStringLiteral("a void check is not printed"));
    }
    const std::optional<Account> bank = ChartOfAccounts(file).account(doc->accountId);
    if (!bank || bank->type != AccountType::Bank) {
        return fail(QStringLiteral("only a payment from a bank account prints as a check"));
    }
    return true;
}

Result CheckQueue::setToPrint(qint64 docId, bool toPrint)
{
    QString why;
    if (toPrint && !canQueue(m_file, docId, &why)) {
        return Result::failure(ErrorCode::Invalid, why);
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(), QStringLiteral("UPDATE documents SET to_print = ?, updated_at = ? WHERE id = ?"),
                   {toPrint ? 1 : 0, CompanyFile::nowStamp(), docId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(toPrint ? QStringLiteral("queue-print") : QStringLiteral("unqueue-print"), QStringLiteral("document"),
                 docId, {}, {});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(docId);
}

QList<QueuedCheck> CheckQueue::queue(qint64 bankAccountId) const
{
    QList<qint64> ids;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM documents WHERE to_print = 1 AND state = ? AND kind IN (?, ?, ?)"
                                   " AND (? = 0 OR account_id = ?) ORDER BY date, id"),
                 {int(DocState::Open), int(DocKind::Check), int(DocKind::BillPayment), int(DocKind::Paycheck),
                  bankAccountId, bankAccountId});
        while (q.next()) {
            ids << q.value(0).toLongLong();
        }
    }
    QList<QueuedCheck> out;
    Documents documents(m_file);
    for (qint64 id : std::as_const(ids)) {
        if (const std::optional<BusinessDocument> d = documents.load(id)) {
            out << queuedFrom(*d);
        }
    }
    return out;
}

qint64 CheckQueue::highestUsed(qint64 bankAccountId) const
{
    // All-digit numbers only; a bill payment's reference counted as M63
    // printed it (check_print.cpp) when it has no number.
    const QString digits = QStringLiteral(" <> '' AND %1 NOT GLOB '*[^0-9]*'");
    const qint64 byNumber = Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(MAX(CAST(number AS INTEGER)), 0) FROM documents WHERE account_id = ?"
                       " AND kind IN (?, ?, ?) AND number") + digits.arg(QStringLiteral("number")),
        {bankAccountId, int(DocKind::Check), int(DocKind::BillPayment), int(DocKind::Paycheck)});
    const qint64 byReference = Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(MAX(CAST(reference AS INTEGER)), 0) FROM documents WHERE account_id = ?"
                       " AND kind = ? AND COALESCE(number, '') = '' AND reference") + digits.arg(QStringLiteral("reference")),
        {bankAccountId, int(DocKind::BillPayment)});
    return std::max(byNumber, byReference);
}

qint64 CheckQueue::nextNumber(qint64 bankAccountId) const
{
    const qint64 stored = Sql::scalarInt(m_file.database(), QStringLiteral("SELECT value FROM settings WHERE key = ?"),
                                         {nextKey(bankAccountId)});
    const qint64 used = highestUsed(bankAccountId);
    if (stored > 0) {
        return std::max(stored, used + 1);
    }
    return used > 0 ? used + 1 : 1001;
}

Result CheckQueue::renumber(qint64 docId, const QString &number, bool toPrint, const QString &reason)
{
    const std::optional<BusinessDocument> doc = Documents(m_file).load(docId);
    if (!doc || !isPayment(doc->kind) || doc->state != DocState::Open) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such check"));
    }
    const QSqlDatabase db = m_file.database();
    QString error;
    if (doc->txnId != 0) {
        QList<PostingLine> lines;
        bool cleared = false;
        {
            QSqlQuery q(db);
            Sql::run(q, QStringLiteral("SELECT account_id, debit, credit, party_id, class_id, item_id, tax_component_id,"
                                       " tax_agency_id, memo, cleared FROM journal_lines WHERE txn_id = ? ORDER BY line_no"),
                     {doc->txnId});
            while (q.next()) {
                lines << PostingLine{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toLongLong(),
                                     q.value(3).toLongLong(), q.value(4).toLongLong(), q.value(5).toLongLong(),
                                     q.value(6).toLongLong(), q.value(7).toLongLong(), q.value(8).toString()};
                cleared = cleared || q.value(9).toInt() != 0;
            }
        }
        if (cleared) {
            return Result::failure(ErrorCode::Reconciled,
                                   QStringLiteral("check %1 has cleared the bank; its number cannot change").arg(doc->number));
        }
        const QString memo = Sql::scalar(db, QStringLiteral("SELECT memo FROM transactions WHERE id = ?"), {doc->txnId})
                                 .toString();
        Ledger ledger(m_file);
        const Result voided = ledger.voidLiveTransaction(docId, reason, {});
        if (!voided.ok) {
            return voided;
        }
        const Result posted = ledger.postLines(docId, doc->kind, doc->date, number, memo, lines, {});
        if (!posted.ok) {
            return posted;
        }
        Sql::exec(db, QStringLiteral("UPDATE transactions SET replaced_by = ? WHERE id = ?"), {posted.id, voided.id});
    }
    if (!Sql::exec(db, QStringLiteral("UPDATE documents SET number = ?, to_print = ?, updated_at = ? WHERE id = ?"),
                   {number, toPrint ? 1 : 0, CompanyFile::nowStamp(), docId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("renumber"), QStringLiteral("document"), docId,
                 QStringLiteral("{\"number\":\"%1\"}").arg(doc->number), QStringLiteral("{\"number\":\"%1\"}").arg(number),
                 reason);
    return Result::success(docId);
}

Result CheckQueue::assignNumbers(qint64 bankAccountId, const QList<qint64> &docIds, qint64 first,
                                 QList<QueuedCheck> *numbered)
{
    if (docIds.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("choose the checks to print"));
    }
    if (first <= 0) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("the first check number must be above zero"));
    }
    Documents documents(m_file);
    SqlTransaction txn(m_file);
    QList<QueuedCheck> done;
    for (qsizetype i = 0; i < docIds.size(); ++i) {
        const std::optional<BusinessDocument> d = documents.load(docIds.at(i));
        if (!d || !isPayment(d->kind) || d->state != DocState::Open || !d->toPrint || d->accountId != bankAccountId) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("only checks waiting to print from this account"));
        }
        const QString number = QString::number(first + i);
        const qint64 taken = Sql::scalarInt(
            m_file.database(),
            QStringLiteral("SELECT COUNT(*) FROM documents WHERE account_id = ? AND kind IN (?, ?, ?) AND id <> ?"
                           " AND (number = ? OR (kind = ? AND COALESCE(number, '') = '' AND reference = ?))"),
            {bankAccountId, int(DocKind::Check), int(DocKind::BillPayment), int(DocKind::Paycheck), d->id, number,
             int(DocKind::BillPayment), number});
        if (taken > 0) {
            return Result::failure(ErrorCode::Duplicate,
                                   QStringLiteral("check number %1 is already used on this account").arg(number));
        }
        if (const Result r = renumber(d->id, number, false, QStringLiteral("check number given at printing")); !r.ok) {
            return r;
        }
        QueuedCheck q = queuedFrom(*d);
        q.number = number;
        done << q;
    }
    QString error;
    if (!storeNext(m_file, bankAccountId, first + docIds.size(), &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("print-checks"), QStringLiteral("account"), bankAccountId, {},
                 QStringLiteral("{\"first\":%1,\"count\":%2}").arg(first).arg(docIds.size()));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    if (numbered != nullptr) {
        *numbered = done;
    }
    return Result::success(docIds.first());
}

Result CheckQueue::confirmPrinted(const QList<qint64> &misprinted, const QDate &today)
{
    Documents documents(m_file);
    SqlTransaction txn(m_file);
    for (qint64 id : misprinted) {
        const std::optional<BusinessDocument> d = documents.load(id);
        if (!d || !isPayment(d->kind) || d->state != DocState::Open || d->toPrint || d->number.isEmpty()) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("only a printed check can be reprinted"));
        }
        // The spoiled paper check keeps its number — as a void record.
        BusinessDocument spoiled;
        spoiled.kind = DocKind::Check;
        spoiled.state = DocState::Void;
        spoiled.number = d->number;
        spoiled.partyId = d->partyId;
        spoiled.accountId = d->accountId;
        spoiled.date = today.isValid() ? today : d->date;
        spoiled.memo = QStringLiteral("Misprinted check %1; the payment went back to the print queue").arg(d->number);
        spoiled.sourceDocId = d->id;
        spoiled.subtotal = m_file.money(0);
        spoiled.discountAmount = m_file.money(0);
        spoiled.taxAmount = m_file.money(0);
        spoiled.total = m_file.money(0);
        if (const Result w = documents.write(spoiled); !w.ok) {
            return w;
        }
        m_file.audit(QStringLiteral("void-check-number"), QStringLiteral("document"), spoiled.id, {},
                     QStringLiteral("{\"number\":\"%1\",\"payment\":%2}").arg(d->number).arg(d->id));
        if (const Result r = renumber(id, QString(), true, QStringLiteral("misprinted; back in the print queue")); !r.ok) {
            return r;
        }
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success();
}

Result CheckQueue::takeBack(qint64 bankAccountId, const QList<qint64> &docIds)
{
    Documents documents(m_file);
    SqlTransaction txn(m_file);
    qint64 lowest = 0;
    for (qint64 id : docIds) {
        const std::optional<BusinessDocument> d = documents.load(id);
        bool numeric = false;
        const qint64 number = d ? d->number.toLongLong(&numeric) : 0;
        if (!d || !isPayment(d->kind) || d->state != DocState::Open || d->toPrint || !numeric
            || d->accountId != bankAccountId) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("only checks of this print run can be taken back"));
        }
        lowest = lowest == 0 ? number : std::min(lowest, number);
        if (const Result r = renumber(id, QString(), true, QStringLiteral("not printed; back in the print queue")); !r.ok) {
            return r;
        }
    }
    QString error;
    if (lowest > 0 && !storeNext(m_file, bankAccountId, lowest, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success();
}

} // namespace QindaOffice::Books
