// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A fake Mercury + Stripe API on 127.0.0.1 for the M48 tests (no test
// touches the real network). Responses are shaped exactly like the
// documented ones (mercury_client.h, stripe_client.h name the fields):
//   Mercury under /mercury/api/v1: GET /accounts,
//     GET /account/{id}/transactions (start, limit, offset, order).
//   Stripe under /stripe: GET /v1/balance, /v1/account,
//     /v1/balance_transactions (created[gte], starting_after, limit,
//     expand[]), POST /v1/prices, /v1/payment_links (Idempotency-Key
//     replays the first answer, as Stripe does).
// `pageCap` makes the server return fewer items than asked, so pagination
// is exercised with a handful of fixtures. Credentials are checked the way
// the real APIs do (Bearer header), and a refusal echoes the key back in
// its error message — the client must redact it.

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <memory>

namespace BooksConnectTest {

struct Request {
    QByteArray method;
    QString path;
    QHash<QString, QString> query; // decoded
    QHash<QByteArray, QByteArray> headers;
    QHash<QString, QString> form;  // decoded POST body
    QByteArray body;
};

class FakeBankApi final : public QTcpServer {
public:
    FakeBankApi() { listen(QHostAddress::LocalHost, 0); }

    [[nodiscard]] QUrl mercuryBase() const { return base(QStringLiteral("/mercury/api/v1")); }
    [[nodiscard]] QUrl stripeBase() const { return base(QStringLiteral("/stripe")); }

    // ---- state the tests set ----
    QString mercuryToken = QStringLiteral("secret-token:mercury_production_abcdef0123456789");
    QJsonArray mercuryAccounts;
    QHash<QString, QJsonArray> mercuryTransactions; // by account id
    QString stripeKey = QStringLiteral("rk_test_51FakeRestrictedKey0123456789abcdef");
    QJsonObject stripeBalance{{QStringLiteral("object"), QStringLiteral("balance")},
                              {QStringLiteral("livemode"), false},
                              {QStringLiteral("available"), QJsonArray()},
                              {QStringLiteral("pending"), QJsonArray()}};
    QString stripeDisplayName = QStringLiteral("Sloom Test Shop");
    QJsonArray stripeTransactions; // any order; served newest first
    int pageCap = 1000;
    QList<Request> requests;
    int pricesCreated = 0;
    int linksCreated = 0;

    [[nodiscard]] QList<Request> requestsTo(const QString &path) const
    {
        QList<Request> out;
        for (const Request &r : requests) {
            if (r.path.endsWith(path)) out << r;
        }
        return out;
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            serve(socket, buffer.get());
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    [[nodiscard]] QUrl base(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }

    static QHash<QString, QString> decode(const QByteArray &encoded)
    {
        QHash<QString, QString> out;
        for (const QByteArray &pair : encoded.split('&')) {
            if (pair.isEmpty()) continue;
            const qsizetype eq = pair.indexOf('=');
            const QByteArray k = eq < 0 ? pair : pair.left(eq);
            const QByteArray v = eq < 0 ? QByteArray() : pair.mid(eq + 1);
            out.insert(QString::fromUtf8(QByteArray::fromPercentEncoding(QByteArray(k).replace('+', ' '))),
                       QString::fromUtf8(QByteArray::fromPercentEncoding(QByteArray(v).replace('+', ' '))));
        }
        return out;
    }

    static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

    void serve(QTcpSocket *socket, QByteArray *buffer)
    {
        const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        Request r;
        const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        r.method = first.value(0);
        const QByteArray target = first.value(1);
        const qsizetype q = target.indexOf('?');
        r.path = QString::fromUtf8(q < 0 ? target : target.left(q));
        r.query = decode(q < 0 ? QByteArray() : target.mid(q + 1));
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0) r.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
        }
        const qsizetype length = r.headers.value("content-length").toLongLong();
        if (buffer->size() < headerEnd + 4 + length) return;
        r.body = buffer->mid(headerEnd + 4, length);
        r.form = decode(r.body);
        buffer->clear();
        requests << r;
        int status = 200;
        const QByteArray body = route(r, &status);
        QByteArray reply = "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\n";
        reply += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        socket->write(reply);
        socket->disconnectFromHost();
    }

    QByteArray route(const Request &r, int *status)
    {
        const QByteArray auth = r.headers.value("authorization");
        if (r.path.startsWith(QLatin1String("/mercury/"))) {
            if (auth != "Bearer " + mercuryToken.toUtf8()) {
                *status = 401;
                return json({{QStringLiteral("errors"), QJsonObject{{QStringLiteral("message"),
                                                                     QStringLiteral("Unauthorized: ") + QString::fromUtf8(auth)}}}});
            }
            return mercury(r, status);
        }
        if (r.path.startsWith(QLatin1String("/stripe/"))) {
            if (auth != "Bearer " + stripeKey.toUtf8()) {
                *status = 401;
                return json({{QStringLiteral("error"),
                              QJsonObject{{QStringLiteral("message"),
                                           QStringLiteral("Invalid API Key provided: ") + QString::fromUtf8(auth.mid(7))},
                                          {QStringLiteral("type"), QStringLiteral("invalid_request_error")}}}});
            }
            return stripe(r, status);
        }
        *status = 404;
        return "{}";
    }

    QByteArray mercury(const Request &r, int *status)
    {
        const QString path = r.path.mid(QStringLiteral("/mercury/api/v1").size());
        if (path == QLatin1String("/accounts")) {
            return json({{QStringLiteral("accounts"), mercuryAccounts},
                         {QStringLiteral("page"), QJsonObject{{QStringLiteral("nextPage"), QJsonValue()},
                                                              {QStringLiteral("previousPage"), QJsonValue()}}}});
        }
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() == 3 && parts[0] == QLatin1String("account") && parts[2] == QLatin1String("transactions")) {
            const QString start = r.query.value(QStringLiteral("start"));
            QList<QJsonObject> matching;
            for (const QJsonValue &v : mercuryTransactions.value(parts[1])) {
                const QJsonObject t = v.toObject();
                if (start.isEmpty() || t.value(QStringLiteral("createdAt")).toString().left(10) >= start) {
                    matching << t;
                }
            }
            std::sort(matching.begin(), matching.end(), [](const QJsonObject &a, const QJsonObject &b) {
                return a.value(QStringLiteral("createdAt")).toString() < b.value(QStringLiteral("createdAt")).toString();
            });
            const int offset = r.query.value(QStringLiteral("offset")).toInt();
            const int limit = qMin(pageCap, r.query.value(QStringLiteral("limit"), QStringLiteral("1000")).toInt());
            QJsonArray page;
            for (int i = offset; i < matching.size() && page.size() < limit; ++i) page.append(matching[i]);
            return json({{QStringLiteral("total"), int(matching.size())}, {QStringLiteral("transactions"), page}});
        }
        *status = 404;
        return "{}";
    }

    QByteArray stripe(const Request &r, int *status)
    {
        const QString path = r.path.mid(QStringLiteral("/stripe").size());
        if (path == QLatin1String("/v1/balance")) return json(stripeBalance);
        if (path == QLatin1String("/v1/account")) {
            return json({{QStringLiteral("id"), QStringLiteral("acct_1Fake")},
                         {QStringLiteral("settings"), QJsonObject{{QStringLiteral("dashboard"),
                                                                   QJsonObject{{QStringLiteral("display_name"), stripeDisplayName}}}}}});
        }
        if (path == QLatin1String("/v1/balance_transactions")) {
            QList<QJsonObject> all;
            const qint64 gte = r.query.value(QStringLiteral("created[gte]")).toLongLong();
            for (const QJsonValue &v : stripeTransactions) {
                if (v.toObject().value(QStringLiteral("created")).toInteger() >= gte) all << v.toObject();
            }
            std::sort(all.begin(), all.end(), [](const QJsonObject &a, const QJsonObject &b) {
                const qint64 ca = a.value(QStringLiteral("created")).toInteger();
                const qint64 cb = b.value(QStringLiteral("created")).toInteger();
                return ca != cb ? ca > cb : a.value(QStringLiteral("id")).toString() > b.value(QStringLiteral("id")).toString();
            });
            qsizetype from = 0;
            const QString after = r.query.value(QStringLiteral("starting_after"));
            if (!after.isEmpty()) {
                while (from < all.size() && all[from].value(QStringLiteral("id")).toString() != after) ++from;
                ++from;
            }
            const int limit = qMin(pageCap, r.query.value(QStringLiteral("limit"), QStringLiteral("10")).toInt());
            QJsonArray data;
            qsizetype i = from;
            for (; i < all.size() && data.size() < limit; ++i) data.append(all[i]);
            return json({{QStringLiteral("object"), QStringLiteral("list")},
                         {QStringLiteral("url"), QStringLiteral("/v1/balance_transactions")},
                         {QStringLiteral("has_more"), i < all.size()},
                         {QStringLiteral("data"), data}});
        }
        const QByteArray key = r.headers.value("idempotency-key");
        if (!key.isEmpty() && m_replays.contains(key)) return m_replays.value(key);
        QByteArray out;
        if (path == QLatin1String("/v1/prices") && r.method == "POST") {
            ++pricesCreated;
            out = json({{QStringLiteral("id"), QStringLiteral("price_fake%1").arg(pricesCreated)},
                        {QStringLiteral("object"), QStringLiteral("price")},
                        {QStringLiteral("unit_amount"), r.form.value(QStringLiteral("unit_amount")).toLongLong()},
                        {QStringLiteral("currency"), r.form.value(QStringLiteral("currency"))}});
        } else if (path == QLatin1String("/v1/payment_links") && r.method == "POST") {
            ++linksCreated;
            out = json({{QStringLiteral("id"), QStringLiteral("plink_fake%1").arg(linksCreated)},
                        {QStringLiteral("object"), QStringLiteral("payment_link")},
                        {QStringLiteral("active"), true},
                        {QStringLiteral("url"), QStringLiteral("https://buy.stripe.com/test_fake%1").arg(linksCreated)}});
        } else {
            *status = 404;
            return "{}";
        }
        if (!key.isEmpty()) m_replays.insert(key, out);
        return out;
    }

    QHash<QByteArray, QByteArray> m_replays;
};

// ---- fixture builders (documented shapes) ----

inline QJsonObject mercuryAccount(const QString &id, const QString &name, const QString &number, double available)
{
    return {{QStringLiteral("id"), id},           {QStringLiteral("name"), name},
            {QStringLiteral("nickname"), QJsonValue()}, {QStringLiteral("kind"), QStringLiteral("checking")},
            {QStringLiteral("type"), QStringLiteral("mercury")}, {QStringLiteral("status"), QStringLiteral("active")},
            {QStringLiteral("accountNumber"), number}, {QStringLiteral("routingNumber"), QStringLiteral("021000021")},
            {QStringLiteral("availableBalance"), available}, {QStringLiteral("currentBalance"), available},
            {QStringLiteral("legalBusinessName"), QStringLiteral("Test Books LLC")},
            {QStringLiteral("createdAt"), QStringLiteral("2025-01-02T10:00:00Z")}};
}

// `postedAt` empty = not posted (null).
inline QJsonObject mercuryTxn(const QString &id, double amount, const QString &status, const QString &createdAt,
                              const QString &postedAt, const QString &counterparty, const QString &description)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("accountId"), QStringLiteral("acc-1")},
            {QStringLiteral("amount"), amount},
            {QStringLiteral("status"), status},
            {QStringLiteral("kind"), amount >= 0 ? QStringLiteral("externalTransfer") : QStringLiteral("debitCardTransaction")},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("postedAt"), postedAt.isEmpty() ? QJsonValue() : QJsonValue(postedAt)},
            {QStringLiteral("estimatedDeliveryDate"), createdAt},
            {QStringLiteral("failedAt"), QJsonValue()},
            {QStringLiteral("counterpartyId"), QStringLiteral("cp-") + id},
            {QStringLiteral("counterpartyName"), counterparty},
            {QStringLiteral("counterpartyNickname"), QJsonValue()},
            {QStringLiteral("bankDescription"), description},
            {QStringLiteral("note"), QJsonValue()},
            {QStringLiteral("externalMemo"), QJsonValue()},
            {QStringLiteral("dashboardLink"), QStringLiteral("https://app.mercury.com/transactions/") + id},
            {QStringLiteral("checkNumber"), QJsonValue()}};
}

inline QJsonObject stripeTxn(const QString &id, const QString &type, qint64 amount, qint64 fee, qint64 created,
                             const QJsonObject &source, const QString &description = {})
{
    QJsonArray feeDetails;
    if (fee != 0) {
        feeDetails.append(QJsonObject{{QStringLiteral("amount"), fee}, {QStringLiteral("currency"), QStringLiteral("usd")},
                                      {QStringLiteral("description"), QStringLiteral("Stripe processing fees")},
                                      {QStringLiteral("type"), QStringLiteral("stripe_fee")}});
    }
    return {{QStringLiteral("id"), id},
            {QStringLiteral("object"), QStringLiteral("balance_transaction")},
            {QStringLiteral("amount"), amount},
            {QStringLiteral("available_on"), created + 2 * 86400},
            {QStringLiteral("created"), created},
            {QStringLiteral("currency"), QStringLiteral("usd")},
            {QStringLiteral("description"), description.isEmpty() ? QJsonValue() : QJsonValue(description)},
            {QStringLiteral("exchange_rate"), QJsonValue()},
            {QStringLiteral("fee"), fee},
            {QStringLiteral("fee_details"), feeDetails},
            {QStringLiteral("net"), amount - fee},
            {QStringLiteral("reporting_category"), type},
            {QStringLiteral("source"), source},
            {QStringLiteral("status"), QStringLiteral("available")},
            {QStringLiteral("type"), type}};
}

inline QJsonObject stripeCharge(const QString &id, const QString &name, const QJsonObject &metadata = {})
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("object"), QStringLiteral("charge")},
            {QStringLiteral("billing_details"), QJsonObject{{QStringLiteral("name"), name}}},
            {QStringLiteral("metadata"), metadata},
            {QStringLiteral("payment_intent"), QStringLiteral("pi_") + id.mid(3)}};
}

inline QJsonObject stripePayout(const QString &id, qint64 arrival)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("object"), QStringLiteral("payout")},
            {QStringLiteral("arrival_date"), arrival},
            {QStringLiteral("status"), QStringLiteral("paid")},
            {QStringLiteral("destination"), QStringLiteral("ba_1Fake")}};
}

} // namespace BooksConnectTest
