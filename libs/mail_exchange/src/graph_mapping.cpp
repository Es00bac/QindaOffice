// SPDX-License-Identifier: GPL-3.0-or-later
#include "graph_mapping.h"

#include "recurrence_pattern.h"
#include "windows_zones.h"

#include <qindatk/recurrence_service.h>

#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <algorithm>
#include <climits>

namespace QindaOffice::Mail::Graph {
namespace {
QString str(const QJsonObject &object, const char *key) { return object.value(QLatin1String(key)).toString(); }
QString field(const QVariantMap &record, const char *key) { return record.value(QString::fromLatin1(key)).toString(); }

// Graph DateTimeOffset text ("2026-03-14T16:00:00Z", often with seven
// fractional digits) as an instant.
QDateTime offsetInstant(QString text)
{
    static const QRegularExpression fraction(QStringLiteral("\\.\\d+"));
    text.remove(fraction);
    return QDateTime::fromString(text.trimmed(), Qt::ISODate);
}

// Text of an optional member: a null QString when absent.
QString optional(const QJsonObject &object, const char *key, const char *inner = nullptr)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) return {};
    const QString text = inner ? value.toObject().value(QLatin1String(inner)).toString() : value.toString();
    return text.isNull() ? QStringLiteral("") : text;
}

QString zoneOf(const QJsonObject &event, const MapContext &context)
{
    const QString candidates[] = {str(event, "originalStartTimeZone"),
        event.value(QLatin1String("recurrence")).toObject().value(QLatin1String("range")).toObject()
            .value(QLatin1String("recurrenceTimeZone")).toString()};
    for (const QString &candidate : candidates) {
        const QString iana = Zones::toIana(candidate, context.preferredZones);
        if (!iana.isEmpty() && QTimeZone(iana.toUtf8()).isValid()) return iana;
    }
    return QTimeZone(context.defaultZone.toUtf8()).isValid() ? context.defaultZone : QStringLiteral("UTC");
}

QDateTime wallOf(const QJsonObject &dateTime, const QString &zone, bool allDay)
{
    const QDateTime wall = toWall(instantOf(dateTime), zone);
    const QString raw = str(dateTime, "dateTime");
    if (allDay && wall.isValid() && wall.time() != QTime(0, 0) && raw.mid(11, 8) == QLatin1String("00:00:00"))
        return QDate::fromString(raw.left(10), Qt::ISODate).startOfDay(QTimeZone(zone.toUtf8()));
    return wall;
}

RecurrencePattern patternOf(const QJsonObject &recurrence)
{
    const QJsonObject pattern = recurrence.value(QLatin1String("pattern")).toObject();
    const QJsonObject range = recurrence.value(QLatin1String("range")).toObject();
    RecurrencePattern p;
    p.type = str(pattern, "type");
    p.interval = qMax(1, pattern.value(QLatin1String("interval")).toInt(1));
    p.month = pattern.value(QLatin1String("month")).toInt();
    p.dayOfMonth = pattern.value(QLatin1String("dayOfMonth")).toInt();
    for (const QJsonValue &day : pattern.value(QLatin1String("daysOfWeek")).toArray()) p.days += dayNumbers(day.toString());
    p.firstDayOfWeek = str(pattern, "firstDayOfWeek").toLower();
    p.index = str(pattern, "index").toLower();
    p.range = str(range, "type").isEmpty() ? QStringLiteral("noEnd") : str(range, "type");
    p.startDate = QDate::fromString(str(range, "startDate"), Qt::ISODate);
    p.endDate = QDate::fromString(str(range, "endDate"), Qt::ISODate);
    p.count = range.value(QLatin1String("numberOfOccurrences")).toInt();
    return p;
}

QJsonObject dateTimeJson(const QDateTime &wall, const QString &windows)
{
    return {{QStringLiteral("dateTime"), wall.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"))},
            {QStringLiteral("timeZone"), windows}};
}

QJsonObject recurrenceJson(const RecurrencePattern &p, const QString &windows)
{
    QJsonObject pattern{{QStringLiteral("type"), p.type}, {QStringLiteral("interval"), p.interval}};
    if (p.type == QLatin1String("weekly")) {
        QJsonArray days;
        for (int day : p.days) days.append(dayName(day));
        pattern.insert(QStringLiteral("daysOfWeek"), days);
        pattern.insert(QStringLiteral("firstDayOfWeek"), QStringLiteral("monday"));
    }
    if (p.dayOfMonth > 0) pattern.insert(QStringLiteral("dayOfMonth"), p.dayOfMonth);
    if (p.month > 0) pattern.insert(QStringLiteral("month"), p.month);
    QJsonObject range{{QStringLiteral("type"), p.range}, {QStringLiteral("startDate"), p.startDate.toString(Qt::ISODate)},
                      {QStringLiteral("recurrenceTimeZone"), windows}};
    if (p.range == QLatin1String("endDate")) range.insert(QStringLiteral("endDate"), p.endDate.toString(Qt::ISODate));
    if (p.range == QLatin1String("numbered")) range.insert(QStringLiteral("numberOfOccurrences"), p.count);
    return {{QStringLiteral("pattern"), pattern}, {QStringLiteral("range"), range}};
}

} // namespace

QString originalKey(const QJsonObject &instance, const QString &zone, bool allDay)
{
    const QDateTime original = offsetInstant(str(instance, "originalStart"));
    return original.isValid() ? occurrenceKey(toWall(original, zone), allDay) : QString();
}

QString eventSelect()
{
    return QStringLiteral("id,type,subject,body,bodyPreview,location,start,end,isAllDay,originalStartTimeZone,"
                          "originalEndTimeZone,recurrence,iCalUId,organizer,attendees,isOnlineMeeting,onlineMeeting,"
                          "onlineMeetingUrl,isCancelled,seriesMasterId");
}

QString instanceSelect()
{
    return QStringLiteral("id,type,seriesMasterId,originalStart,start,end,subject,body,location,isCancelled");
}

QDateTime instantOf(const QJsonObject &dateTime)
{
    const QString text = str(dateTime, "dateTime");
    QString zone = Zones::toIana(str(dateTime, "timeZone"));
    if (zone.isEmpty()) zone = QStringLiteral("UTC");
    const QDate date = QDate::fromString(text.left(10), Qt::ISODate);
    const QTime time = QTime::fromString(text.mid(11, 8), QStringLiteral("HH:mm:ss"));
    if (!date.isValid() || !time.isValid()) return {};
    return QDateTime(date, time, QTimeZone(zone.toUtf8()), QDateTime::TransitionResolution::PreferBefore);
}

QStringList missingOccurrences(const QJsonObject &master, const QList<QJsonObject> &instances, const MapContext &context)
{
    const QString zone = zoneOf(master, context);
    const bool allDay = master.value(QLatin1String("isAllDay")).toBool();
    const QDateTime start = wallOf(master.value(QLatin1String("start")).toObject(), zone, allDay);
    const QDateTime end = wallOf(master.value(QLatin1String("end")).toObject(), zone, allDay);
    const MappedRecurrence mapped = patternToRule(patternOf(master.value(QLatin1String("recurrence")).toObject()), start.date());
    if (!start.isValid() || mapped.rule.isNull() || !mapped.raw.isEmpty()) return {};
    const QString startText = occurrenceKey(start, allDay);
    const qint64 span = QindaTK::RecurrenceService::spanSeconds(startText, occurrenceKey(end, allDay), zone);
    QindaTK::RecurrenceService recurrence;
    const QVariantList rows = recurrence.expand(QStringLiteral("x"), startText, int(qBound(qint64(0), span, qint64(INT_MAX))), zone,
        mapped.rule.toMap(), context.window.startUtc.toString(Qt::ISODate), context.window.endUtc.toString(Qt::ISODate), 1000);
    if (!recurrence.error().isEmpty() || rows.size() >= 1000) return {};   // cannot tell: claim nothing
    QSet<QString> present;
    for (const QJsonObject &instance : instances) present.insert(originalKey(instance, zone, allDay));
    QStringList missing;
    for (const QVariant &row : rows) {
        const QString key = row.toMap().value(QStringLiteral("localStart")).toString();
        if (!present.contains(key)) missing.append(key);
    }
    return missing;
}

CalendarSync::RemoteEvent mapEvent(const QJsonObject &event, const QList<QJsonObject> &instances, const MapContext &context)
{
    CalendarSync::RemoteEvent out;
    out.remoteId = str(event, "id");
    out.version = str(event, "@odata.etag");
    out.hasFields = true;
    const QString zone = zoneOf(event, context);
    const bool allDay = event.value(QLatin1String("isAllDay")).toBool();
    const QDateTime start = wallOf(event.value(QLatin1String("start")).toObject(), zone, allDay);
    QDateTime end = wallOf(event.value(QLatin1String("end")).toObject(), zone, allDay);
    if (!end.isValid() || end < start) end = start;
    QVariantMap &f = out.fields;
    f.insert(QStringLiteral("title"), str(event, "subject"));
    const QJsonObject body = event.value(QLatin1String("body")).toObject();
    f.insert(QStringLiteral("description"), normalizedText(str(body, "contentType").compare(QLatin1String("text"), Qt::CaseInsensitive) == 0
                                                               ? str(body, "content") : str(event, "bodyPreview")));
    f.insert(QStringLiteral("location"), event.value(QLatin1String("location")).toObject().value(QLatin1String("displayName")).toString());
    f.insert(QStringLiteral("zone"), zone);
    f.insert(QStringLiteral("allDay"), allDay);
    f.insert(QStringLiteral("startLocal"), localText(start, allDay));
    QDate endDate = end.date();
    if (allDay && endDate <= start.date()) endDate = start.date().addDays(1);
    f.insert(QStringLiteral("endLocal"), allDay ? endDate.toString(Qt::ISODate) : localText(end, false));
    f.insert(QStringLiteral("remoteItemType"), str(event, "type"));
    const QJsonValue recurrence = event.value(QLatin1String("recurrence"));
    MappedRecurrence mapped{QVariant::fromValue(nullptr), {}};
    if (recurrence.isObject()) mapped = patternToRule(patternOf(recurrence.toObject()), start.date());
    f.insert(QStringLiteral("recurrenceRule"), mapped.rule);
    f.insert(QStringLiteral("recurrenceRaw"), mapped.raw);

    QStringList exceptions = recurrence.isObject() ? missingOccurrences(event, instances, context) : QStringList();
    QVariantList overrides;
    QVariantMap occurrences;
    for (const QJsonObject &instance : instances) {
        const QString key = originalKey(instance, zone, allDay);
        if (key.isEmpty()) continue;
        occurrences.insert(key, str(instance, "id"));
        if (instance.value(QLatin1String("isCancelled")).toBool()) {
            exceptions.append(key);
            continue;
        }
        if (str(instance, "type") != QLatin1String("exception")) continue;
        const QJsonObject instanceBody = instance.value(QLatin1String("body")).toObject();
        const QString description = instance.contains(QLatin1String("body")) ? normalizedText(str(instanceBody, "content")) : QString();
        const QVariantMap entry = overrideEntry(f, key, instantOf(instance.value(QLatin1String("start")).toObject()),
            instantOf(instance.value(QLatin1String("end")).toObject()), optional(instance, "subject"),
            optional(instance, "location", "displayName"), description);
        if (!entry.isEmpty()) overrides.append(entry);
    }
    exceptions.sort();
    exceptions.removeDuplicates();
    std::sort(overrides.begin(), overrides.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("recurrenceId")).toString() < b.toMap().value(QStringLiteral("recurrenceId")).toString();
    });
    QVariantList exceptionList;
    for (const QString &key : std::as_const(exceptions)) exceptionList.append(key);
    f.insert(QStringLiteral("exceptions"), exceptionList);
    f.insert(QStringLiteral("overrides"), overrides);
    f.insert(QStringLiteral("remoteOccurrences"), occurrences);

    f.insert(QStringLiteral("remoteUid"), str(event, "iCalUId"));
    const QJsonObject organizer = event.value(QLatin1String("organizer")).toObject().value(QLatin1String("emailAddress")).toObject();
    f.insert(QStringLiteral("organizer"), organizer.isEmpty() ? QVariantMap()
             : QVariantMap{{QStringLiteral("name"), str(organizer, "name")}, {QStringLiteral("email"), str(organizer, "address")}});
    QVariantList attendees;
    for (const QJsonValue &value : event.value(QLatin1String("attendees")).toArray()) {
        const QJsonObject attendee = value.toObject();
        const QJsonObject address = attendee.value(QLatin1String("emailAddress")).toObject();
        attendees.append(QVariantMap{{QStringLiteral("name"), str(address, "name")}, {QStringLiteral("email"), str(address, "address")},
            {QStringLiteral("role"), str(attendee, "type")},
            {QStringLiteral("response"), attendee.value(QLatin1String("status")).toObject().value(QLatin1String("response")).toString()}});
    }
    f.insert(QStringLiteral("attendees"), attendees);
    f.insert(QStringLiteral("isOnlineMeeting"), event.value(QLatin1String("isOnlineMeeting")).toBool());
    const QString joinUrl = event.value(QLatin1String("onlineMeeting")).toObject().value(QLatin1String("joinUrl")).toString();
    f.insert(QStringLiteral("onlineMeetingUrl"), joinUrl.isEmpty() ? str(event, "onlineMeetingUrl") : joinUrl);
    f.insert(QStringLiteral("readOnly"), context.readOnly);
    return out;
}

bool eventBody(const QVariantMap &record, const SeriesChanges &changes, bool create, QJsonObject *body, QString *error)
{
    EventTimes times;
    if (!eventTimes(record, &times, error)) return false;
    const QVariantMap rule = record.value(QStringLiteral("recurrenceRule")).toMap();
    const QString raw = field(record, "recurrenceRaw");
    const QVariantMap baseline = record.value(QStringLiteral("remoteBaseline")).toMap();
    if (rule.isEmpty() && !raw.isEmpty()) {
        if (create || changes.recurrence) {
            *error = QStringLiteral("The repeat pattern %1 cannot be written to Microsoft 365 from here").arg(raw);
            return false;
        }
        if (changes.times && field(baseline, "startLocal").left(10) != field(record, "startLocal").left(10)) {
            *error = QStringLiteral("A series with the repeat pattern %1 can only change its time of day here").arg(raw);
            return false;
        }
    }
    const QString windows = Zones::toWindows(times.zone);
    const bool writeRule = !rule.isEmpty() && (create || changes.recurrence || changes.times);
    if ((create || changes.times || writeRule) && windows.isEmpty()) {
        *error = QStringLiteral("Microsoft 365 has no Windows time zone for %1").arg(times.zone);
        return false;
    }
    RecurrencePattern pattern;
    if (writeRule && !ruleToPattern(rule, times.start.date(), true, &pattern, error)) return false;
    if (create || changes.title) body->insert(QStringLiteral("subject"), field(record, "title"));
    if (create || changes.description)
        body->insert(QStringLiteral("body"), QJsonObject{{QStringLiteral("contentType"), QStringLiteral("text")},
                                                         {QStringLiteral("content"), field(record, "description")}});
    if (create || changes.location)
        body->insert(QStringLiteral("location"), QJsonObject{{QStringLiteral("displayName"), field(record, "location")}});
    if (create || changes.times) {
        body->insert(QStringLiteral("start"), dateTimeJson(times.start, windows));
        body->insert(QStringLiteral("end"), dateTimeJson(times.end, windows));
        body->insert(QStringLiteral("isAllDay"), times.allDay);
    }
    if (writeRule) body->insert(QStringLiteral("recurrence"), recurrenceJson(pattern, windows));
    else if (!create && changes.recurrence && rule.isEmpty() && raw.isEmpty())
        body->insert(QStringLiteral("recurrence"), QJsonValue::Null);
    return true;
}

bool occurrenceBody(const QVariantMap &master, const QString &key, const QVariantMap &entry, QJsonObject *body,
                    QString *error)
{
    EventTimes times;
    if (!occurrenceTimes(master, key, entry, &times, error)) return false;
    const QString windows = Zones::toWindows(times.zone);
    if (windows.isEmpty()) {
        *error = QStringLiteral("Microsoft 365 has no Windows time zone for %1").arg(times.zone);
        return false;
    }
    auto value = [&](const char *name) {
        const QString key = QString::fromLatin1(name);
        return entry.contains(key) ? entry.value(key).toString() : master.value(key).toString();
    };
    body->insert(QStringLiteral("start"), dateTimeJson(times.start, windows));
    body->insert(QStringLiteral("end"), dateTimeJson(times.end, windows));
    body->insert(QStringLiteral("subject"), value("title"));
    body->insert(QStringLiteral("location"), QJsonObject{{QStringLiteral("displayName"), value("location")}});
    body->insert(QStringLiteral("body"), QJsonObject{{QStringLiteral("contentType"), QStringLiteral("text")},
                                                     {QStringLiteral("content"), value("description")}});
    return true;
}

} // namespace QindaOffice::Mail::Graph
