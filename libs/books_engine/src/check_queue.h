// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The "to be printed" check queue (M68, D-077; docs/books.md "Check
// printing"), as QuickBooks keeps it: a check, or a bill payment drawn on a
// bank account, can be saved to print later (`documents.to_print`). It
// posts at once, without a number. Printing the queue gives the chosen
// checks consecutive numbers from the account's next check number; the
// user then says which printed correctly. A misprint goes back to the queue
// without its number and that number is RECORDED VOID — a void check
// document with the number, the payee and the bank account (its
// `sourceDocId` names the payment it was printed for) — so the checkbook's
// sequence has no gap; the reprint takes the next numbers.
//
// AGENT-GUARD (immutable postings, D-045): a posted transaction's number
// cannot change (SQL trigger), so giving or taking a number is the ledger's
// edit: the live transaction is voided and the SAME lines are posted again
// under the new number, in one SQL transaction (renumber()). The document
// keeps its id, so a bill payment's applications stay. A check whose lines
// have cleared or been reconciled is never renumbered, and the lock date
// applies as to any edit.
//
// The next number per bank account is stored in settings
// (`next_check_number:<accountId>`) and never goes below one past the
// highest numeric check number the account has used (void records
// included), so a hand-written check moves it on.

#include "books_types.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

namespace QindaOffice::Books {

class CompanyFile;

struct QueuedCheck {
    qint64 docId = 0;
    DocKind kind = DocKind::Check; // Check or BillPayment
    qint64 accountId = 0;
    QDate date;
    qint64 payeeId = 0;
    Money amount;
    QString number;                // "" while it waits in the queue
};

class CheckQueue {
public:
    explicit CheckQueue(CompanyFile &file);

    // Whether `docId` may wait in the queue: a live Check or Bill Payment
    // drawn on a bank account.
    [[nodiscard]] static bool canQueue(CompanyFile &file, qint64 docId, QString *why = nullptr);
    // Puts a check in the queue (its number is replaced when it prints) or
    // takes it out. Audited.
    Result setToPrint(qint64 docId, bool toPrint);
    // The checks waiting to print — of one bank account, or of every one
    // (0) — by date, then id.
    [[nodiscard]] QList<QueuedCheck> queue(qint64 bankAccountId = 0) const;
    [[nodiscard]] qint64 nextNumber(qint64 bankAccountId) const;

    // Numbers `docIds` (queued checks of `bankAccountId`, in that order)
    // first, first + 1, …, takes them out of the queue and moves the
    // account's next number past them. One SQL transaction: a number
    // another check of the account already uses refuses the whole run.
    // The numbered checks come back in `numbered`.
    Result assignNumbers(qint64 bankAccountId, const QList<qint64> &docIds, qint64 first,
                         QList<QueuedCheck> *numbered = nullptr);
    // After printing: `misprinted` checks go back to the queue without a
    // number; each number they had is recorded as a void check dated
    // `today`. The others stay printed. One SQL transaction.
    Result confirmPrinted(const QList<qint64> &misprinted, const QDate &today);
    // Nothing printed (the job was cancelled): the checks go back to the
    // queue without numbers and the account's next number returns to the
    // first of them. Nothing is recorded void.
    Result takeBack(qint64 bankAccountId, const QList<qint64> &docIds);

private:
    // Void + repost with `number` (check_queue.h AGENT-GUARD); sets the
    // document's number and to_print. Caller holds a SqlTransaction.
    Result renumber(qint64 docId, const QString &number, bool toPrint, const QString &reason);
    [[nodiscard]] qint64 highestUsed(qint64 bankAccountId) const;
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
