// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// StripeSync: brings Stripe's balance history into the books since the
// cursor (M48, D-048). Pages through GET /v1/balance_transactions
// (created[gte] = the cursor's lastCreated, or the start date on the first
// sync; limit 100; starting_after = the last id of each page; sources
// expanded), then posts the collected transactions OLDEST FIRST through
// StripePosting (stripe_posting.h has the accounting model) and finally
// matches Stripe payout deposits already in the bank feeds
// (payout_matching.h). The cursor moves to the newest transaction seen;
// the one-second overlap on the next sync is absorbed by FITID dedup.

#include "connect_settings.h"
#include "stripe_client.h"

#include <QObject>
#include <QPointer>

namespace QindaOffice::Books::Connect {

class StripeSync : public QObject {
    Q_OBJECT
public:
    StripeSync(StripeClient *client, FileProvider file, QDate today, QObject *parent = nullptr);

    void run(std::function<void(const SyncReport &)> done);

    static constexpr int kPageSize = 100;

private:
    void fetch(const QString &startingAfter);
    void post();
    void finish();

    QPointer<StripeClient> m_client;
    FileProvider m_file;
    QDate m_today;
    std::function<void(const SyncReport &)> m_done;
    SyncReport m_report;
    QJsonObject m_cursor;
    qint64 m_createdGte = 0;
    QList<StripeBalanceTransaction> m_collected;
};

} // namespace QindaOffice::Books::Connect
