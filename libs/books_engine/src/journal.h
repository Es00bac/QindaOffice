// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The journal: balanced, immutable double-entry transactions (D-045).
//
// Posting rules (docs/books.md "Posting rules"):
//  * Every posting document makes exactly one LIVE JournalTransaction;
//    debits equal credits, at least two lines, every account exists and is
//    active, all inside one SQL transaction with the document write.
//  * Posted lines never change (SQL triggers enforce it). Editing = void
//    the live transaction + post a new one in the same SQL transaction;
//    the old one keeps its lines, gets voided = 1 and replaced_by.
//  * A date on/before the lock date is refused with ClosedPeriod unless
//    PostOptions::overrideLock is set WITH a reason; the reason is stored
//    on the transaction and in audit_log.
//  * Void keeps the document and its transaction with zero effect (every
//    balance query filters voided = 0), unapplies its applications, and is
//    audited with before/after JSON. A void cannot be undone.

#include "books_types.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

struct PostOptions {
    bool overrideLock = false;
    QString reason; // required when overrideLock
};

// A user journal-entry line: exactly one of debit/credit is non-zero.
struct JournalLine {
    qint64 accountId = 0;
    Money debit;
    Money credit;
    qint64 partyId = 0;
    qint64 classId = 0;
    QString memo;
};

struct JournalEntryInput {
    QDate date;
    QString number;
    QString memo;
    QList<JournalLine> lines;
};

// Engine-internal posting line (minor units of the company currency).
struct PostingLine {
    qint64 accountId = 0;
    qint64 debit = 0;
    qint64 credit = 0;
    qint64 partyId = 0;
    qint64 classId = 0;
    qint64 itemId = 0;
    qint64 taxComponentId = 0;
    qint64 taxAgencyId = 0;
    QString memo;
};

struct TransactionLineView {
    qint64 id = 0;
    qint64 accountId = 0;
    QString accountName;
    Money debit;
    Money credit;
    qint64 partyId = 0;
    qint64 classId = 0;
    qint64 itemId = 0;
    QString memo;
    int cleared = 0; // 0 uncleared, 1 cleared, 2 reconciled
};

struct TransactionView {
    qint64 id = 0;
    qint64 docId = 0;
    DocKind kind = DocKind::JournalEntry;
    QDate date;
    QString number;
    QString memo;
    bool voided = false;
    QString voidReason;
    qint64 replacedBy = 0;
    QString lockOverrideReason;
    QString createdBy;
    QList<TransactionLineView> lines;
};

struct AuditEntry {
    qint64 id = 0;
    QString at;
    QString user;
    QString action;
    QString entity;
    qint64 entityId = 0;
    QString beforeJson;
    QString afterJson;
    QString reason;
};

class Ledger {
public:
    explicit Ledger(CompanyFile &file);

    // ---- user journal entries (returns the document id) ----
    Result postJournalEntry(const JournalEntryInput &entry, const PostOptions &options = {});
    // Void + repost in one step; the document id stays the same.
    Result replaceJournalEntry(qint64 docId, const JournalEntryInput &entry,
                               const QString &reason, const PostOptions &options = {});
    // Voids any posting document (invoice, bill, payment, JE …).
    Result voidDocument(qint64 docId, const QString &reason, const PostOptions &options = {});

    [[nodiscard]] std::optional<TransactionView> transaction(qint64 txnId) const;
    // Live and voided transactions of a document, oldest first.
    [[nodiscard]] QList<TransactionView> transactionsForDocument(qint64 docId) const;
    [[nodiscard]] QList<AuditEntry> auditTrail(const QString &entity = {}, qint64 entityId = 0) const;

    // ---- engine-internal: callers hold a SqlTransaction ----
    // Validates and writes a transaction + lines, links documents.txn_id.
    // Result.id = transaction id.
    Result postLines(qint64 docId, DocKind kind, const QDate &date, const QString &number,
                     const QString &memo, const QList<PostingLine> &lines,
                     const PostOptions &options);
    // Voids the document's live transaction (if any) without touching the
    // document state; used by edit (void + repost) and by voidDocument.
    Result voidLiveTransaction(qint64 docId, const QString &reason, const PostOptions &options);
    [[nodiscard]] Result checkLock(const QDate &date, const PostOptions &options) const;
    [[nodiscard]] static Result validateBalanced(const QList<PostingLine> &lines);
    // validateBalanced + every account exists and is active.
    [[nodiscard]] Result validateLines(const QList<PostingLine> &lines) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
