// SPDX-License-Identifier: GPL-3.0-or-later
#include "graph_fake_server.h"

#include <QJsonDocument>
#include <QTimeZone>
#include <QUrl>

namespace {
QDateTime instantOf(const QJsonObject &time)
{
    const QString zoneName = time.value(QLatin1String("timeZone")).toString();
    QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(zoneName.toUtf8());
    if (iana.isEmpty()) iana = zoneName.toUtf8();
    const QString text = time.value(QLatin1String("dateTime")).toString().left(19);
    return QDateTime(QDate::fromString(text.left(10), Qt::ISODate), QTime::fromString(text.mid(11), QStringLiteral("HH:mm:ss")),
                     QTimeZone(iana)).toUTC();
}

bool overlaps(const QJsonObject &row, const QDateTime &from, const QDateTime &to)
{
    return instantOf(row.value(QLatin1String("start")).toObject()) < to && instantOf(row.value(QLatin1String("end")).toObject()) > from;
}

FakeHttpServer::Reply json(int status, const QJsonObject &object)
{
    return {status, {{"Content-Type", "application/json"}}, QJsonDocument(object).toJson(QJsonDocument::Compact)};
}

QDateTime queryTime(const FakeHttpServer::Request &request, const char *key)
{
    return QDateTime::fromString(request.query.queryItemValue(QString::fromLatin1(key), QUrl::FullyDecoded), Qt::ISODate);
}
} // namespace

GraphFakeServer::GraphFakeServer() : http([this](const FakeHttpServer::Request &request) { return handle(request); }) {}

QJsonObject GraphFakeServer::utcTime(const QDateTime &instant)
{
    return {{QStringLiteral("dateTime"), instant.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss")) + QStringLiteral(".0000000")},
            {QStringLiteral("timeZone"), QStringLiteral("UTC")}};
}

QJsonObject GraphFakeServer::full(const QString &id) const
{
    QJsonObject out = events.value(id).json;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("@odata.etag"), etag(id));
    return out;
}

QString GraphFakeServer::masterOf(const QString &id, int *index) const
{
    for (auto it = events.cbegin(); it != events.cend(); ++it)
        for (int i = 0; i < it->instances.size(); ++i)
            if (it->instances.at(i).value(QLatin1String("id")).toString() == id) { *index = i; return it.key(); }
    return {};
}

FakeHttpServer::Reply GraphFakeServer::page(const FakeHttpServer::Request &request, const QList<QJsonObject> &rows)
{
    const int skip = request.query.queryItemValue(QStringLiteral("$skip")).toInt();
    QJsonArray value;
    for (int i = skip; i < qMin<int>(skip + pageSize, rows.size()); ++i) value.append(rows.at(i));
    QJsonObject out{{QStringLiteral("value"), value}};
    if (skip + pageSize < rows.size()) {
        QUrl next(http.origin() + request.path);
        QUrlQuery query = request.query;
        query.removeAllQueryItems(QStringLiteral("$skip"));
        query.addQueryItem(QStringLiteral("$skip"), QString::number(skip + pageSize));
        next.setQuery(query);
        out.insert(QStringLiteral("@odata.nextLink"), skip == 0 && !foreignNextLink.isEmpty() ? foreignNextLink : next.toString());
    }
    return json(200, out);
}

FakeHttpServer::Reply GraphFakeServer::handle(const FakeHttpServer::Request &request)
{
    if (throttle)
        return {429, {{"Retry-After", "7"}}, R"({"error":{"code":"TooManyRequests","message":"Slow down"}})"};
    if (!request.path.startsWith(QLatin1String("/v1.0/"))) return {404, {}, {}};
    const QString path = request.path.mid(5);
    const QByteArray &method = request.method;
    if (method == "GET" && path == QLatin1String("/me/calendar"))
        return json(200, {{QStringLiteral("id"), QStringLiteral("cal-default")}, {QStringLiteral("canEdit"), canEdit}});
    if (method == "GET" && path == QLatin1String("/me/calendars")) {
        return page(request, {QJsonObject{{QStringLiteral("id"), QStringLiteral("cal-default")}, {QStringLiteral("name"), QStringLiteral("Calendar")},
                                          {QStringLiteral("color"), QStringLiteral("auto")}, {QStringLiteral("hexColor"), QStringLiteral("#1f6feb")},
                                          {QStringLiteral("canEdit"), true}, {QStringLiteral("isDefaultCalendar"), true}},
                              QJsonObject{{QStringLiteral("id"), QStringLiteral("cal-holidays")}, {QStringLiteral("name"), QStringLiteral("Holidays")},
                                          {QStringLiteral("color"), QStringLiteral("lightGreen")}, {QStringLiteral("hexColor"), QString()},
                                          {QStringLiteral("canEdit"), false}, {QStringLiteral("isDefaultCalendar"), false}}});
    }
    if (method == "GET" && path == QLatin1String("/me/calendar/events")) {
        QList<QJsonObject> rows;
        for (auto it = events.cbegin(); it != events.cend(); ++it) rows.append(full(it.key()));
        return page(request, rows);
    }
    if (method == "GET" && path == QLatin1String("/me/calendar/calendarView")) {
        const QDateTime from = queryTime(request, "startDateTime"), to = queryTime(request, "endDateTime");
        QList<QJsonObject> rows;
        for (auto it = events.cbegin(); it != events.cend(); ++it) {
            if (it->json.value(QLatin1String("type")).toString() == QLatin1String("singleInstance") && overlaps(it->json, from, to))
                rows.append(full(it.key()));
            for (const QJsonObject &instance : it->instances)
                if (overlaps(instance, from, to)) rows.append(instance);
        }
        return page(request, rows);
    }
    if (method == "POST" && path == QLatin1String("/me/calendar/events")) {
        QJsonObject body = QJsonDocument::fromJson(request.body).object();
        posted.append(body);
        const QString id = QStringLiteral("ev-new-%1").arg(m_nextId++);
        Event event;
        event.json = body;
        event.json.remove(QStringLiteral("transactionId"));
        event.json.insert(QStringLiteral("originalStartTimeZone"), body.value(QLatin1String("start")).toObject().value(QLatin1String("timeZone")));
        event.json.insert(QStringLiteral("start"), utcTime(instantOf(body.value(QLatin1String("start")).toObject())));
        event.json.insert(QStringLiteral("end"), utcTime(instantOf(body.value(QLatin1String("end")).toObject())));
        event.json.insert(QStringLiteral("type"), body.contains(QLatin1String("recurrence")) ? QStringLiteral("seriesMaster") : QStringLiteral("singleInstance"));
        event.json.insert(QStringLiteral("iCalUId"), QStringLiteral("ical-") + id);
        events.insert(id, event);
        return json(201, full(id));
    }
    static const QString eventsPrefix = QStringLiteral("/me/events/");
    if (!path.startsWith(eventsPrefix)) return {404, {}, {}};
    QString id = path.mid(eventsPrefix.size());
    if (method == "GET" && id.endsWith(QLatin1String("/instances"))) {
        id.chop(10);
        const QDateTime from = queryTime(request, "startDateTime"), to = queryTime(request, "endDateTime");
        QList<QJsonObject> rows;
        for (const QJsonObject &instance : events.value(id).instances)
            if (overlaps(instance, from, to)) rows.append(instance);
        return page(request, rows);
    }
    if (method == "PATCH") return patch(id, request);
    if (method == "DELETE") return remove(id, request);
    int index = -1;
    if (events.contains(id)) return json(200, full(id));
    const QString master = masterOf(id, &index);
    if (!master.isEmpty()) return json(200, events.value(master).instances.at(index));
    return json(404, {{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("ErrorItemNotFound")}}}});
}

FakeHttpServer::Reply GraphFakeServer::patch(const QString &id, const FakeHttpServer::Request &request)
{
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const QByteArray ifMatch = request.headers.value("if-match");
    auto merge = [&](QJsonObject *target) {
        for (auto it = body.constBegin(); it != body.constEnd(); ++it) {
            if (it.key() == QLatin1String("start") || it.key() == QLatin1String("end")) {
                target->insert(it.key(), utcTime(instantOf(it.value().toObject())));
                if (it.key() == QLatin1String("start")) target->insert(QStringLiteral("originalStartTimeZone"), it.value().toObject().value(QLatin1String("timeZone")));
            } else if (it.value().isNull()) {
                target->remove(it.key());
                if (it.key() == QLatin1String("recurrence")) target->insert(QStringLiteral("type"), QStringLiteral("singleInstance"));
            } else {
                target->insert(it.key(), it.value());
            }
        }
    };
    if (events.contains(id)) {
        if (!ifMatch.isEmpty() && QString::fromUtf8(ifMatch) != etag(id)) return json(412, {});
        merge(&events[id].json);
        bump(id);
        return json(200, full(id));
    }
    int index = -1;
    const QString master = masterOf(id, &index);
    if (master.isEmpty()) return json(404, {});
    QJsonObject &instance = events[master].instances[index];
    merge(&instance);
    instance.remove(QStringLiteral("originalStartTimeZone"));
    instance.insert(QStringLiteral("type"), QStringLiteral("exception"));
    bump(master);
    return json(200, instance);
}

FakeHttpServer::Reply GraphFakeServer::remove(const QString &id, const FakeHttpServer::Request &request)
{
    const QByteArray ifMatch = request.headers.value("if-match");
    if (events.contains(id)) {
        if (!ifMatch.isEmpty() && QString::fromUtf8(ifMatch) != etag(id)) return json(412, {});
        events.remove(id);
        return {204, {}, {}};
    }
    int index = -1;
    const QString master = masterOf(id, &index);
    if (master.isEmpty()) return json(404, {});
    events[master].instances.removeAt(index);
    bump(master);
    return {204, {}, {}};
}
