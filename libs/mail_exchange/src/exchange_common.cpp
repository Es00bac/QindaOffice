// SPDX-License-Identifier: GPL-3.0-or-later
#include "exchange_common.h"

#include <qindatk/ical_codec.h>
#include <qindatk/recurrence_service.h>

#include <QCryptographicHash>
#include <QJsonValue>
#include <QLocale>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QTimeZone>

namespace QindaOffice::Mail {
namespace {
QString text(const QVariantMap &map, const char *key) { return map.value(QString::fromLatin1(key)).toString(); }

bool sameOverride(const QVariantMap &a, const QVariantMap &b)
{
    for (const char *key : {"startLocal", "endLocal"})
        if (canonicalKey(text(a, key)) != canonicalKey(text(b, key))) return false;
    for (const char *key : {"title", "description", "location"}) {
        const QString field = QString::fromLatin1(key);
        if (a.contains(field) != b.contains(field) || a.value(field).toString() != b.value(field).toString()) return false;
    }
    return true;
}

QMap<QString, QVariantMap> overridesByKey(const QVariantMap &record)
{
    QMap<QString, QVariantMap> out;
    for (const QVariant &value : record.value(QStringLiteral("overrides")).toList()) {
        const QVariantMap entry = value.toMap();
        out.insert(canonicalKey(entry.value(QStringLiteral("recurrenceId")).toString()), entry);
    }
    return out;
}

QSet<QString> exceptionKeys(const QVariantMap &record)
{
    QSet<QString> out;
    for (const QVariant &value : record.value(QStringLiteral("exceptions")).toList()) {
        // Exceptions may be occurrence ids ("<event>/<local>") or bare local text.
        const QString raw = value.toString();
        out.insert(canonicalKey(raw.section(QLatin1Char('/'), -1)));
    }
    return out;
}
} // namespace

const QStringList &digestFields()
{
    static const QStringList fields{QStringLiteral("title"), QStringLiteral("startLocal"), QStringLiteral("endLocal"),
        QStringLiteral("zone"), QStringLiteral("allDay"), QStringLiteral("recurrenceRule"),
        QStringLiteral("recurrenceRaw"), QStringLiteral("exceptions"), QStringLiteral("description"),
        QStringLiteral("location"), QStringLiteral("overrides")};
    return fields;
}

const QStringList &remoteExtraFields()
{
    static const QStringList fields{QStringLiteral("remoteUid"), QStringLiteral("remoteItemType"),
        QStringLiteral("remoteOccurrences"), QStringLiteral("readOnly"), QStringLiteral("organizer"),
        QStringLiteral("attendees"), QStringLiteral("isOnlineMeeting"), QStringLiteral("onlineMeetingUrl"),
        // M81 (D-090): the server's categories, read on every pull; QindaMail
        // writes a change through ExchangeMailbox::setItemState at once.
        QStringLiteral("categories")};
    return fields;
}

QString eventDigest(const QVariantMap &record)
{
    return QindaTK::ICal::digest(QStringLiteral("VEVENT"), record);
}

QVariantMap digestSubset(const QVariantMap &record)
{
    QVariantMap out;
    for (const QString &field : digestFields()) out.insert(field, record.value(field));
    return out;
}

QString localText(const QDateTime &wall, bool allDay)
{
    if (allDay) return wall.date().toString(Qt::ISODate);
    const QTime time = wall.time();
    return wall.date().toString(Qt::ISODate) + QLatin1Char(' ')
        + time.toString(time.second() ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
}

QString occurrenceKey(const QDateTime &wall, bool allDay)
{
    return allDay ? wall.date().toString(Qt::ISODate) : wall.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
}

QDateTime wallTime(const QString &text, const QString &zone)
{
    QDate date;
    QTime time;
    bool dateOnly = false;
    const QTimeZone tz(zone.toUtf8());
    if (!tz.isValid() || !QindaTK::RecurrenceService::parseLocal(text, &date, &time, &dateOnly)) return {};
    if (dateOnly) return date.startOfDay(tz);
    return QDateTime(date, time, tz, QDateTime::TransitionResolution::PreferBefore);
}

QDateTime toWall(const QDateTime &instant, const QString &zone)
{
    const QTimeZone tz(zone.toUtf8());
    return tz.isValid() && instant.isValid() ? instant.toTimeZone(tz) : QDateTime();
}

QString canonicalKey(const QString &text)
{
    return text.trimmed().isEmpty() ? QString() : QindaTK::RecurrenceService::canonicalLocal(text.trimmed());
}

bool overlapsWindow(const QVariantMap &record, const SyncWindow &window)
{
    const QString zone = text(record, "zone").isEmpty() ? QStringLiteral("UTC") : text(record, "zone");
    const QDateTime start = wallTime(text(record, "startLocal"), zone);
    const QDateTime end = wallTime(text(record, "endLocal"), zone);
    if (!start.isValid()) return true;                // unreadable: let the server decide
    return start < window.endUtc && (end.isValid() ? end : start) >= window.startUtc;
}

bool isRecurring(const QVariantMap &record)
{
    return !record.value(QStringLiteral("recurrenceRule")).toMap().isEmpty() || !text(record, "recurrenceRaw").isEmpty()
        || text(record, "remoteItemType").contains(QLatin1String("aster"), Qt::CaseInsensitive);
}

QString localIdFor(const QString &prefix, const QString &accountId, const QString &remoteId)
{
    const QByteArray hash = QCryptographicHash::hash(remoteId.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
    return prefix + QLatin1Char(':') + accountId + QLatin1Char(':') + QString::fromLatin1(hash);
}

bool eventTimes(const QVariantMap &record, EventTimes *out, QString *error)
{
    out->zone = text(record, "zone").isEmpty() ? QStringLiteral("UTC") : text(record, "zone");
    out->allDay = record.value(QStringLiteral("allDay")).toBool() || text(record, "startLocal").trimmed().size() == 10;
    out->start = wallTime(text(record, "startLocal"), out->zone);
    out->end = wallTime(text(record, "endLocal"), out->zone);
    if (!out->start.isValid()) {
        *error = QStringLiteral("The event has no valid start in zone %1").arg(out->zone);
        return false;
    }
    if (!out->end.isValid() || out->end < out->start)
        out->end = out->allDay ? out->start.date().addDays(1).startOfDay(out->start.timeZone()) : out->start;
    return true;
}

bool occurrenceTimes(const QVariantMap &master, const QString &key, const QVariantMap &entry,
                     EventTimes *out, QString *error)
{
    EventTimes series;
    if (!eventTimes(master, &series, error)) return false;
    *out = series;
    const QDateTime original = wallTime(key, series.zone);
    if (!original.isValid()) {
        *error = QStringLiteral("Invalid occurrence %1").arg(key);
        return false;
    }
    const QString startText = text(entry, "startLocal"), endText = text(entry, "endLocal");
    out->start = startText.isEmpty() ? original : wallTime(startText, series.zone);
    if (series.allDay) {
        const qint64 days = qMax<qint64>(1, series.start.date().daysTo(series.end.date()));
        out->end = endText.isEmpty() ? out->start.date().addDays(days).startOfDay(out->start.timeZone())
                                     : wallTime(endText, series.zone);
    } else {
        out->end = endText.isEmpty() ? out->start.addSecs(series.start.secsTo(series.end)) : wallTime(endText, series.zone);
    }
    if (!out->start.isValid() || !out->end.isValid()) {
        *error = QStringLiteral("Invalid override times for occurrence %1").arg(key);
        return false;
    }
    return true;
}

QVariantMap overrideEntry(const QVariantMap &master, const QString &key, const QDateTime &start,
                          const QDateTime &end, const QString &title, const QString &location,
                          const QString &description)
{
    EventTimes original;
    QString ignored;
    if (!occurrenceTimes(master, key, {}, &original, &ignored) || !start.isValid() || !end.isValid()) return {};
    QVariantMap entry{{QStringLiteral("recurrenceId"), key}};
    const bool moved = start != original.start || end != original.end;
    if (moved) {
        entry.insert(QStringLiteral("startLocal"), localText(toWall(start, original.zone), original.allDay));
        entry.insert(QStringLiteral("endLocal"), localText(toWall(end, original.zone), original.allDay));
    }
    const QList<QPair<const char *, QString>> texts{{"title", title}, {"location", location}, {"description", description}};
    for (const auto &[field, value] : texts)
        if (!value.isNull() && value != text(master, field)) entry.insert(QString::fromLatin1(field), value);
    return entry.size() > 1 ? entry : QVariantMap();
}

QString normalizedText(const QString &value)
{
    QString out = value;
    out.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    out.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    while (!out.isEmpty() && out.back().isSpace()) out.chop(1);
    return out;
}

QVariantMap rejection(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("state"), QStringLiteral("rejected")},
            {QStringLiteral("error"), error}, {QStringLiteral("retryable"), false}};
}

int parseRetryAfter(const QByteArray &value, const QDateTime &nowUtc)
{
    const QByteArray trimmed = value.trimmed();
    if (trimmed.isEmpty()) return -1;
    bool numeric = false;
    const int seconds = trimmed.toInt(&numeric);
    if (numeric) return seconds < 0 ? -1 : seconds;
    // RFC 9110 IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT".
    QDateTime when = QLocale::c().toDateTime(QString::fromLatin1(trimmed), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
    if (!when.isValid()) return -1;
    when.setTimeZone(QTimeZone::UTC);
    return int(qMax<qint64>(0, nowUtc.secsTo(when)));
}

SeriesChanges seriesChanges(const QVariantMap &baseline, const QVariantMap &record)
{
    SeriesChanges changes;
    if (baseline.isEmpty()) {
        changes.title = changes.description = changes.location = changes.times = true;
        changes.recurrence = isRecurring(record);
        return changes;
    }
    changes.title = text(baseline, "title") != text(record, "title");
    changes.description = text(baseline, "description") != text(record, "description");
    changes.location = text(baseline, "location") != text(record, "location");
    changes.times = canonicalKey(text(baseline, "startLocal")) != canonicalKey(text(record, "startLocal"))
        || canonicalKey(text(baseline, "endLocal")) != canonicalKey(text(record, "endLocal"))
        || text(baseline, "zone") != text(record, "zone")
        || baseline.value(QStringLiteral("allDay")).toBool() != record.value(QStringLiteral("allDay")).toBool();
    // JSON comparison: a rule read back from the store file has double numbers.
    changes.recurrence = QJsonValue::fromVariant(baseline.value(QStringLiteral("recurrenceRule")).toMap())
            != QJsonValue::fromVariant(record.value(QStringLiteral("recurrenceRule")).toMap())
        || text(baseline, "recurrenceRaw") != text(record, "recurrenceRaw");
    return changes;
}

OccurrenceChanges occurrenceChanges(const QVariantMap &server, const QVariantMap &record)
{
    OccurrenceChanges changes;
    const QSet<QString> remoteExcluded = exceptionKeys(server), localExcluded = exceptionKeys(record);
    for (const QString &key : localExcluded)
        if (!remoteExcluded.contains(key)) changes.excluded.append(key);
    for (const QString &key : remoteExcluded)
        if (!localExcluded.contains(key)) changes.reinstated.append(key);
    const QMap<QString, QVariantMap> remote = overridesByKey(server), local = overridesByKey(record);
    for (auto it = local.cbegin(); it != local.cend(); ++it)
        if (!remote.contains(it.key()) || !sameOverride(remote.value(it.key()), it.value())) changes.overrides.append(it.value());
    for (auto it = remote.cbegin(); it != remote.cend(); ++it)
        if (!local.contains(it.key()) && !localExcluded.contains(it.key()))
            changes.restored.append(QVariantMap{{QStringLiteral("recurrenceId"), it.key()}});
    changes.excluded.sort();
    changes.reinstated.sort();
    return changes;
}

bool occurrencesNeedWrite(const QVariantMap &baseline, const QVariantMap &record, const SeriesChanges &changes)
{
    auto same = [](const QVariant &a, const QVariant &b) { return QJsonValue::fromVariant(a) == QJsonValue::fromVariant(b); };
    const bool has = !record.value(QStringLiteral("exceptions")).toList().isEmpty()
        || !record.value(QStringLiteral("overrides")).toList().isEmpty();
    if (!isRecurring(record)) return false;
    if (baseline.isEmpty()) return has;
    const bool differs = !same(baseline.value(QStringLiteral("exceptions")), record.value(QStringLiteral("exceptions")))
        || !same(baseline.value(QStringLiteral("overrides")), record.value(QStringLiteral("overrides")));
    return differs || ((changes.times || changes.recurrence) && has);
}

} // namespace QindaOffice::Mail
