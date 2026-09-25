// SPDX-License-Identifier: GPL-3.0-or-later
#include "mercury_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrlQuery>

namespace QindaOffice::Books::Connect {

namespace {

QDateTime timeOf(const QJsonValue &value)
{
    return value.isString() ? QDateTime::fromString(value.toString(), Qt::ISODateWithMs) : QDateTime();
}

QString text(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString().trimmed() : QString();
}

// A friendlier sentence for the statuses people meet.
void explain(ApiResponse *r, const QString &token)
{
    if (r->kind == ApiResponse::Kind::Unauthorized) {
        r->error = QStringLiteral("Mercury did not accept the API token (HTTP %1). Create a read-only token in "
                                  "Mercury under Settings ▸ Tokens and paste it again.")
                       .arg(r->status);
    } else if (r->kind == ApiResponse::Kind::HttpError) {
        const QString message = r->json.object().value(QStringLiteral("errors")).toObject()
                                    .value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            // The server's text may echo the request; never the token.
            r->error = ConnectHttp::redact(QStringLiteral("Mercury: %1 (HTTP %2)").arg(message).arg(r->status), token);
        }
    }
}

} // namespace

bool jsonMoneyToMinor(const QJsonValue &value, int exponent, qint64 *minor)
{
    // AGENT-NOTE: the APIs send dollars as JSON numbers, which QJson reads
    // as a double. Printing it with three spare digits and requiring them
    // to be zero turns 1234.56 into exactly 123456 and refuses 12.345
    // instead of rounding it (no float arithmetic touches the amount).
    if (!value.isDouble()) {
        return false;
    }
    const QString printed = QString::number(value.toDouble(), 'f', exponent + 3);
    const qsizetype dot = printed.indexOf(QLatin1Char('.'));
    if (dot < 0 || !printed.endsWith(QStringLiteral("000"))) {
        return false;
    }
    QString digits = printed.left(dot) + printed.mid(dot + 1, exponent);
    bool ok = false;
    const qint64 v = digits.toLongLong(&ok);
    if (ok) {
        *minor = v;
    }
    return ok;
}

MercuryClient::MercuryClient(QNetworkAccessManager *manager, QUrl baseUrl, QString token, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_base(std::move(baseUrl))
    , m_token(token.trimmed())
{
}

QUrl MercuryClient::defaultBaseUrl()
{
    return QUrl(QStringLiteral("https://api.mercury.com/api/v1"));
}

void MercuryClient::get(const QString &path, const QList<QPair<QString, QString>> &query,
                        std::function<void(const ApiResponse &)> done)
{
    QUrl url = m_base;
    url.setPath(m_base.path() + path);
    QUrlQuery q;
    for (const auto &[key, value] : query) {
        q.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    }
    url.setQuery(q);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    request.setRawHeader("Accept", "application/json");
    const QString token = m_token;
    ConnectHttp::send(m_manager, request, "GET", {}, m_token, this, [done, token](const ApiResponse &response) {
        ApiResponse r = response;
        explain(&r, token);
        done(r);
    });
}

void MercuryClient::accounts(AccountsDone done)
{
    get(QStringLiteral("/accounts"), {}, [done](const ApiResponse &r) {
        QList<MercuryAccount> out;
        if (r.ok()) {
            for (const QJsonValue &v : r.json.object().value(QStringLiteral("accounts")).toArray()) {
                const QJsonObject o = v.toObject();
                MercuryAccount a;
                a.id = text(o, "id");
                const QString nickname = text(o, "nickname");
                a.name = nickname.isEmpty() ? text(o, "name") : nickname;
                a.kind = text(o, "kind");
                a.status = text(o, "status");
                a.lastFour = text(o, "accountNumber").right(4);
                jsonMoneyToMinor(o.value(QStringLiteral("availableBalance")), 2, &a.available);
                jsonMoneyToMinor(o.value(QStringLiteral("currentBalance")), 2, &a.current);
                if (!a.id.isEmpty()) {
                    out << a;
                }
            }
        }
        done(r, out);
    });
}

void MercuryClient::transactions(const QString &accountId, const QDate &start, int offset, int limit, PageDone done)
{
    const QList<QPair<QString, QString>> query{
        {QStringLiteral("start"), start.toString(Qt::ISODate)},
        {QStringLiteral("limit"), QString::number(limit)},
        {QStringLiteral("offset"), QString::number(offset)},
        {QStringLiteral("order"), QStringLiteral("asc")},
    };
    get(QStringLiteral("/account/%1/transactions").arg(QString::fromLatin1(QUrl::toPercentEncoding(accountId))), query,
        [done](const ApiResponse &response) {
            ApiResponse r = response;
            MercuryPage page;
            if (r.ok()) {
                const QJsonObject root = r.json.object();
                page.total = root.value(QStringLiteral("total")).toInt();
                for (const QJsonValue &v : root.value(QStringLiteral("transactions")).toArray()) {
                    const QJsonObject o = v.toObject();
                    MercuryTransaction t;
                    t.id = text(o, "id");
                    t.status = text(o, "status");
                    t.kind = text(o, "kind");
                    t.createdAt = timeOf(o.value(QStringLiteral("createdAt")));
                    t.postedAt = timeOf(o.value(QStringLiteral("postedAt")));
                    t.counterpartyName = text(o, "counterpartyName");
                    t.bankDescription = text(o, "bankDescription");
                    t.note = text(o, "note");
                    t.externalMemo = text(o, "externalMemo");
                    t.checkNumber = text(o, "checkNumber");
                    if (t.id.isEmpty() || !jsonMoneyToMinor(o.value(QStringLiteral("amount")), 2, &t.amount)) {
                        r.kind = ApiResponse::Kind::BadJson;
                        r.error = QStringLiteral("Mercury sent a transaction without an id or an exact amount.");
                        break;
                    }
                    page.transactions << t;
                }
            }
            done(r, page);
        });
}

} // namespace QindaOffice::Books::Connect
