// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The per-provider settings and cursors books_connect keeps in the company
// file's `connections` row (connections.h), and the sync report every sync
// returns (M48, D-048).
//
// AGENT-CONTRACT (storage — JSON keys in connections.settings / .cursor;
// add keys freely, never rename one):
//  mercury settings: {accounts: [{id, name, kind, lastFour}], links:
//      {<mercuryAccountId>: <booksAccountId>}, startDate: "yyyy-MM-dd"}
//  mercury cursor:   {<mercuryAccountId>: {lastPostedAt: ISO time,
//      pendingSince: "yyyy-MM-dd"}}
//  stripe settings:  {clearingAccountId, feeAccountId, payoutAccountId,
//      currency ("usd"), autoPostFees, autoPostPayouts, autoApplyInvoices,
//      accountName, livemode, startDate}
//  stripe cursor:    {lastCreated: unix seconds, lastId: "txn_…"}
// Never a token (Connections::save refuses one).

#include "mercury_client.h"

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace QindaOffice::Books {
class CompanyFile;
}

namespace QindaOffice::Books::Connect {

// The window's current company file (null when none). Syncs ask it again
// after every network wait instead of holding a pointer across one: a Save
// As replaces the file (books_document.h).
using FileProvider = std::function<CompanyFile *()>;

struct SyncReport {
    bool ok = true;
    int imported = 0; // bank-feed lines added
    int applied = 0;  // Stripe payments applied to invoices
    int matched = 0;  // feed lines matched to postings (payouts, payments)
    int posted = 0;   // fee lines and payout transfers booked automatically
    QStringList errors;

    void fail(const QString &error)
    {
        ok = false;
        errors << error;
    }
    void merge(const SyncReport &other);
};

struct MercurySettings {
    QList<MercuryAccount> accounts; // the last list seen (balances not kept)
    QHash<QString, qint64> links;   // Mercury account id → QindaBooks bank account id
    QDate startDate;                // the first sync reaches back to here

    [[nodiscard]] static MercurySettings fromJson(const QJsonObject &json);
    [[nodiscard]] QJsonObject toJson() const;
};

struct StripeSettings {
    qint64 clearingAccountId = 0; // the "Stripe (USD)" bank-type account
    qint64 feeAccountId = 0;      // Merchant Fees (expense)
    qint64 payoutAccountId = 0;   // where payouts land (the Mercury account)
    QString currency;             // lower case, = the company currency
    bool autoPostFees = true;
    bool autoPostPayouts = true;
    bool autoApplyInvoices = true;
    QString accountName;
    bool livemode = false;
    QDate startDate;

    [[nodiscard]] static StripeSettings fromJson(const QJsonObject &json);
    [[nodiscard]] QJsonObject toJson() const;
};

// Days a first sync reaches back when no start date was chosen.
inline constexpr int kDefaultHistoryDays = 90;

} // namespace QindaOffice::Books::Connect
