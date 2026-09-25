// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// StripeClient: the Stripe API calls QindaBooks makes (M48, D-048),
// asynchronous over Qt Network.
//
// AGENT-CONTRACT (Stripe API, docs.stripe.com/api, checked 2026-09-25;
// no Stripe-Version header is sent, so the account's default API version
// answers — every field below exists in all current versions):
//  * Base https://api.stripe.com; `Authorization: Bearer <key>`; a
//    restricted key (rk_live_/rk_test_) is recommended (docs/books.md
//    lists the permissions). POST bodies are form-encoded with bracketed
//    keys (`metadata[k]=v`).
//  * GET /v1/balance → {object: "balance", livemode, available: [{amount,
//    currency}], pending: [{amount, currency}]}.
//  * GET /v1/account → {id, business_profile: {name}, settings:
//    {dashboard: {display_name}}, email} — optional (a restricted key may
//    lack the permission; the name then falls back).
//  * GET /v1/balance_transactions?limit=100&created[gte]=<unix>
//    &starting_after=<txn id>&expand[]=data.source → {object: "list",
//    data: [{id, amount (signed), fee, fee_details, net, currency,
//    created, available_on, description, reporting_category, status,
//    type, source}], has_more}. Newest first; pages go older through
//    starting_after.
//  * POST /v1/prices (currency, unit_amount, product_data[name]) →
//    {id}; POST /v1/payment_links (line_items[0][price],
//    line_items[0][quantity], metadata[…], payment_intent_data[metadata][…],
//    payment_intent_data[description], restrictions[completed_sessions]
//    [limit]) → {id, url, active}. Both with an Idempotency-Key header.
//  * Payment-intent metadata is copied onto the Charge it creates
//    (docs.stripe.com/metadata "Exceptions"), so an expanded charge
//    `source` carries qindabooks_invoice / qindabooks_company.

#include "connect_http.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;

namespace QindaOffice::Books::Connect {

struct StripeBalance {
    bool livemode = false;
    QHash<QString, qint64> available; // lower-case currency → minor units
    QHash<QString, qint64> pending;
};

struct StripeBalanceTransaction {
    QString id;       // txn_…
    QString type;     // charge, payment, refund, payout, stripe_fee, adjustment, …
    QString currency; // lower case
    qint64 amount = 0; // gross, signed (payouts and refunds are negative)
    qint64 fee = 0;    // positive when assessed
    qint64 net = 0;
    qint64 created = 0; // unix seconds
    qint64 availableOn = 0;
    QString description;
    QString reportingCategory;
    QString status;   // available | pending
    QString sourceId; // ch_…, re_…, po_…, …
    QJsonObject source; // the expanded object; empty when not expanded
};

struct StripePage {
    QList<StripeBalanceTransaction> data;
    bool hasMore = false;
};

class StripeClient : public QObject {
    Q_OBJECT
public:
    StripeClient(QNetworkAccessManager *manager, QUrl baseUrl, QString key, QObject *parent = nullptr);
    [[nodiscard]] static QUrl defaultBaseUrl();
    // rk_live_/sk_live_ → true, *_test_ → false (the key says its mode).
    [[nodiscard]] static bool keyIsLive(const QString &key);

    void balance(std::function<void(const ApiResponse &, const StripeBalance &)> done);
    // The business's display name ("" when the key may not read it).
    void accountName(std::function<void(const ApiResponse &, const QString &)> done);
    void balanceTransactions(qint64 createdGte, const QString &startingAfter, int limit,
                             std::function<void(const ApiResponse &, const StripePage &)> done);
    void createPrice(const QString &currency, qint64 unitAmount, const QString &productName,
                     const QString &idempotencyKey, std::function<void(const ApiResponse &, const QString &)> done);
    // `fields` are the extra form fields (metadata[…], payment_intent_data[…]).
    void createPaymentLink(const QString &priceId, const QList<QPair<QString, QString>> &fields,
                           const QString &idempotencyKey,
                           std::function<void(const ApiResponse &, const QString &id, const QString &url)> done);

private:
    void call(const QByteArray &verb, const QString &path, const QList<QPair<QString, QString>> &params,
              const QString &idempotencyKey, std::function<void(const ApiResponse &)> done);

    QPointer<QNetworkAccessManager> m_manager;
    QUrl m_base;
    QString m_key;
};

} // namespace QindaOffice::Books::Connect
