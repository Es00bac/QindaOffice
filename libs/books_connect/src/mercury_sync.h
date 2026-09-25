// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MercurySync: brings every linked Mercury account's POSTED transactions
// into its QindaBooks bank account's feed (M48, D-048).
//
//  * Only status "sent" with a postedAt time is imported; pending, failed,
//    cancelled, reversed and blocked are skipped. A pending transaction
//    that posts later is imported then, once (FITID = the Mercury id, the
//    feed dedups on it).
//  * Line: date = postedAt (local date), amount = Mercury's signed amount,
//    payee = counterparty name, memo = bank description + note/memo,
//    check number when Mercury has one.
//  * After each account: bank rules run on the new lines and Stripe payout
//    deposits are matched to their transfers (payout_matching.h).
//  * Cursor per account (connect_settings.h): the newest postedAt seen
//    and the creation date of the oldest still-pending transaction. The
//    next sync asks from min(lastPostedAt − 7 days, pendingSince) — the
//    overlap costs nothing (dedup) and covers late postings.
//
// Asynchronous: run() pages through GET /account/{id}/transactions
// (limit 500, offset paging until `total`) and writes to the file only
// between network waits, asking the FileProvider each time.

#include "connect_settings.h"

#include <QObject>
#include <QPointer>

namespace QindaOffice::Books::Connect {

class MercuryClient;

class MercurySync : public QObject {
    Q_OBJECT
public:
    MercurySync(MercuryClient *client, FileProvider file, QDate today, QObject *parent = nullptr);

    // Calls `done` once. Deletes nothing; the caller owns this object.
    void run(std::function<void(const SyncReport &)> done);

    static constexpr int kPageSize = 500;

private:
    void nextAccount();
    void fetch(int offset);
    void importCollected();
    void finish();

    QPointer<MercuryClient> m_client;
    FileProvider m_file;
    QDate m_today;
    std::function<void(const SyncReport &)> m_done;
    SyncReport m_report;
    QJsonObject m_cursor;
    QList<QPair<QString, qint64>> m_queue; // (Mercury id, books account id)
    QString m_accountId;
    qint64 m_booksAccountId = 0;
    QList<MercuryTransaction> m_collected;
};

} // namespace QindaOffice::Books::Connect
