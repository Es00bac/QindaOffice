// SPDX-License-Identifier: GPL-3.0-or-later
// GraphBackend writes (POST, PATCH/DELETE with If-Match, occurrences) and the
// calendar list. Reads are in graph_backend.cpp.
#include "graph_backend.h"

#include "graph_mapping.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

namespace QindaOffice::Mail {
using CalendarSync::LocalItem;
using CalendarSync::RemoteEvent;
using CalendarSync::WriteOutcome;
using Failure = QindaTK::Protocol::Failure;

namespace {
WriteOutcome outcome(WriteOutcome::Kind kind, const QString &text = {}, const QString &id = {}, const QString &version = {})
{
    WriteOutcome out;
    out.kind = kind;
    out.error = text;
    out.remoteId = id;
    out.version = version;
    return out;
}

QJsonObject json(const HttpTransport::Response &response) { return QJsonDocument::fromJson(response.body).object(); }
bool success(const HttpTransport::Response &response) { return response.status >= 200 && response.status <= 299; }
} // namespace

WriteOutcome GraphBackend::failure(const HttpTransport::Response &response, const QString &what)
{
    if (response.status == 412) return outcome(WriteOutcome::Conflict);
    if (response.status == 404 || response.status == 410) return outcome(WriteOutcome::Gone);
    const QString message = json(response).value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
    return outcome(WriteOutcome::Failed, QStringLiteral("Microsoft Graph refused to %1 (HTTP %2)%3").arg(what).arg(response.status)
                                             .arg(message.isEmpty() ? QString() : QStringLiteral(": ") + message));
}

WriteOutcome GraphBackend::create(const LocalItem &item)
{
    QJsonObject body;
    QString error;
    if (!Graph::eventBody(item.record, {}, true, &body, &error)) return outcome(WriteOutcome::Failed, error);
    // Graph ignores a POST whose transactionId it has already seen, so a
    // retried create never duplicates the event; a re-create after a remote
    // deletion has another remote id and so another transaction.
    const QByteArray transaction = QCryptographicHash::hash((item.id + QLatin1Char('|') + item.remoteId).toUtf8(),
                                                            QCryptographicHash::Sha256).toHex().left(32);
    body.insert(QStringLiteral("transactionId"), QString::fromLatin1(transaction));
    HttpTransport::Response response;
    if (!request("POST", url(calendarPath() + QStringLiteral("/events")), &body, {}, &response))
        return outcome(WriteOutcome::Failed, transport().error());
    const QJsonObject created = json(response);
    const QString id = created.value(QLatin1String("id")).toString();
    if (!success(response) || id.isEmpty()) {
        const WriteOutcome failed = failure(response, QStringLiteral("create the event"));
        return failed.kind == WriteOutcome::Failed ? failed : outcome(WriteOutcome::Failed, QStringLiteral("The calendar was not found"));
    }
    preferZone(id, item.record.value(QStringLiteral("zone")).toString());
    const QString etag = created.value(QLatin1String("@odata.etag")).toString();
    if (item.record.value(QStringLiteral("exceptions")).toList().isEmpty() && item.record.value(QStringLiteral("overrides")).toList().isEmpty())
        return outcome(WriteOutcome::Done, {}, id, etag);
    const WriteOutcome applied = applyOccurrences(item, id, etag, false);
    // The series exists either way: report its identity so it is never created twice.
    if (applied.kind != WriteOutcome::Done) errors().append(QStringLiteral("%1: %2").arg(item.id, applied.error));
    return outcome(WriteOutcome::Done, {}, id, applied.kind == WriteOutcome::Done ? applied.version : etag);
}

WriteOutcome GraphBackend::update(const LocalItem &item, const QString &version)
{
    const QVariantMap baseline = item.baseline();
    const SeriesChanges changes = seriesChanges(baseline, item.record);
    QJsonObject body;
    QString error;
    if (!Graph::eventBody(item.record, changes, false, &body, &error)) return outcome(WriteOutcome::Conflict, error);
    QString current = version;
    if (!body.isEmpty()) {
        HttpTransport::Response response;
        if (!request("PATCH", url(eventPath(item.remoteId)), &body, version.toUtf8(), &response))
            return outcome(WriteOutcome::Failed, transport().error());
        if (!success(response)) return failure(response, QStringLiteral("update the event"));
        current = json(response).value(QLatin1String("@odata.etag")).toString();
    }
    if (!occurrencesNeedWrite(baseline, item.record, changes)) return outcome(WriteOutcome::Done, {}, item.remoteId, current);
    return applyOccurrences(item, item.remoteId, current, body.isEmpty());
}

WriteOutcome GraphBackend::applyOccurrences(const LocalItem &item, const QString &remoteId, const QString &version,
                                            bool conditional)
{
    QMap<QString, RemoteEvent> current;
    if (!fetch({remoteId}, &current)) return outcome(WriteOutcome::Failed, transport().error());
    if (!current.contains(remoteId)) return outcome(WriteOutcome::Gone);
    const RemoteEvent server = current.value(remoteId);
    if (conditional && !version.isEmpty() && server.version != version) return outcome(WriteOutcome::Conflict);
    const OccurrenceChanges changes = occurrenceChanges(server.fields, item.record);
    if (!changes.reinstated.isEmpty())
        return outcome(WriteOutcome::Conflict, QStringLiteral("Microsoft 365 cannot bring back a deleted occurrence (%1)")
                                                   .arg(changes.reinstated.join(QStringLiteral(", "))));
    const QString zone = server.fields.value(QStringLiteral("zone")).toString();
    const bool allDay = server.fields.value(QStringLiteral("allDay")).toBool();
    const QVariantMap known = server.fields.value(QStringLiteral("remoteOccurrences")).toMap();
    const QVariantList updates = changes.overrides + changes.restored;
    QStringList keys = changes.excluded;
    for (const QVariant &entry : updates) keys.append(canonicalKey(entry.toMap().value(QStringLiteral("recurrenceId")).toString()));
    QMap<QString, QString> ids;
    for (const QString &key : std::as_const(keys)) {
        QString id = known.value(key).toString();
        if (id.isEmpty()) {
            // Outside the window: ask for the instances around the original start.
            const QDateTime original = wallTime(key, zone);
            QList<QJsonObject> rows;
            if (!original.isValid() || !instances(remoteId, original.addDays(-1), original.addDays(2), &rows))
                return outcome(WriteOutcome::Failed, transport().error());
            for (const QJsonObject &row : std::as_const(rows))
                if (Graph::originalKey(row, zone, allDay) == key) id = row.value(QLatin1String("id")).toString();
        }
        if (id.isEmpty())
            return outcome(WriteOutcome::Conflict, QStringLiteral("The occurrence %1 no longer exists on Microsoft 365").arg(key));
        ids.insert(key, id);
    }
    QList<QPair<QString, QJsonObject>> patches;
    for (const QVariant &value : updates) {
        const QVariantMap entry = value.toMap();
        const QString key = canonicalKey(entry.value(QStringLiteral("recurrenceId")).toString());
        QJsonObject body;
        QString error;
        if (!Graph::occurrenceBody(item.record, key, entry.size() > 1 ? entry : QVariantMap(), &body, &error))
            return outcome(WriteOutcome::Conflict, error);
        patches.append({ids.value(key), body});
    }
    for (const QString &key : changes.excluded) {
        HttpTransport::Response response;
        if (!request("DELETE", url(eventPath(ids.value(key))), nullptr, {}, &response))
            return outcome(WriteOutcome::Failed, transport().error());
        if (!success(response) && response.status != 404) return failure(response, QStringLiteral("delete an occurrence"));
    }
    for (const auto &[id, body] : std::as_const(patches)) {
        HttpTransport::Response response;
        if (!request("PATCH", url(eventPath(id)), &body, {}, &response)) return outcome(WriteOutcome::Failed, transport().error());
        if (!success(response)) {
            const WriteOutcome failed = failure(response, QStringLiteral("change an occurrence"));
            return failed.kind == WriteOutcome::Gone ? outcome(WriteOutcome::Conflict) : failed;
        }
    }
    return outcome(WriteOutcome::Done, {}, remoteId);    // the engine re-reads the new etag
}

WriteOutcome GraphBackend::remove(const LocalItem &item, const QString &version)
{
    HttpTransport::Response response;
    if (!request("DELETE", url(eventPath(item.remoteId)), nullptr, version.toUtf8(), &response))
        return outcome(WriteOutcome::Failed, transport().error());
    if (success(response)) return outcome(WriteOutcome::Done, {}, item.remoteId);
    return failure(response, QStringLiteral("delete the event"));
}

bool GraphBackend::listCalendars(QVariantList *out)
{
    QList<QJsonObject> rows;
    if (!getPaged(url(QStringLiteral("/me/calendars"),
                      QUrlQuery{{QStringLiteral("$select"), QStringLiteral("id,name,color,hexColor,canEdit,isDefaultCalendar")},
                                {QStringLiteral("$top"), QString::number(m_config.pageSize)}}), &rows))
        return false;
    for (const QJsonObject &row : std::as_const(rows)) {
        const QString hex = row.value(QLatin1String("hexColor")).toString();
        out->append(QVariantMap{{QStringLiteral("id"), row.value(QLatin1String("id")).toString()},
            {QStringLiteral("name"), row.value(QLatin1String("name")).toString()},
            {QStringLiteral("color"), hex.isEmpty() ? row.value(QLatin1String("color")).toString() : hex},
            {QStringLiteral("readOnly"), !row.value(QLatin1String("canEdit")).toBool(true)},
            {QStringLiteral("isDefault"), row.value(QLatin1String("isDefaultCalendar")).toBool()}});
    }
    return true;
}

} // namespace QindaOffice::Mail
