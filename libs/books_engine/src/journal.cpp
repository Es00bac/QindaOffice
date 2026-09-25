// SPDX-License-Identifier: GPL-3.0-or-later
#include "journal.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

Ledger::Ledger(CompanyFile &file)
    : m_file(file)
{
}

Result Ledger::validateBalanced(const QList<PostingLine> &lines)
{
    qint64 debit = 0;
    qint64 credit = 0;
    int nonZero = 0;
    for (const PostingLine &line : lines) {
        if (line.debit < 0 || line.credit < 0 || (line.debit != 0 && line.credit != 0)) {
            return Result::failure(ErrorCode::Invalid,
                                   QStringLiteral("each line is either a debit or a credit, never negative"));
        }
        if (line.accountId <= 0) {
            return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("a line has no account"));
        }
        if (qAddOverflow(debit, line.debit, &debit) || qAddOverflow(credit, line.credit, &credit)) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("amounts overflow"));
        }
        if (line.debit != 0 || line.credit != 0) {
            ++nonZero;
        }
    }
    if (nonZero < 2 && !(nonZero == 0 && lines.size() >= 2)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a transaction needs at least two lines"));
    }
    if (debit != credit) {
        return Result::failure(ErrorCode::Unbalanced,
                               QStringLiteral("debits (%1) do not equal credits (%2)").arg(debit).arg(credit));
    }
    return Result::success();
}

Result Ledger::checkLock(const QDate &date, const PostOptions &options) const
{
    if (!date.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a transaction needs a date"));
    }
    const QDate lock = m_file.lockDate();
    if (lock.isValid() && date <= lock) {
        if (!options.overrideLock) {
            return Result::failure(ErrorCode::ClosedPeriod,
                                   QStringLiteral("the books are closed through %1")
                                       .arg(lock.toString(Qt::ISODate)));
        }
        if (options.reason.trimmed().isEmpty()) {
            return Result::failure(ErrorCode::ClosedPeriod,
                                   QStringLiteral("posting into a closed period needs a reason"));
        }
    }
    return Result::success();
}

Result Ledger::validateLines(const QList<PostingLine> &lines) const
{
    if (const Result balanced = validateBalanced(lines); !balanced.ok) {
        return balanced;
    }
    for (const PostingLine &line : lines) {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT active FROM accounts WHERE id = ?"), {line.accountId});
        if (!q.next()) {
            return Result::failure(ErrorCode::UnknownAccount,
                                   QStringLiteral("account %1 does not exist").arg(line.accountId));
        }
        if (!q.value(0).toBool()) {
            return Result::failure(ErrorCode::InactiveAccount, QStringLiteral("an account on this transaction is inactive"));
        }
    }
    return Result::success();
}

Result Ledger::postLines(qint64 docId, DocKind kind, const QDate &date, const QString &number,
                         const QString &memo, const QList<PostingLine> &lines, const PostOptions &options)
{
    if (const Result valid = validateLines(lines); !valid.ok) {
        return valid;
    }
    if (const Result open = checkLock(date, options); !open.ok) {
        return open;
    }
    const QSqlDatabase db = m_file.database();
    const QDate lock = m_file.lockDate();
    const bool overridden = lock.isValid() && date <= lock;
    QString error;
    const qint64 txnId = Sql::insert(
        db,
        QStringLiteral("INSERT INTO transactions(doc_id, kind, date, number, memo, lock_override_reason,"
                       " created_at, created_by) VALUES(?,?,?,?,?,?,?,?)"),
        {docId, int(kind), Sql::isoDate(date), number, memo,
         overridden ? QVariant(options.reason) : QVariant(), CompanyFile::nowStamp(), m_file.userName()},
        &error);
    if (txnId == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    QSqlQuery insert(db);
    if (!insert.prepare(QStringLiteral(
            "INSERT INTO journal_lines(txn_id, line_no, account_id, date, kind, debit, credit, party_id,"
            " class_id, item_id, tax_component_id, tax_agency_id, memo) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
        return Result::failure(ErrorCode::Storage, QStringLiteral("prepare failed"));
    }
    int lineNo = 0;
    for (const PostingLine &line : lines) {
        if (line.debit == 0 && line.credit == 0) {
            continue; // zero lines carry no information into the ledger
        }
        const QVariantList values{txnId,
                                  ++lineNo,
                                  line.accountId,
                                  Sql::isoDate(date),
                                  int(kind),
                                  line.debit,
                                  line.credit,
                                  Sql::idOrNull(line.partyId),
                                  Sql::idOrNull(line.classId),
                                  Sql::idOrNull(line.itemId),
                                  Sql::idOrNull(line.taxComponentId),
                                  Sql::idOrNull(line.taxAgencyId),
                                  line.memo};
        for (int i = 0; i < values.size(); ++i) {
            insert.bindValue(i, values.at(i));
        }
        if (!insert.exec()) {
            return Result::failure(ErrorCode::Storage, QStringLiteral("could not write a journal line"));
        }
    }
    insert.finish();
    if (!Sql::exec(db, QStringLiteral("UPDATE documents SET txn_id = ? WHERE id = ?"), {txnId, docId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    if (overridden) {
        m_file.audit(QStringLiteral("lock-override"), QStringLiteral("transaction"), txnId, {},
                     compact({{QStringLiteral("date"), Sql::isoDate(date)},
                              {QStringLiteral("lock_date"), Sql::isoDate(lock)}}),
                     options.reason);
    }
    return Result::success(txnId);
}

Result Ledger::voidLiveTransaction(qint64 docId, const QString &reason, const PostOptions &options)
{
    const QSqlDatabase db = m_file.database();
    const qint64 txnId = Sql::scalarInt(db, QStringLiteral("SELECT txn_id FROM documents WHERE id = ?"), {docId});
    if (txnId == 0) {
        return Result::success(0); // a draft or estimate: nothing posted
    }
    const QDate date = Sql::fromIso(Sql::scalar(db, QStringLiteral("SELECT date FROM transactions WHERE id = ?"), {txnId}));
    if (const Result open = checkLock(date, options); !open.ok) {
        return open;
    }
    if (Sql::scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM journal_lines WHERE txn_id = ? AND cleared = 2"), {txnId})
        > 0) {
        return Result::failure(ErrorCode::Reconciled,
                               QStringLiteral("this transaction is part of a finished reconciliation; undo the "
                                              "reconciliation first"));
    }
    QString error;
    if (!Sql::exec(db, QStringLiteral("UPDATE transactions SET voided = 1, void_reason = ? WHERE id = ?"),
                   {reason, txnId}, &error)
        || !Sql::exec(db, QStringLiteral("UPDATE journal_lines SET voided = 1 WHERE txn_id = ?"), {txnId}, &error)
        || !Sql::exec(db, QStringLiteral("UPDATE documents SET txn_id = NULL WHERE id = ?"), {docId}, &error)
        || !Sql::exec(db, QStringLiteral("DELETE FROM reconciliation_marks WHERE line_id IN"
                                         " (SELECT id FROM journal_lines WHERE txn_id = ?)"),
                      {txnId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    return Result::success(txnId);
}

Result Ledger::voidDocument(qint64 docId, const QString &reason, const PostOptions &options)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> doc = documents.load(docId);
    if (!doc) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such document"));
    }
    if (doc->state == DocState::Void) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("already void"));
    }
    SqlTransaction txn(m_file);
    const Result voided = voidLiveTransaction(docId, reason, options);
    if (!voided.ok) {
        return voided;
    }
    if (const Result r = documents.voidApplicationsOf(docId); !r.ok) {
        return r;
    }
    if (const Result r = documents.setState(docId, DocState::Void); !r.ok) {
        return r;
    }
    m_file.audit(QStringLiteral("void"), QStringLiteral("document"), docId, compact(documents.toJson(*doc)), {},
                 reason);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(docId);
}

namespace {

QList<PostingLine> entryLines(const JournalEntryInput &entry, const QString &currency, Result *failure)
{
    QList<PostingLine> lines;
    for (const JournalLine &line : entry.lines) {
        if ((!line.debit.currency().isEmpty() && line.debit.currency() != currency)
            || (!line.credit.currency().isEmpty() && line.credit.currency() != currency)) {
            *failure = Result::failure(ErrorCode::CurrencyMismatch,
                                       QStringLiteral("journal lines must be in %1").arg(currency));
            return {};
        }
        PostingLine p;
        p.accountId = line.accountId;
        p.debit = line.debit.minor();
        p.credit = line.credit.minor();
        p.partyId = line.partyId;
        p.classId = line.classId;
        p.memo = line.memo;
        lines << p;
    }
    return lines;
}

BusinessDocument entryDocument(const JournalEntryInput &entry, const QString &currency)
{
    BusinessDocument doc;
    doc.kind = DocKind::JournalEntry;
    doc.date = entry.date;
    doc.number = entry.number;
    doc.memo = entry.memo;
    qint64 total = 0;
    for (const JournalLine &line : entry.lines) {
        DocumentLine l;
        l.accountId = line.accountId;
        l.debit = line.debit;
        l.credit = line.credit;
        l.partyId = line.partyId;
        l.classId = line.classId;
        l.description = line.memo;
        l.amount = line.debit.isZero() ? line.credit : line.debit;
        total += line.debit.minor();
        doc.lines << l;
    }
    doc.total = Money(total, currency);
    doc.subtotal = doc.total;
    return doc;
}

} // namespace

Result Ledger::postJournalEntry(const JournalEntryInput &entry, const PostOptions &options)
{
    Result failure;
    const QList<PostingLine> lines = entryLines(entry, m_file.currency(), &failure);
    if (lines.isEmpty()) {
        return failure.code == ErrorCode::None
            ? Result::failure(ErrorCode::Invalid, QStringLiteral("a journal entry needs lines"))
            : failure;
    }
    // Before the document write: a bad line must fail as itself, not as a
    // foreign-key error from document_lines.
    if (const Result valid = validateLines(lines); !valid.ok) {
        return valid;
    }
    SqlTransaction txn(m_file);
    BusinessDocument doc = entryDocument(entry, m_file.currency());
    Documents documents(m_file);
    if (const Result written = documents.write(doc); !written.ok) {
        return written;
    }
    if (const Result posted = postLines(doc.id, doc.kind, entry.date, entry.number, entry.memo, lines, options);
        !posted.ok) {
        return posted;
    }
    m_file.audit(QStringLiteral("post"), QStringLiteral("document"), doc.id, {}, compact(documents.toJson(doc)));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

Result Ledger::replaceJournalEntry(qint64 docId, const JournalEntryInput &entry, const QString &reason,
                                   const PostOptions &options)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> before = documents.load(docId);
    if (!before || before->kind != DocKind::JournalEntry) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such journal entry"));
    }
    if (before->state == DocState::Void) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("a void entry cannot be edited"));
    }
    Result failure;
    const QList<PostingLine> lines = entryLines(entry, m_file.currency(), &failure);
    if (lines.isEmpty()) {
        return failure.code == ErrorCode::None
            ? Result::failure(ErrorCode::Invalid, QStringLiteral("a journal entry needs lines"))
            : failure;
    }
    if (const Result valid = validateLines(lines); !valid.ok) {
        return valid;
    }
    SqlTransaction txn(m_file);
    const Result voided = voidLiveTransaction(docId, reason, options);
    if (!voided.ok) {
        return voided;
    }
    BusinessDocument doc = entryDocument(entry, m_file.currency());
    doc.id = docId;
    if (const Result written = documents.write(doc); !written.ok) {
        return written;
    }
    const Result posted = postLines(doc.id, doc.kind, entry.date, entry.number, entry.memo, lines, options);
    if (!posted.ok) {
        return posted;
    }
    Sql::exec(m_file.database(), QStringLiteral("UPDATE transactions SET replaced_by = ? WHERE id = ?"),
              {posted.id, voided.id});
    m_file.audit(QStringLiteral("edit"), QStringLiteral("document"), docId, compact(documents.toJson(*before)),
                 compact(documents.toJson(doc)), reason);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(docId);
}

std::optional<TransactionView> Ledger::transaction(qint64 txnId) const
{
    const QSqlDatabase db = m_file.database();
    TransactionView view;
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("SELECT id, doc_id, kind, date, number, memo, voided, void_reason, replaced_by,"
                                   " lock_override_reason, created_by FROM transactions WHERE id = ?"),
                 {txnId});
        if (!q.next()) {
            return std::nullopt;
        }
        view.id = q.value(0).toLongLong();
        view.docId = q.value(1).toLongLong();
        view.kind = DocKind(q.value(2).toInt());
        view.date = Sql::fromIso(q.value(3));
        view.number = q.value(4).toString();
        view.memo = q.value(5).toString();
        view.voided = q.value(6).toBool();
        view.voidReason = q.value(7).toString();
        view.replacedBy = q.value(8).toLongLong();
        view.lockOverrideReason = q.value(9).toString();
        view.createdBy = q.value(10).toString();
    }
    QSqlQuery q(db);
    Sql::run(q, QStringLiteral("SELECT l.id, l.account_id, a.name, l.debit, l.credit, l.party_id, l.class_id,"
                               " l.item_id, l.memo, l.cleared FROM journal_lines l"
                               " JOIN accounts a ON a.id = l.account_id WHERE l.txn_id = ? ORDER BY l.line_no"),
             {txnId});
    while (q.next()) {
        TransactionLineView line;
        line.id = q.value(0).toLongLong();
        line.accountId = q.value(1).toLongLong();
        line.accountName = q.value(2).toString();
        line.debit = m_file.money(q.value(3).toLongLong());
        line.credit = m_file.money(q.value(4).toLongLong());
        line.partyId = q.value(5).toLongLong();
        line.classId = q.value(6).toLongLong();
        line.itemId = q.value(7).toLongLong();
        line.memo = q.value(8).toString();
        line.cleared = q.value(9).toInt();
        view.lines << line;
    }
    return view;
}

QList<TransactionView> Ledger::transactionsForDocument(qint64 docId) const
{
    QList<qint64> ids;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM transactions WHERE doc_id = ? ORDER BY id"), {docId});
        while (q.next()) {
            ids << q.value(0).toLongLong();
        }
    }
    QList<TransactionView> out;
    for (qint64 id : ids) {
        if (const std::optional<TransactionView> view = transaction(id)) {
            out << *view;
        }
    }
    return out;
}

QList<AuditEntry> Ledger::auditTrail(const QString &entity, qint64 entityId) const
{
    QList<AuditEntry> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, at, user, action, entity, entity_id, before_json, after_json, reason"
                               " FROM audit_log WHERE (? = '' OR entity = ?) AND (? = 0 OR entity_id = ?)"
                               " ORDER BY id"),
             {entity, entity, entityId, entityId});
    while (q.next()) {
        out << AuditEntry{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                          q.value(3).toString(), q.value(4).toString(), q.value(5).toLongLong(),
                          q.value(6).toString(), q.value(7).toString(), q.value(8).toString()};
    }
    return out;
}

} // namespace QindaOffice::Books
