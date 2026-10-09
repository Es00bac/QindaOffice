// SPDX-License-Identifier: GPL-3.0-or-later
// MailboxJobs over Microsoft Graph v1.0 (mailbox_jobs.h): mailboxSettings
// automaticRepliesSetting, outlook/masterCategories, the inbox's
// messageRules, and a message's categories and flag (looked up by
// internetMessageId) or an event's categories.
#include "mailbox_jobs.h"

#include "windows_zones.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QUrlQuery>

namespace QindaOffice::Mail::MailboxJobs {

using namespace Qt::StringLiterals;
using Failure = QindaTK::Protocol::Failure;

namespace {

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    const int defaultPort = a.scheme() == "https"_L1 ? 443 : 80;
    return a.scheme() == b.scheme() && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
        && a.port(defaultPort) == b.port(defaultPort);
}

struct GraphCall {
    explicit GraphCall(const Config &c) : config(c), transport(c.timeoutMs, c.allowInsecureAuth)
    {
        transport.setAuthorization("Bearer " + c.secret.toUtf8(), {c.secret});
    }

    QUrl url(const QString &path, const QUrlQuery &query = {}) const
    {
        QUrl out(config.baseUrl.toString(QUrl::FullyEncoded | QUrl::StripTrailingSlash) + path);
        if (!query.isEmpty()) out.setQuery(query);
        return out;
    }

    // One request; false for a failed dialogue or a non-2xx answer (which fails the transport).
    bool send(const QByteArray &method, const QUrl &target, const QJsonObject *body, QJsonObject *answer,
              const QList<int> &alsoFine = {})
    {
        HttpTransport::Headers headers{{"Accept", "application/json"}, {"Prefer", "outlook.timezone=\"UTC\""}};
        QByteArray payload;
        if (body != nullptr) {
            headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
            payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
        }
        HttpTransport::Response response;
        if (!transport.send(method, target, headers, payload, {}, &response)) return false;
        const QJsonObject json = QJsonDocument::fromJson(response.body).object();
        if ((response.status < 200 || response.status > 299) && !alsoFine.contains(response.status)) {
            transport.fail(Failure::Rejected, u"Microsoft Graph returned HTTP %1 for %2: %3"_s.arg(response.status)
                                                  .arg(target.path(), json.value(u"error"_s).toObject().value(u"message"_s).toString()));
            return false;
        }
        if (answer != nullptr) *answer = json;
        lastStatus = response.status;
        return true;
    }

    // A collection and its @odata.nextLink pages (same origin, bounded).
    bool getPaged(const QUrl &first, QList<QJsonObject> *out)
    {
        QUrl next = first;
        for (int page = 0; next.isValid() && !next.isEmpty(); ++page) {
            if (page >= config.maxPages) break;     // older mail beyond the bound keeps its local tags
            if (!sameOrigin(next, config.baseUrl)) {
                transport.fail(Failure::Rejected, u"Refusing to follow @odata.nextLink to another host (%1)"_s.arg(next.host()));
                return false;
            }
            QJsonObject json;
            if (!send("GET", next, nullptr, &json)) return false;
            for (const QJsonValue &value : json.value(u"value"_s).toArray()) out->append(value.toObject());
            next = QUrl(json.value(u"@odata.nextLink"_s).toString());
        }
        return true;
    }

    const Config &config;
    HttpTransport transport;
    int lastStatus = 0;
};

QString isoUtc(const QJsonObject &dateTimeZone)
{
    const QString text = dateTimeZone.value(u"dateTime"_s).toString().left(19);
    if (text.isEmpty()) return {};
    const QString iana = Zones::toIana(dateTimeZone.value(u"timeZone"_s).toString());
    const QTimeZone zone = iana.isEmpty() ? QTimeZone(QTimeZone::UTC) : QTimeZone(iana.toUtf8());
    const QDateTime local(QDate::fromString(text.left(10), Qt::ISODate), QTime::fromString(text.mid(11), u"HH:mm:ss"_s),
                          zone.isValid() ? zone : QTimeZone(QTimeZone::UTC));
    return local.isValid() ? local.toUTC().toString(Qt::ISODate) : QString();
}

QJsonObject utcValue(const QString &iso)
{
    const QDateTime time = QDateTime::fromString(iso, Qt::ISODate).toUTC();
    return {{u"dateTime"_s, time.toString(u"yyyy-MM-dd'T'HH:mm:ss"_s)}, {u"timeZone"_s, u"UTC"_s}};
}

QVariantMap repliesFromJson(const QJsonObject &setting)
{
    const QString status = setting.value(u"status"_s).toString();
    const QString audience = setting.value(u"externalAudience"_s).toString();
    return {{u"enabled"_s, status == "alwaysEnabled"_L1 || status == "scheduled"_L1},
            {u"scheduled"_s, status == "scheduled"_L1},
            {u"start"_s, isoUtc(setting.value(u"scheduledStartDateTime"_s).toObject())},
            {u"end"_s, isoUtc(setting.value(u"scheduledEndDateTime"_s).toObject())},
            {u"internalMessage"_s, textFromHtml(setting.value(u"internalReplyMessage"_s).toString())},
            {u"externalMessage"_s, textFromHtml(setting.value(u"externalReplyMessage"_s).toString())},
            {u"externalAudience"_s, audience == "contactsOnly"_L1 ? u"contacts"_s : audience == "none"_L1 ? u"none"_s : u"all"_s}};
}

QJsonObject repliesToJson(const QVariantMap &settings)
{
    const bool enabled = settings.value(u"enabled"_s).toBool();
    const bool scheduled = settings.value(u"scheduled"_s).toBool();
    const QString audience = settings.value(u"externalAudience"_s).toString();
    QJsonObject setting{
        {u"status"_s, !enabled ? u"disabled"_s : scheduled ? u"scheduled"_s : u"alwaysEnabled"_s},
        {u"externalAudience"_s, audience == "contacts"_L1 ? u"contactsOnly"_s : audience == "none"_L1 ? u"none"_s : u"all"_s},
        {u"internalReplyMessage"_s, htmlFromText(settings.value(u"internalMessage"_s).toString())},
        {u"externalReplyMessage"_s, htmlFromText(settings.value(u"externalMessage"_s).toString())}};
    if (scheduled) {
        setting.insert(u"scheduledStartDateTime"_s, utcValue(settings.value(u"start"_s).toString()));
        setting.insert(u"scheduledEndDateTime"_s, utcValue(settings.value(u"end"_s).toString()));
    }
    return setting;
}

QVariantList categoriesFromJson(const QList<QJsonObject> &rows)
{
    QVariantList out;
    for (const QJsonObject &row : rows) {
        const QString color = row.value(u"color"_s).toString();
        int preset = -1;
        if (color.startsWith("preset"_L1)) preset = color.mid(6).toInt();
        out.append(QVariantMap{{u"name"_s, row.value(u"displayName"_s).toString()}, {u"preset"_s, preset},
                               {u"id"_s, row.value(u"id"_s).toString()}});
    }
    return out;
}

// Graph's rule predicates and actions, worded for the read-only list.
QString ruleSummary(const QJsonObject &rule)
{
    const auto words = [](const QJsonValue &value) {
        QStringList out;
        for (const QJsonValue &entry : value.toArray()) {
            const QJsonObject address = entry.toObject().value(u"emailAddress"_s).toObject();
            out.append(entry.isString() ? entry.toString() : address.value(u"address"_s).toString());
        }
        return out.join(u", "_s);
    };
    QStringList conditions;
    const QJsonObject when = rule.value(u"conditions"_s).toObject();
    const QList<QPair<const char *, const char *>> lists{{"fromAddresses", "from %1"}, {"sentToAddresses", "sent to %1"},
        {"subjectContains", "subject contains %1"}, {"bodyContains", "body contains %1"},
        {"bodyOrSubjectContains", "subject or body contains %1"}, {"headerContains", "header contains %1"},
        {"senderContains", "sender contains %1"}, {"categories", "assigned to %1"}};
    for (const auto &[key, text] : lists)
        if (when.contains(QLatin1String(key))) conditions.append(QString::fromLatin1(text).arg(words(when.value(QLatin1String(key)))));
    if (when.value(u"sentOnlyToMe"_s).toBool()) conditions.append(u"sent only to me"_s);
    if (when.value(u"hasAttachments"_s).toBool()) conditions.append(u"with an attachment"_s);
    if (when.contains(u"importance"_s)) conditions.append(u"importance %1"_s.arg(when.value(u"importance"_s).toString()));
    QStringList actions;
    const QJsonObject act = rule.value(u"actions"_s).toObject();
    if (act.contains(u"moveToFolder"_s)) actions.append(u"move to a folder"_s);
    if (act.contains(u"copyToFolder"_s)) actions.append(u"copy to a folder"_s);
    if (act.value(u"delete"_s).toBool()) actions.append(u"delete"_s);
    if (act.value(u"permanentDelete"_s).toBool()) actions.append(u"delete permanently"_s);
    if (act.value(u"markAsRead"_s).toBool()) actions.append(u"mark as read"_s);
    if (act.contains(u"markImportance"_s)) actions.append(u"mark %1 importance"_s.arg(act.value(u"markImportance"_s).toString()));
    if (act.contains(u"assignCategories"_s)) actions.append(u"assign %1"_s.arg(words(act.value(u"assignCategories"_s))));
    if (act.contains(u"forwardTo"_s)) actions.append(u"forward to %1"_s.arg(words(act.value(u"forwardTo"_s))));
    if (act.contains(u"redirectTo"_s)) actions.append(u"redirect to %1"_s.arg(words(act.value(u"redirectTo"_s))));
    if (act.value(u"stopProcessingRules"_s).toBool()) actions.append(u"stop processing more rules"_s);
    return (conditions.isEmpty() ? u"every message"_s : conditions.join(u" and "_s)) + u" → "_s
        + (actions.isEmpty() ? u"(no action QindaMail can show)"_s : actions.join(u", "_s));
}

QJsonObject flagToJson(const QVariantMap &flag, const QString &zone)
{
    const QString status = flag.value(u"status"_s).toString();
    QJsonObject out{{u"flagStatus"_s, status == "flagged"_L1 ? u"flagged"_s : status == "complete"_L1 ? u"complete"_s : u"notFlagged"_s}};
    QString windows = Zones::toWindows(zone);
    if (windows.isEmpty()) windows = u"UTC"_s;
    const auto day = [&windows](const QString &date) {
        return QJsonObject{{u"dateTime"_s, date + u"T00:00:00"_s}, {u"timeZone"_s, windows}};
    };
    const QString start = flag.value(u"start"_s).toString(), due = flag.value(u"due"_s).toString();
    if (status == "flagged"_L1 && !due.isEmpty()) {
        out.insert(u"startDateTime"_s, day(start.isEmpty() ? due : start));    // Graph wants both
        out.insert(u"dueDateTime"_s, day(due));
    }
    if (status == "complete"_L1 && !flag.value(u"completed"_s).toString().isEmpty())
        out.insert(u"completedDateTime"_s, utcValue(flag.value(u"completed"_s).toString()));
    return out;
}

QVariantMap flagFromJson(const QJsonObject &flag)
{
    const QString status = flag.value(u"flagStatus"_s).toString();
    return {{u"status"_s, status == "flagged"_L1 ? u"flagged"_s : status == "complete"_L1 ? u"complete"_s : QString()},
            {u"start"_s, flag.value(u"startDateTime"_s).toObject().value(u"dateTime"_s).toString().left(10)},
            {u"due"_s, flag.value(u"dueDateTime"_s).toObject().value(u"dateTime"_s).toString().left(10)},
            {u"completed"_s, isoUtc(flag.value(u"completedDateTime"_s).toObject())}};
}

QString odataQuoted(const QString &text)
{
    QString out = text;
    return u'\'' + out.replace(u'\'', "''"_L1) + u'\'';
}

} // namespace

QVariantMap graph(Op op, const Config &config, const QVariantMap &input)
{
    GraphCall call(config);
    QVariantMap out;
    QString problem;
    QJsonObject json;
    switch (op) {
    case Op::ReadAutoReplies:
        if (call.send("GET", call.url(u"/me/mailboxSettings/automaticRepliesSetting"_s), nullptr, &json))
            out.insert(u"settings"_s, repliesFromJson(json));
        break;
    case Op::WriteAutoReplies: {
        const QJsonObject body{{u"automaticRepliesSetting"_s, repliesToJson(input)}};
        if (call.send("PATCH", call.url(u"/me/mailboxSettings"_s), &body, &json))
            out.insert(u"settings"_s, repliesFromJson(json.value(u"automaticRepliesSetting"_s).toObject().isEmpty()
                                                          ? repliesToJson(input) : json.value(u"automaticRepliesSetting"_s).toObject()));
        break;
    }
    case Op::AddCategory: {
        const int preset = input.value(u"preset"_s).toInt();
        const QJsonObject body{{u"displayName"_s, input.value(u"name"_s).toString()},
                               {u"color"_s, preset >= 0 && preset <= 24 ? u"preset%1"_s.arg(preset) : u"none"_s}};
        // 409: the mailbox already has it — the list below shows it either way.
        if (!call.send("POST", call.url(u"/me/outlook/masterCategories"_s), &body, nullptr, {409})) break;
        [[fallthrough]];
    }
    case Op::ListCategories: {
        QList<QJsonObject> rows;
        if (call.getPaged(call.url(u"/me/outlook/masterCategories"_s), &rows)) out.insert(u"categories"_s, categoriesFromJson(rows));
        break;
    }
    case Op::ListRules: {
        QList<QJsonObject> rows;
        if (!call.getPaged(call.url(u"/me/mailFolders/inbox/messageRules"_s), &rows)) break;
        QVariantList rules;
        for (const QJsonObject &rule : std::as_const(rows))
            rules.append(QVariantMap{{u"name"_s, rule.value(u"displayName"_s).toString()},
                                     {u"enabled"_s, rule.value(u"isEnabled"_s).toBool(true)},
                                     {u"order"_s, rule.value(u"sequence"_s).toInt()},
                                     {u"summary"_s, ruleSummary(rule)}});
        out.insert(u"rules"_s, rules);
        break;
    }
    case Op::SetItemState: {
        QJsonObject body;
        if (input.contains(u"categories"_s))
            body.insert(u"categories"_s, QJsonArray::fromStringList(input.value(u"categories"_s).toStringList()));
        if (input.value(u"kind"_s).toString() == "event"_L1) {
            const QString id = input.value(u"remoteId"_s).toString();
            if (!body.isEmpty() && call.send("PATCH", call.url(u"/me/events/"_s + QString::fromLatin1(QUrl::toPercentEncoding(id))), &body, nullptr)) {
                out.insert(u"found"_s, true);
                out.insert(u"remoteId"_s, id);
            }
            break;
        }
        if (input.contains(u"flag"_s)) body.insert(u"flag"_s, flagToJson(input.value(u"flag"_s).toMap(), input.value(u"zone"_s).toString()));
        const QString messageId = u'<' + input.value(u"internetMessageId"_s).toString().trimmed() + u'>';
        const QUrlQuery query{{u"$filter"_s, u"internetMessageId eq "_s + odataQuoted(messageId)},
                              {u"$select"_s, u"id,categories,flag"_s}, {u"$top"_s, u"1"_s}};
        if (!call.send("GET", call.url(u"/me/messages"_s, query), nullptr, &json)) break;
        const QJsonArray found = json.value(u"value"_s).toArray();
        out.insert(u"found"_s, !found.isEmpty());
        if (found.isEmpty()) break;                       // not in this mailbox (yet): nothing to tag
        const QString id = found.first().toObject().value(u"id"_s).toString();
        out.insert(u"remoteId"_s, id);
        call.send("PATCH", call.url(u"/me/messages/"_s + QString::fromLatin1(QUrl::toPercentEncoding(id))), &body, nullptr);
        break;
    }
    case Op::PullItemStates: {
        const QDateTime since = QDateTime::currentDateTimeUtc().addDays(-input.value(u"days"_s, 30).toInt());
        const QUrlQuery query{{u"$select"_s, u"internetMessageId,categories,flag"_s},
                              {u"$filter"_s, u"receivedDateTime ge "_s + since.toString(u"yyyy-MM-dd'T'HH:mm:ss'Z'"_s)},
                              {u"$top"_s, u"100"_s}};
        QList<QJsonObject> rows;
        if (!call.getPaged(call.url(u"/me/messages"_s, query), &rows)) break;
        QVariantList items;
        for (const QJsonObject &row : std::as_const(rows)) {
            QString id = row.value(u"internetMessageId"_s).toString().trimmed();
            if (id.startsWith(u'<') && id.endsWith(u'>')) id = id.mid(1, id.size() - 2);
            if (id.isEmpty()) continue;
            items.append(QVariantMap{{u"internetMessageId"_s, id},
                                     {u"categories"_s, row.value(u"categories"_s).toVariant().toStringList()},
                                     {u"flag"_s, flagFromJson(row.value(u"flag"_s).toObject())}});
        }
        out.insert(u"items"_s, items);
        break;
    }
    }
    return finish(call.transport, out, problem);
}

} // namespace QindaOffice::Mail::MailboxJobs
