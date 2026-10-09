// SPDX-License-Identifier: GPL-3.0-or-later
// GraphFakeServer's mailbox side (M81, D-090): shaped after the Graph v1.0
// reference for mailboxSettings, outlookCategory, messageRule and message
// (internetMessageId, categories, flag). Collections page through
// @odata.nextLink like the calendar side.
#include "graph_fake_server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

namespace {
FakeHttpServer::Reply json(int status, const QJsonObject &object)
{
    return {status, {{"Content-Type", "application/json"}}, QJsonDocument(object).toJson(QJsonDocument::Compact)};
}

FakeHttpServer::Reply notFound()
{
    return json(404, {{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("ErrorItemNotFound")},
                                                            {QStringLiteral("message"), QStringLiteral("Not found")}}}});
}
} // namespace

FakeHttpServer::Reply GraphFakeServer::mailbox(const FakeHttpServer::Request &request, const QString &path)
{
    const QByteArray &method = request.method;
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    if (path == QLatin1String("/me/mailboxSettings/automaticRepliesSetting") && method == "GET")
        return json(200, automaticReplies);
    if (path == QLatin1String("/me/mailboxSettings") && method == "PATCH") {
        const QJsonObject setting = body.value(QLatin1String("automaticRepliesSetting")).toObject();
        if (setting.value(QLatin1String("status")).toString() == QLatin1String("scheduled")
            && !setting.contains(QLatin1String("scheduledStartDateTime")))
            return json(400, {{QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QStringLiteral("scheduled needs dates")}}}});
        for (auto it = setting.constBegin(); it != setting.constEnd(); ++it) automaticReplies.insert(it.key(), it.value());
        return json(200, {{QStringLiteral("automaticRepliesSetting"), automaticReplies}});
    }
    if (path == QLatin1String("/me/outlook/masterCategories")) {
        if (method == "GET") return page(request, masterCategories);
        if (method == "POST") {
            const QString name = body.value(QLatin1String("displayName")).toString();
            for (const QJsonObject &category : std::as_const(masterCategories))
                if (category.value(QLatin1String("displayName")).toString().compare(name, Qt::CaseInsensitive) == 0)
                    return json(409, {{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("ErrorDuplicate")}}}});
            QJsonObject category{{QStringLiteral("id"), QStringLiteral("cat-%1").arg(masterCategories.size() + 1)},
                                 {QStringLiteral("displayName"), name}, {QStringLiteral("color"), body.value(QLatin1String("color"))}};
            masterCategories.append(category);
            return json(201, category);
        }
    }
    if (path == QLatin1String("/me/mailFolders/inbox/messageRules") && method == "GET") return page(request, messageRules);
    if (path == QLatin1String("/me/messages") && method == "GET") {
        const QString filter = request.query.queryItemValue(QStringLiteral("$filter"), QUrl::FullyDecoded);
        static const QRegularExpression byId(QStringLiteral("^internetMessageId eq '((?:[^']|'')*)'$"));
        static const QRegularExpression since(QStringLiteral("^receivedDateTime ge (\\S+)$"));
        QList<QJsonObject> rows;
        const QRegularExpressionMatch id = byId.match(filter), from = since.match(filter);
        if (!id.hasMatch() && !from.hasMatch()) return json(400, {});
        const QDateTime earliest = QDateTime::fromString(from.captured(1), Qt::ISODate);
        for (auto it = messages.cbegin(); it != messages.cend(); ++it) {
            QJsonObject row = it.value();
            row.insert(QStringLiteral("id"), it.key());
            if (id.hasMatch() && row.value(QLatin1String("internetMessageId")).toString() != id.captured(1).replace(QStringLiteral("''"), QStringLiteral("'")))
                continue;
            if (from.hasMatch()
                && QDateTime::fromString(row.value(QLatin1String("receivedDateTime")).toString(), Qt::ISODate) < earliest)
                continue;
            rows.append(row);
        }
        return page(request, rows);
    }
    static const QString prefix = QStringLiteral("/me/messages/");
    if (path.startsWith(prefix) && method == "PATCH") {
        const QString id = path.mid(prefix.size());
        if (!messages.contains(id)) return notFound();
        QJsonObject &message = messages[id];
        for (const char *key : {"categories", "flag"}) {
            if (body.contains(QLatin1String(key))) message.insert(QLatin1String(key), body.value(QLatin1String(key)));
        }
        QJsonObject out = message;
        out.insert(QStringLiteral("id"), id);
        return json(200, out);
    }
    return notFound();
}
