// SPDX-License-Identifier: GPL-3.0-or-later
// GraphBackend reads: the window listing, single-event reads and paging.
// Writes and the calendar list are in graph_writes.cpp.
#include "graph_backend.h"

#include "graph_mapping.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace QindaOffice::Mail {
using CalendarSync::RemoteEvent;
using Failure = QindaTK::Protocol::Failure;

namespace {
QString utcParam(const QDateTime &time) { return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")); }
QString filterTime(const QDateTime &time) { return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss")); }

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    const int defaultPort = a.scheme() == QLatin1String("https") ? 443 : 80;
    return a.scheme() == b.scheme() && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
        && a.port(defaultPort) == b.port(defaultPort);
}
} // namespace

GraphBackend::GraphBackend(const GraphConfig &config) : Backend(config.timeoutMs, config.allowInsecureAuth), m_config(config)
{
    if (!config.secret.isEmpty()) transport().setAuthorization("Bearer " + config.secret.toUtf8(), {config.secret});
}

QUrl GraphBackend::url(const QString &path, const QUrlQuery &query) const
{
    QUrl out(m_config.baseUrl.toString(QUrl::FullyEncoded | QUrl::StripTrailingSlash) + path);
    if (!query.isEmpty()) out.setQuery(query);
    return out;
}

QString GraphBackend::calendarPath() const
{
    return m_config.calendarId.isEmpty() ? QStringLiteral("/me/calendar")
        : QStringLiteral("/me/calendars/") + QString::fromLatin1(QUrl::toPercentEncoding(m_config.calendarId));
}

QString GraphBackend::eventPath(const QString &id) const
{
    return QStringLiteral("/me/events/") + QString::fromLatin1(QUrl::toPercentEncoding(id));
}

bool GraphBackend::request(const QByteArray &method, const QUrl &target, const QJsonObject *body, const QByteArray &ifMatch,
                           HttpTransport::Response *response)
{
    HttpTransport::Headers headers{{"Accept", "application/json"},
        {"Prefer", "outlook.timezone=\"UTC\", outlook.body-content-type=\"text\", IdType=\"ImmutableId\""}};
    QByteArray payload;
    if (body) {
        headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
        payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
    }
    if (!ifMatch.isEmpty()) headers.append(qMakePair(QByteArray("If-Match"), ifMatch));
    return transport().send(method, target, headers, payload, {}, response);
}

bool GraphBackend::getPaged(const QUrl &first, QList<QJsonObject> *out)
{
    QUrl next = first;
    for (int page = 0; next.isValid() && !next.isEmpty(); ++page) {
        if (page >= m_config.maxPages) {
            transport().fail(Failure::Rejected, QStringLiteral("The calendar holds more than %1 pages of events in the sync "
                                                               "window; shorten syncPastDays or syncFutureDays").arg(m_config.maxPages));
            return false;
        }
        if (!sameOrigin(next, m_config.baseUrl)) {
            transport().fail(Failure::Rejected, QStringLiteral("Refusing to follow @odata.nextLink to another host (%1)").arg(next.host()));
            return false;
        }
        HttpTransport::Response response;
        if (!request("GET", next, nullptr, {}, &response)) return false;
        const QJsonDocument document = QJsonDocument::fromJson(response.body);
        if (response.status != 200 || !document.isObject()) {
            transport().fail(Failure::Rejected, QStringLiteral("Microsoft Graph returned HTTP %1 for %2: %3").arg(response.status)
                .arg(next.path(), document.object().value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString()));
            return false;
        }
        for (const QJsonValue &value : document.object().value(QLatin1String("value")).toArray()) out->append(value.toObject());
        next = QUrl(document.object().value(QLatin1String("@odata.nextLink")).toString());
    }
    return true;
}

bool GraphBackend::calendarReadOnly()
{
    if (m_readOnly >= 0) return true;
    HttpTransport::Response response;
    if (!request("GET", url(calendarPath(), QUrlQuery{{QStringLiteral("$select"), QStringLiteral("id,canEdit")}}), nullptr, {}, &response))
        return false;
    const QJsonObject calendar = QJsonDocument::fromJson(response.body).object();
    if (response.status != 200) {
        transport().fail(Failure::Rejected, QStringLiteral("Microsoft Graph could not open the calendar (HTTP %1)").arg(response.status));
        return false;
    }
    m_readOnly = calendar.value(QLatin1String("canEdit")).toBool(true) ? 0 : 1;
    return true;
}

bool GraphBackend::instances(const QString &masterId, const QDateTime &from, const QDateTime &to, QList<QJsonObject> *out)
{
    const QUrlQuery query{{QStringLiteral("startDateTime"), utcParam(from)}, {QStringLiteral("endDateTime"), utcParam(to)},
                          {QStringLiteral("$select"), Graph::instanceSelect()},
                          {QStringLiteral("$top"), QString::number(m_config.pageSize)}};
    return getPaged(url(eventPath(masterId) + QStringLiteral("/instances"), query), out);
}

bool GraphBackend::mapMaster(const QJsonObject &master, QList<QJsonObject> rows, RemoteEvent *out)
{
    const QString id = master.value(QLatin1String("id")).toString();
    const Graph::MapContext context{m_config.defaultZone, preferredZones(id, m_config.defaultZone), m_readOnly == 1,
                                    m_config.window};
    if (!Graph::missingOccurrences(master, rows, context).isEmpty()) {
        // A window occurrence without an instance was deleted, or moved out
        // of the window as an exception: look a year to either side.
        QList<QJsonObject> wide;
        if (!instances(id, m_config.window.startUtc.addDays(-366), m_config.window.endUtc.addDays(366), &wide)) return false;
        QSet<QString> known;
        for (const QJsonObject &row : std::as_const(rows)) known.insert(row.value(QLatin1String("id")).toString());
        for (const QJsonObject &row : std::as_const(wide))
            if (!known.contains(row.value(QLatin1String("id")).toString())) rows.append(row);
    }
    *out = Graph::mapEvent(master, rows, context);
    out->remoteId = id;
    return true;
}

bool GraphBackend::list(QMap<QString, RemoteEvent> *out)
{
    if (!calendarReadOnly()) return false;
    const SyncWindow &window = m_config.window;
    const QString filter = QStringLiteral("type eq 'seriesMaster' or (start/dateTime lt '%1' and end/dateTime gt '%2')")
                               .arg(filterTime(window.endUtc), filterTime(window.startUtc));
    QList<QJsonObject> events, view;
    if (!getPaged(url(calendarPath() + QStringLiteral("/events"),
                      QUrlQuery{{QStringLiteral("$select"), Graph::eventSelect()}, {QStringLiteral("$filter"), filter},
                                {QStringLiteral("$top"), QString::number(m_config.pageSize)}}), &events))
        return false;
    if (!getPaged(url(calendarPath() + QStringLiteral("/calendarView"),
                      QUrlQuery{{QStringLiteral("startDateTime"), utcParam(window.startUtc)},
                                {QStringLiteral("endDateTime"), utcParam(window.endUtc)},
                                {QStringLiteral("$select"), Graph::instanceSelect()},
                                {QStringLiteral("$top"), QString::number(m_config.pageSize)}}), &view))
        return false;
    QHash<QString, QList<QJsonObject>> byMaster;
    QStringList singles;
    for (const QJsonObject &row : std::as_const(view)) {
        const QString master = row.value(QLatin1String("seriesMasterId")).toString();
        if (!master.isEmpty()) byMaster[master].append(row);
        else singles.append(row.value(QLatin1String("id")).toString());
    }
    QSet<QString> listed;
    for (const QJsonObject &event : std::as_const(events)) {
        const QString id = event.value(QLatin1String("id")).toString();
        const QString type = event.value(QLatin1String("type")).toString();
        listed.insert(id);
        if (type == QLatin1String("seriesMaster")) {
            RemoteEvent mapped;
            if (!mapMaster(event, byMaster.value(id), &mapped)) return false;
            out->insert(id, mapped);
        } else if (type == QLatin1String("singleInstance")) {
            const Graph::MapContext context{m_config.defaultZone, preferredZones(id, m_config.defaultZone), m_readOnly == 1,
                                            m_config.window};
            out->insert(id, Graph::mapEvent(event, {}, context));
        }
    }
    // Anything the window shows that the event listing did not return (a
    // server that ignores part of the filter) is read one by one.
    QStringList extra;
    for (auto it = byMaster.cbegin(); it != byMaster.cend(); ++it)
        if (!listed.contains(it.key())) extra.append(it.key());
    for (const QString &id : std::as_const(singles))
        if (!listed.contains(id)) extra.append(id);
    return extra.isEmpty() || fetch(extra, out);
}

bool GraphBackend::fetch(const QStringList &ids, QMap<QString, RemoteEvent> *out)
{
    if (!calendarReadOnly()) return false;
    for (const QString &id : ids) {
        HttpTransport::Response response;
        if (!request("GET", url(eventPath(id), QUrlQuery{{QStringLiteral("$select"), Graph::eventSelect()}}), nullptr, {}, &response))
            return false;
        if (response.status == 404 || response.status == 410) continue;         // deleted
        const QJsonObject event = QJsonDocument::fromJson(response.body).object();
        if (response.status != 200 || event.isEmpty()) {
            transport().fail(Failure::Rejected, QStringLiteral("Microsoft Graph could not read an event (HTTP %1)").arg(response.status));
            return false;
        }
        RemoteEvent mapped;
        if (event.value(QLatin1String("type")).toString() == QLatin1String("seriesMaster")) {
            QList<QJsonObject> rows;
            if (!instances(id, m_config.window.startUtc, m_config.window.endUtc, &rows) || !mapMaster(event, rows, &mapped))
                return false;
        } else {
            const Graph::MapContext context{m_config.defaultZone, preferredZones(id, m_config.defaultZone), m_readOnly == 1,
                                            m_config.window};
            mapped = Graph::mapEvent(event, {}, context);
        }
        mapped.remoteId = id;
        out->insert(id, mapped);
    }
    return true;
}

} // namespace QindaOffice::Mail
