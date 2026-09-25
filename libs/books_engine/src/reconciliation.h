// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reconciliation: agree a bank or credit-card account with its statement
// (docs/books.md "Reconciliation").
//
// A session names the statement date and ending balance. Its beginning
// balance is the sum of everything reconciled before. The user marks the
// lines that appear on the statement; finish() is refused until
//   ending − (beginning + Σ marked) == 0
// and then stamps the marked journal lines reconciled (cleared = 2, with
// the session id). undoLast() reverses the most recent finished session.
// All amounts are in the account's NORMAL sign (a card's balance owed is
// positive), matching the statement the user holds.
//
// AGENT-GUARD: reconciled lines make their transaction un-voidable
// (Ledger::voidLiveTransaction returns Reconciled) — undo the
// reconciliation first. Otherwise the statement would silently disagree.

#include "books_types.h"
#include "money.h"
#include "report_table.h"

#include <QDate>
#include <QList>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class ReconciliationState { InProgress = 0, Finished = 1, Undone = 2 };

struct ReconciliationSession {
    qint64 id = 0;
    qint64 accountId = 0;
    QDate statementDate;
    Money beginningBalance;
    Money endingBalance;
    ReconciliationState state = ReconciliationState::InProgress;
    QString finishedAt;
};

struct ReconcileCandidate {
    qint64 lineId = 0;
    qint64 docId = 0;
    QDate date;
    DocKind kind = DocKind::JournalEntry;
    QString number;
    QString payee;
    QString memo;
    Money amount; // normal sign: + increases the balance
    bool marked = false;
};

class Reconciliation {
public:
    explicit Reconciliation(CompanyFile &file);

    // Refused while another session of the account is in progress.
    Result start(qint64 accountId, const QDate &statementDate, const Money &endingBalance);
    [[nodiscard]] std::optional<ReconciliationSession> session(qint64 id) const;
    [[nodiscard]] std::optional<ReconciliationSession> inProgress(qint64 accountId) const;
    [[nodiscard]] QList<ReconciliationSession> history(qint64 accountId) const;

    // Unreconciled live lines of the account dated on/before the statement.
    [[nodiscard]] QList<ReconcileCandidate> candidates(qint64 sessionId) const;
    Result mark(qint64 sessionId, const QList<qint64> &lineIds, bool marked);
    [[nodiscard]] Money clearedBalance(qint64 sessionId) const;
    [[nodiscard]] Money difference(qint64 sessionId) const;
    // NotZero unless difference() is exactly zero.
    Result finish(qint64 sessionId);
    Result cancel(qint64 sessionId);
    Result undoLast(qint64 accountId);

    // Beginning, cleared deposits/payments with their lines, ending, and
    // the items still uncleared as of the statement date.
    [[nodiscard]] ReportTable report(qint64 sessionId) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
