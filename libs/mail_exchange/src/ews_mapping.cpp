// SPDX-License-Identifier: GPL-3.0-or-later
#include "ews_mapping.h"

#include "ews_soap.h"
#include "recurrence_pattern.h"
#include "windows_zones.h"

#include <QTimeZone>
#include <algorithm>

namespace QindaOffice::Mail::Ews {
namespace {
const QStringList &months()
{
    static const QStringList names{QStringLiteral("January"), QStringLiteral("February"), QStringLiteral("March"),
        QStringLiteral("April"), QStringLiteral("May"), QStringLiteral("June"), QStringLiteral("July"),
        QStringLiteral("August"), QStringLiteral("September"), QStringLiteral("October"), QStringLiteral("November"),
        QStringLiteral("December")};
    return names;
}

QDateTime instant(const QString &text) { return QDateTime::fromString(text.trimmed(), Qt::ISODate); }
QString utcText(const QDateTime &time) { return time.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")); }
bool isTrue(const XmlNode &node, QStringView name) { return node.childText(name).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0; }
QString field(const QVariantMap &record, const char *key) { return record.value(QString::fromLatin1(key)).toString(); }

QString capitalized(QString name)
{
    if (!name.isEmpty()) name[0] = name.at(0).toUpper();
    return name;
}

// Text of an optional element: a null QString when the server left it out.
QString optionalText(const XmlNode *node, QStringView name, bool body = false)
{
    const XmlNode *child = node ? node->child(name) : nullptr;
    if (!child) return {};
    const QString value = body ? normalizedText(child->text) : child->text.trimmed();
    return value.isNull() ? QStringLiteral("") : value;
}

RecurrencePattern readPattern(const XmlNode &recurrence)
{
    static const QHash<QString, QString> types{{QStringLiteral("DailyRecurrence"), QStringLiteral("daily")},
        {QStringLiteral("WeeklyRecurrence"), QStringLiteral("weekly")},
        {QStringLiteral("AbsoluteMonthlyRecurrence"), QStringLiteral("absoluteMonthly")},
        {QStringLiteral("RelativeMonthlyRecurrence"), QStringLiteral("relativeMonthly")},
        {QStringLiteral("AbsoluteYearlyRecurrence"), QStringLiteral("absoluteYearly")},
        {QStringLiteral("RelativeYearlyRecurrence"), QStringLiteral("relativeYearly")}};
    RecurrencePattern p;
    for (const XmlNode &node : recurrence.children) {
        if (types.contains(node.name)) {
            p.type = types.value(node.name);
            p.interval = qMax(1, node.childText(u"Interval").toInt());
            for (const QString &token : node.childText(u"DaysOfWeek").split(QLatin1Char(' '), Qt::SkipEmptyParts))
                p.days += dayNumbers(token);
            p.dayOfMonth = node.childText(u"DayOfMonth").toInt();
            p.month = months().indexOf(node.childText(u"Month")) + 1;
            p.index = node.childText(u"DayOfWeekIndex").toLower();
            p.firstDayOfWeek = node.childText(u"FirstDayOfWeek").toLower();
        } else if (node.name == u"NoEndRecurrence" || node.name == u"EndDateRecurrence" || node.name == u"NumberedRecurrence") {
            p.range = node.name == u"NoEndRecurrence" ? QStringLiteral("noEnd")
                    : node.name == u"EndDateRecurrence" ? QStringLiteral("endDate") : QStringLiteral("numbered");
            // xs:date may carry an offset ("2026-03-21-07:00"): the date is the first ten characters.
            p.startDate = QDate::fromString(node.childText(u"StartDate").left(10), Qt::ISODate);
            p.endDate = QDate::fromString(node.childText(u"EndDate").left(10), Qt::ISODate);
            p.count = node.childText(u"NumberOfOccurrences").toInt();
        } else if (p.type.isEmpty()) {
            p.type = node.name;                      // regeneration patterns: kept as raw
        }
    }
    return p;
}

void writePattern(QXmlStreamWriter &w, const RecurrencePattern &p, const WriteOptions &options)
{
    startT(w, QStringLiteral("Recurrence"));
    if (p.type == QLatin1String("daily")) {
        startT(w, QStringLiteral("DailyRecurrence"));
        textT(w, QStringLiteral("Interval"), QString::number(p.interval));
    } else if (p.type == QLatin1String("weekly")) {
        startT(w, QStringLiteral("WeeklyRecurrence"));
        textT(w, QStringLiteral("Interval"), QString::number(p.interval));
        QStringList days;
        for (int day : p.days) days.append(capitalized(dayName(day)));
        textT(w, QStringLiteral("DaysOfWeek"), days.join(QLatin1Char(' ')));
        if (options.firstDayOfWeek) textT(w, QStringLiteral("FirstDayOfWeek"), QStringLiteral("Monday"));
    } else if (p.type == QLatin1String("absoluteMonthly")) {
        startT(w, QStringLiteral("AbsoluteMonthlyRecurrence"));
        textT(w, QStringLiteral("Interval"), QString::number(p.interval));
        textT(w, QStringLiteral("DayOfMonth"), QString::number(p.dayOfMonth));
    } else {
        startT(w, QStringLiteral("AbsoluteYearlyRecurrence"));
        textT(w, QStringLiteral("DayOfMonth"), QString::number(p.dayOfMonth));
        textT(w, QStringLiteral("Month"), months().value(p.month - 1));
    }
    w.writeEndElement();
    const QString range = p.range == QLatin1String("numbered") ? QStringLiteral("NumberedRecurrence")
        : p.range == QLatin1String("endDate") ? QStringLiteral("EndDateRecurrence") : QStringLiteral("NoEndRecurrence");
    startT(w, range);
    textT(w, QStringLiteral("StartDate"), p.startDate.toString(Qt::ISODate));
    if (p.range == QLatin1String("numbered")) textT(w, QStringLiteral("NumberOfOccurrences"), QString::number(p.count));
    else if (p.range == QLatin1String("endDate")) textT(w, QStringLiteral("EndDate"), p.endDate.toString(Qt::ISODate));
    w.writeEndElement();
    w.writeEndElement();
}

void writeBody(QXmlStreamWriter &w, const QString &text)
{
    startT(w, QStringLiteral("Body"));
    w.writeAttribute(QStringLiteral("BodyType"), QStringLiteral("Text"));
    w.writeCharacters(text);
    w.writeEndElement();
}

void writeZone(QXmlStreamWriter &w, const QString &element, const QString &windows)
{
    startT(w, element);
    w.writeAttribute(QStringLiteral("Id"), windows);
    w.writeEndElement();
}

QVariantMap person(const XmlNode *mailbox)
{
    if (!mailbox) return {};
    return {{QStringLiteral("name"), mailbox->childText(u"Name")}, {QStringLiteral("email"), mailbox->childText(u"EmailAddress")}};
}

void appendAttendees(const XmlNode &item, QStringView list, const QString &role, QVariantList *out)
{
    const XmlNode *node = item.child(list);
    if (!node) return;
    for (const XmlNode *attendee : node->childrenNamed(u"Attendee")) {
        QVariantMap entry = person(attendee->child(u"Mailbox"));
        entry.insert(QStringLiteral("role"), role);
        entry.insert(QStringLiteral("response"), attendee->childText(u"ResponseType"));
        out->append(entry);
    }
}

// Writes one SetItemField; `value` writes the element inside <t:CalendarItem>.
void setField(QXmlStreamWriter &w, const QString &uri, const std::function<void()> &value)
{
    startT(w, QStringLiteral("SetItemField"));
    fieldUri(w, uri);
    startT(w, QStringLiteral("CalendarItem"));
    value();
    w.writeEndElement();
    w.writeEndElement();
}
} // namespace

const QStringList &itemFieldUris()
{
    static const QStringList uris{QStringLiteral("item:Subject"), QStringLiteral("item:Body"),
        QStringLiteral("item:EffectiveRights"), QStringLiteral("calendar:UID"), QStringLiteral("calendar:Start"),
        QStringLiteral("calendar:End"), QStringLiteral("calendar:IsAllDayEvent"), QStringLiteral("calendar:Location"),
        QStringLiteral("calendar:CalendarItemType"), QStringLiteral("calendar:Organizer"),
        QStringLiteral("calendar:RequiredAttendees"), QStringLiteral("calendar:OptionalAttendees"),
        QStringLiteral("calendar:Resources"), QStringLiteral("calendar:TimeZone"), QStringLiteral("calendar:Recurrence"),
        QStringLiteral("calendar:ModifiedOccurrences"), QStringLiteral("calendar:DeletedOccurrences"),
        QStringLiteral("calendar:StartTimeZone"), QStringLiteral("calendar:IsOnlineMeeting"),
        QStringLiteral("calendar:StartTimeZoneId"), QStringLiteral("calendar:JoinOnlineMeetingUrl")};
    return uris;
}

const QStringList &occurrenceFieldUris()
{
    static const QStringList uris{QStringLiteral("item:Subject"), QStringLiteral("item:Body"),
        QStringLiteral("calendar:Start"), QStringLiteral("calendar:End"), QStringLiteral("calendar:Location"),
        QStringLiteral("calendar:IsCancelled"), QStringLiteral("calendar:OriginalStart")};
    return uris;
}

CalendarSync::RemoteEvent mapItem(const XmlNode &item, const QHash<QString, const XmlNode *> &exceptionItems,
                                  const MapContext &context)
{
    CalendarSync::RemoteEvent out;
    if (const XmlNode *id = item.child(u"ItemId")) {
        out.remoteId = id->attribute(QStringLiteral("Id"));
        out.version = id->attribute(QStringLiteral("ChangeKey"));
    }
    out.hasFields = true;
    QString zoneName = item.childText(u"StartTimeZoneId");
    if (zoneName.isEmpty())
        if (const XmlNode *zone = item.child(u"StartTimeZone")) zoneName = zone->attribute(QStringLiteral("Id"));
    QString zone = Zones::toIana(zoneName, context.preferredZones);
    if (zone.isEmpty() || !QTimeZone(zone.toUtf8()).isValid())
        zone = QTimeZone(context.defaultZone.toUtf8()).isValid() ? context.defaultZone : QStringLiteral("UTC");
    const bool allDay = isTrue(item, u"IsAllDayEvent");
    const QDateTime start = toWall(instant(item.childText(u"Start")), zone);
    QDateTime end = toWall(instant(item.childText(u"End")), zone);
    if (!end.isValid() || end < start) end = start;

    QVariantMap &f = out.fields;
    f.insert(QStringLiteral("title"), item.childText(u"Subject"));
    f.insert(QStringLiteral("description"), normalizedText(item.child(u"Body") ? item.child(u"Body")->text : QString()));
    f.insert(QStringLiteral("location"), item.childText(u"Location"));
    f.insert(QStringLiteral("zone"), zone);
    f.insert(QStringLiteral("allDay"), allDay);
    f.insert(QStringLiteral("startLocal"), localText(start, allDay));
    QDate endDate = end.date();
    if (allDay && (end.time() != QTime(0, 0) || endDate <= start.date())) endDate = qMax(endDate.addDays(1), start.date().addDays(1));
    f.insert(QStringLiteral("endLocal"), allDay ? endDate.toString(Qt::ISODate) : localText(end, false));
    const QString type = item.childText(u"CalendarItemType");
    f.insert(QStringLiteral("remoteItemType"), type == QLatin1String("RecurringMaster") ? QStringLiteral("seriesMaster")
                                               : QStringLiteral("singleInstance"));
    MappedRecurrence recurrence{QVariant::fromValue(nullptr), {}};
    if (const XmlNode *node = item.child(u"Recurrence")) recurrence = patternToRule(readPattern(*node), start.date());
    f.insert(QStringLiteral("recurrenceRule"), recurrence.rule);
    f.insert(QStringLiteral("recurrenceRaw"), recurrence.raw);

    QStringList exceptions;
    if (const XmlNode *deleted = item.child(u"DeletedOccurrences"))
        for (const XmlNode *occurrence : deleted->childrenNamed(u"DeletedOccurrence"))
            exceptions.append(occurrenceKey(toWall(instant(occurrence->childText(u"Start")), zone), allDay));
    QVariantList overrides;
    QVariantMap occurrences;
    if (const XmlNode *modified = item.child(u"ModifiedOccurrences")) {
        for (const XmlNode *occurrence : modified->childrenNamed(u"Occurrence")) {
            const XmlNode *idNode = occurrence->child(u"ItemId");
            const QString occurrenceId = idNode ? idNode->attribute(QStringLiteral("Id")) : QString();
            const QString key = occurrenceKey(toWall(instant(occurrence->childText(u"OriginalStart")), zone), allDay);
            occurrences.insert(key, occurrenceId);
            const XmlNode *exception = exceptionItems.value(occurrenceId);
            if (exception && isTrue(*exception, u"IsCancelled")) {
                exceptions.append(key);
                continue;
            }
            const QVariantMap entry = overrideEntry(f, key, instant(occurrence->childText(u"Start")),
                instant(occurrence->childText(u"End")), optionalText(exception, u"Subject"),
                optionalText(exception, u"Location"), optionalText(exception, u"Body", true));
            if (!entry.isEmpty()) overrides.append(entry);
        }
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

    f.insert(QStringLiteral("remoteUid"), item.childText(u"UID"));
    const XmlNode *organizer = item.child(u"Organizer");
    f.insert(QStringLiteral("organizer"), person(organizer ? organizer->child(u"Mailbox") : nullptr));
    QVariantList attendees;
    appendAttendees(item, u"RequiredAttendees", QStringLiteral("required"), &attendees);
    appendAttendees(item, u"OptionalAttendees", QStringLiteral("optional"), &attendees);
    appendAttendees(item, u"Resources", QStringLiteral("resource"), &attendees);
    f.insert(QStringLiteral("attendees"), attendees);
    f.insert(QStringLiteral("isOnlineMeeting"), isTrue(item, u"IsOnlineMeeting"));
    f.insert(QStringLiteral("onlineMeetingUrl"), item.childText(u"JoinOnlineMeetingUrl"));
    const XmlNode *rights = item.child(u"EffectiveRights");
    f.insert(QStringLiteral("readOnly"), rights && rights->childText(u"Modify").compare(QLatin1String("false"), Qt::CaseInsensitive) == 0);
    return out;
}

bool writeNewItem(QXmlStreamWriter &w, const QVariantMap &record, const WriteOptions &options, QString *error)
{
    EventTimes times;
    if (!eventTimes(record, &times, error)) return false;
    const QString windows = Zones::toWindows(times.zone);
    if (windows.isEmpty()) { *error = QStringLiteral("Exchange has no Windows time zone for %1").arg(times.zone); return false; }
    const QVariantMap rule = record.value(QStringLiteral("recurrenceRule")).toMap();
    RecurrencePattern pattern;
    if (!rule.isEmpty() && !ruleToPattern(rule, times.start.date(), false, &pattern, error)) return false;
    if (rule.isEmpty() && !field(record, "recurrenceRaw").isEmpty()) {
        *error = QStringLiteral("A series with the repeat pattern %1 cannot be created on Exchange").arg(field(record, "recurrenceRaw"));
        return false;
    }
    // AGENT-GUARD: element order is the CalendarItemType xs:sequence
    // (Item fields, then Start, End, IsAllDayEvent, Location, Recurrence,
    // StartTimeZone, EndTimeZone); Exchange rejects any other order.
    startT(w, QStringLiteral("CalendarItem"));
    textT(w, QStringLiteral("Subject"), field(record, "title"));
    writeBody(w, field(record, "description"));
    textT(w, QStringLiteral("Start"), utcText(times.start));
    textT(w, QStringLiteral("End"), utcText(times.end));
    textT(w, QStringLiteral("IsAllDayEvent"), times.allDay ? QStringLiteral("true") : QStringLiteral("false"));
    textT(w, QStringLiteral("Location"), field(record, "location"));
    if (!rule.isEmpty()) writePattern(w, pattern, options);
    writeZone(w, QStringLiteral("StartTimeZone"), windows);
    writeZone(w, QStringLiteral("EndTimeZone"), windows);
    w.writeEndElement();
    return true;
}

bool writeSeriesUpdates(QXmlStreamWriter &w, const QVariantMap &record, const SeriesChanges &changes,
                        bool recurringOnServer, const WriteOptions &options, QString *error)
{
    EventTimes times;
    if (!eventTimes(record, &times, error)) return false;
    const QVariantMap rule = record.value(QStringLiteral("recurrenceRule")).toMap();
    const QString raw = field(record, "recurrenceRaw");
    const QVariantMap baseline = record.value(QStringLiteral("remoteBaseline")).toMap();
    if (!raw.isEmpty() && rule.isEmpty()) {
        if (changes.recurrence) {
            *error = QStringLiteral("The server's repeat pattern (%1) cannot be edited here").arg(raw);
            return false;
        }
        if (changes.times && field(baseline, "startLocal").left(10) != field(record, "startLocal").left(10)) {
            *error = QStringLiteral("A series with the repeat pattern %1 can only change its time of day here").arg(raw);
            return false;
        }
    }
    RecurrencePattern pattern;
    const bool writeRule = !rule.isEmpty() && (changes.recurrence || changes.times);
    if (writeRule && !ruleToPattern(rule, times.start.date(), false, &pattern, error)) return false;
    const QString windows = Zones::toWindows(times.zone);
    if (changes.times && windows.isEmpty()) {
        *error = QStringLiteral("Exchange has no Windows time zone for %1").arg(times.zone);
        return false;
    }
    if (changes.title)
        setField(w, QStringLiteral("item:Subject"), [&] { textT(w, QStringLiteral("Subject"), field(record, "title")); });
    if (changes.description)
        setField(w, QStringLiteral("item:Body"), [&] { writeBody(w, field(record, "description")); });
    if (changes.location)
        setField(w, QStringLiteral("calendar:Location"), [&] { textT(w, QStringLiteral("Location"), field(record, "location")); });
    if (changes.times) {
        setField(w, QStringLiteral("calendar:StartTimeZone"), [&] { writeZone(w, QStringLiteral("StartTimeZone"), windows); });
        setField(w, QStringLiteral("calendar:EndTimeZone"), [&] { writeZone(w, QStringLiteral("EndTimeZone"), windows); });
        setField(w, QStringLiteral("calendar:IsAllDayEvent"), [&] {
            textT(w, QStringLiteral("IsAllDayEvent"), times.allDay ? QStringLiteral("true") : QStringLiteral("false"));
        });
        setField(w, QStringLiteral("calendar:Start"), [&] { textT(w, QStringLiteral("Start"), utcText(times.start)); });
        setField(w, QStringLiteral("calendar:End"), [&] { textT(w, QStringLiteral("End"), utcText(times.end)); });
    }
    if (writeRule) {
        setField(w, QStringLiteral("calendar:Recurrence"), [&] { writePattern(w, pattern, options); });
    } else if (changes.recurrence && rule.isEmpty() && raw.isEmpty() && recurringOnServer) {
        startT(w, QStringLiteral("DeleteItemField"));
        fieldUri(w, QStringLiteral("calendar:Recurrence"));
        w.writeEndElement();
    }
    return true;
}

bool writeOccurrenceUpdates(QXmlStreamWriter &w, const QVariantMap &master, const QString &key,
                            const QVariantMap &entry, QString *error)
{
    EventTimes times;
    if (!occurrenceTimes(master, key, entry, &times, error)) return false;
    auto value = [&](const char *name) {
        const QString key = QString::fromLatin1(name);
        return entry.contains(key) ? entry.value(key).toString() : master.value(key).toString();
    };
    setField(w, QStringLiteral("calendar:Start"), [&] { textT(w, QStringLiteral("Start"), utcText(times.start)); });
    setField(w, QStringLiteral("calendar:End"), [&] { textT(w, QStringLiteral("End"), utcText(times.end)); });
    setField(w, QStringLiteral("item:Subject"), [&] { textT(w, QStringLiteral("Subject"), value("title")); });
    setField(w, QStringLiteral("calendar:Location"), [&] { textT(w, QStringLiteral("Location"), value("location")); });
    setField(w, QStringLiteral("item:Body"), [&] { writeBody(w, value("description")); });
    return true;
}

} // namespace QindaOffice::Mail::Ews
