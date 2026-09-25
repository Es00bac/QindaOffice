// SPDX-License-Identifier: GPL-3.0-or-later
#include "stripe_client.h"

#include <QJsonArray>
#include <QNetworkAccessManager>

namespace QindaOffice::Books::Connect {

namespace {

QByteArray formEncode(const QList<QPair<QString, QString>> &params)
{
    QByteArray out;
    for (const auto &[key, value] : params) {
        if (!out.isEmpty()) {
            out += '&';
        }
        out += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return out;
}

void explain(ApiResponse *r, const QString &key)
{
    const QString message = ConnectHttp::redact(
        r->json.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(), key);
    if (r->kind == ApiResponse::Kind::Unauthorized && r->status == 401) {
        r->error = QStringLiteral("Stripe did not accept the key (HTTP 401). Create a restricted key in the Stripe "
                                  "Dashboard under Developers ▸ API keys and paste it again.");
    } else if (r->kind == ApiResponse::Kind::Unauthorized) {
        r->error = QStringLiteral("The Stripe key is missing a permission (HTTP %1)%2")
                       .arg(r->status)
                       .arg(message.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + message);
    } else if (r->kind == ApiResponse::Kind::HttpError && !message.isEmpty()) {
        r->error = QStringLiteral("Stripe: %1 (HTTP %2)").arg(message).arg(r->status);
    }
}

QHash<QString, qint64> amounts(const QJsonArray &list)
{
    QHash<QString, qint64> out;
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        out[o.value(QStringLiteral("currency")).toString().toLower()] +=
            o.value(QStringLiteral("amount")).toInteger();
    }
    return out;
}

StripeBalanceTransaction readTransaction(const QJsonObject &o)
{
    StripeBalanceTransaction t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.type = o.value(QStringLiteral("type")).toString();
    t.currency = o.value(QStringLiteral("currency")).toString().toLower();
    // AGENT-NOTE: Stripe amounts are integers in the smallest unit —
    // toInteger() reads them exactly (no double on the money path).
    t.amount = o.value(QStringLiteral("amount")).toInteger();
    t.fee = o.value(QStringLiteral("fee")).toInteger();
    t.net = o.value(QStringLiteral("net")).toInteger();
    t.created = o.value(QStringLiteral("created")).toInteger();
    t.availableOn = o.value(QStringLiteral("available_on")).toInteger();
    t.description = o.value(QStringLiteral("description")).toString();
    t.reportingCategory = o.value(QStringLiteral("reporting_category")).toString();
    t.status = o.value(QStringLiteral("status")).toString();
    const QJsonValue source = o.value(QStringLiteral("source"));
    if (source.isObject()) {
        t.source = source.toObject();
        t.sourceId = t.source.value(QStringLiteral("id")).toString();
    } else {
        t.sourceId = source.toString();
    }
    return t;
}

} // namespace

StripeClient::StripeClient(QNetworkAccessManager *manager, QUrl baseUrl, QString key, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_base(std::move(baseUrl))
    , m_key(key.trimmed())
{
}

QUrl StripeClient::defaultBaseUrl()
{
    return QUrl(QStringLiteral("https://api.stripe.com"));
}

bool StripeClient::keyIsLive(const QString &key)
{
    return key.contains(QStringLiteral("_live_"));
}

void StripeClient::call(const QByteArray &verb, const QString &path, const QList<QPair<QString, QString>> &params,
                        const QString &idempotencyKey, std::function<void(const ApiResponse &)> done)
{
    QUrl url = m_base;
    url.setPath(m_base.path() + path);
    QByteArray body;
    if (verb == "GET") {
        url.setQuery(QString::fromLatin1(formEncode(params)));
    } else {
        body = formEncode(params);
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    request.setRawHeader("Accept", "application/json");
    if (verb != "GET") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    }
    if (!idempotencyKey.isEmpty()) {
        request.setRawHeader("Idempotency-Key", idempotencyKey.toUtf8());
    }
    const QString key = m_key;
    ConnectHttp::send(m_manager, request, verb, body, m_key, this, [done, key](const ApiResponse &response) {
        ApiResponse r = response;
        explain(&r, key);
        done(r);
    });
}

void StripeClient::balance(std::function<void(const ApiResponse &, const StripeBalance &)> done)
{
    call("GET", QStringLiteral("/v1/balance"), {}, {}, [done](const ApiResponse &r) {
        StripeBalance b;
        if (r.ok()) {
            const QJsonObject o = r.json.object();
            b.livemode = o.value(QStringLiteral("livemode")).toBool();
            b.available = amounts(o.value(QStringLiteral("available")).toArray());
            b.pending = amounts(o.value(QStringLiteral("pending")).toArray());
        }
        done(r, b);
    });
}

void StripeClient::accountName(std::function<void(const ApiResponse &, const QString &)> done)
{
    call("GET", QStringLiteral("/v1/account"), {}, {}, [done](const ApiResponse &r) {
        QString name;
        if (r.ok()) {
            const QJsonObject o = r.json.object();
            name = o.value(QStringLiteral("settings")).toObject().value(QStringLiteral("dashboard")).toObject()
                       .value(QStringLiteral("display_name")).toString();
            if (name.isEmpty()) {
                name = o.value(QStringLiteral("business_profile")).toObject().value(QStringLiteral("name")).toString();
            }
            if (name.isEmpty()) {
                name = o.value(QStringLiteral("email")).toString();
            }
        }
        done(r, name);
    });
}

void StripeClient::balanceTransactions(qint64 createdGte, const QString &startingAfter, int limit,
                                       std::function<void(const ApiResponse &, const StripePage &)> done)
{
    QList<QPair<QString, QString>> params{{QStringLiteral("limit"), QString::number(limit)},
                                          {QStringLiteral("created[gte]"), QString::number(createdGte)},
                                          {QStringLiteral("expand[]"), QStringLiteral("data.source")}};
    if (!startingAfter.isEmpty()) {
        params.append({QStringLiteral("starting_after"), startingAfter});
    }
    call("GET", QStringLiteral("/v1/balance_transactions"), params, {}, [done](const ApiResponse &response) {
        ApiResponse r = response;
        StripePage page;
        if (r.ok()) {
            const QJsonObject o = r.json.object();
            page.hasMore = o.value(QStringLiteral("has_more")).toBool();
            for (const QJsonValue &v : o.value(QStringLiteral("data")).toArray()) {
                const StripeBalanceTransaction t = readTransaction(v.toObject());
                if (t.id.isEmpty() || t.currency.isEmpty()) {
                    r.kind = ApiResponse::Kind::BadJson;
                    r.error = QStringLiteral("Stripe sent a balance transaction without an id or a currency.");
                    break;
                }
                page.data << t;
            }
        }
        done(r, page);
    });
}

void StripeClient::createPrice(const QString &currency, qint64 unitAmount, const QString &productName,
                               const QString &idempotencyKey,
                               std::function<void(const ApiResponse &, const QString &)> done)
{
    call("POST", QStringLiteral("/v1/prices"),
         {{QStringLiteral("currency"), currency.toLower()},
          {QStringLiteral("unit_amount"), QString::number(unitAmount)},
          {QStringLiteral("product_data[name]"), productName}},
         idempotencyKey,
         [done](const ApiResponse &r) { done(r, r.ok() ? r.json.object().value(QStringLiteral("id")).toString() : QString()); });
}

void StripeClient::createPaymentLink(const QString &priceId, const QList<QPair<QString, QString>> &fields,
                                     const QString &idempotencyKey,
                                     std::function<void(const ApiResponse &, const QString &, const QString &)> done)
{
    QList<QPair<QString, QString>> params{{QStringLiteral("line_items[0][price]"), priceId},
                                          {QStringLiteral("line_items[0][quantity]"), QStringLiteral("1")}};
    params.append(fields);
    call("POST", QStringLiteral("/v1/payment_links"), params, idempotencyKey, [done](const ApiResponse &r) {
        const QJsonObject o = r.json.object();
        done(r, r.ok() ? o.value(QStringLiteral("id")).toString() : QString(),
             r.ok() ? o.value(QStringLiteral("url")).toString() : QString());
    });
}

} // namespace QindaOffice::Books::Connect
