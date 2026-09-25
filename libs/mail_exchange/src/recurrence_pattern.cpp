// SPDX-License-Identifier: GPL-3.0-or-later
#include "recurrence_pattern.h"

#include <QStringList>
#include <QVariantMap>
#include <algorithm>

namespace QindaOffice::Mail {
namespace {
const QStringList &names()
{
    static const QStringList days{QStringLiteral("monday"), QStringLiteral("tuesday"), QStringLiteral("wednesday"),
        QStringLiteral("thursday"), QStringLiteral("friday"), QStringLiteral("saturday"), QStringLiteral("sunday")};
    return days;
}

QString byDay(const QList<int> &days)
{
    static const QStringList codes{QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"),
        QStringLiteral("TH"), QStringLiteral("FR"), QStringLiteral("SA"), QStringLiteral("SU")};
    QStringList out;
    for (int day : days)
        if (day >= 1 && day <= 7) out.append(codes.at(day - 1));
    return out.join(QLatin1Char(','));
}

QString setPosition(const QString &index)
{
    static const QStringList order{QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third"),
                                   QStringLiteral("fourth")};
    const int at = order.indexOf(index.toLower());
    return at >= 0 ? QString::number(at + 1) : QStringLiteral("-1");
}

// "BYMONTHDAY=d", or the Exchange last-day-of-short-month form for 29-31.
QString monthDay(int day)
{
    if (day <= 28) return QStringLiteral("BYMONTHDAY=%1").arg(day);
    QStringList days;
    for (int d = 28; d <= day; ++d) days.append(QString::number(d));
    return QStringLiteral("BYMONTHDAY=%1;BYSETPOS=-1").arg(days.join(QLatin1Char(',')));
}

QString rawRule(const RecurrencePattern &p)
{
    QStringList parts;
    const QString type = p.type;
    static const QStringList known{QStringLiteral("daily"), QStringLiteral("weekly"), QStringLiteral("absoluteMonthly"),
        QStringLiteral("relativeMonthly"), QStringLiteral("absoluteYearly"), QStringLiteral("relativeYearly")};
    // Task regeneration patterns and anything newer: named, never expanded.
    if (!known.contains(type)) return QStringLiteral("X-UNSUPPORTED-PATTERN=") + type;
    const QString frequency = type == QLatin1String("daily") ? QStringLiteral("DAILY")
        : type == QLatin1String("weekly") ? QStringLiteral("WEEKLY")
        : type.endsWith(QLatin1String("Monthly")) ? QStringLiteral("MONTHLY") : QStringLiteral("YEARLY");
    parts.append(QStringLiteral("FREQ=") + frequency);
    if (p.interval > 1) parts.append(QStringLiteral("INTERVAL=%1").arg(p.interval));
    if (type.endsWith(QLatin1String("Yearly")) && p.month >= 1) parts.append(QStringLiteral("BYMONTH=%1").arg(p.month));
    if (type == QLatin1String("weekly") && !p.days.isEmpty()) parts.append(QStringLiteral("BYDAY=") + byDay(p.days));
    if (type.startsWith(QLatin1String("absolute")) && p.dayOfMonth >= 1) {
        parts.append(p.month == 2 && p.dayOfMonth == 29 && type == QLatin1String("absoluteYearly")
                         ? QStringLiteral("BYMONTHDAY=28,29;BYSETPOS=-1") : monthDay(p.dayOfMonth));
    }
    if (type.startsWith(QLatin1String("relative")))
        parts.append(QStringLiteral("BYDAY=%1;BYSETPOS=%2").arg(byDay(p.days), setPosition(p.index)));
    if (type == QLatin1String("weekly") && !p.firstDayOfWeek.isEmpty() && p.firstDayOfWeek != QLatin1String("monday"))
        parts.append(QStringLiteral("WKST=") + byDay(dayNumbers(p.firstDayOfWeek)));
    if (p.range == QLatin1String("numbered")) parts.append(QStringLiteral("COUNT=%1").arg(p.count));
    else if (p.range == QLatin1String("endDate") && p.endDate.isValid())
        parts.append(QStringLiteral("UNTIL=") + p.endDate.toString(QStringLiteral("yyyyMMdd")));
    return parts.join(QLatin1Char(';'));
}
} // namespace

QList<int> dayNumbers(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("day")) return {1, 2, 3, 4, 5, 6, 7};
    if (lower == QLatin1String("weekday")) return {1, 2, 3, 4, 5};
    if (lower == QLatin1String("weekendday")) return {6, 7};
    const int at = names().indexOf(lower);
    return at >= 0 ? QList<int>{at + 1} : QList<int>{};
}

QString dayName(int day)
{
    return day >= 1 && day <= 7 ? names().at(day - 1) : QString();
}

MappedRecurrence patternToRule(const RecurrencePattern &p, const QDate &start)
{
    QList<int> days = p.days;
    std::sort(days.begin(), days.end());
    days.erase(std::unique(days.begin(), days.end()), days.end());
    QVariantMap rule{{QStringLiteral("interval"), qMax(1, p.interval)}, {QStringLiteral("endMode"), QStringLiteral("never")}};
    bool mappable = true;
    if (p.type == QLatin1String("daily")) {
        rule.insert(QStringLiteral("frequency"), QStringLiteral("daily"));
    } else if (p.type == QLatin1String("weekly")) {
        rule.insert(QStringLiteral("frequency"), QStringLiteral("weekly"));
        QVariantList weekdays;
        for (int day : std::as_const(days)) weekdays.append(day);
        rule.insert(QStringLiteral("weekdays"), weekdays);
        const QString first = p.firstDayOfWeek.toLower();
        const bool sameWeeks = p.interval <= 1 || days.size() < 2 || first.isEmpty() || first == QLatin1String("monday")
            || (first == QLatin1String("sunday") && !days.contains(7));
        mappable = !days.isEmpty() && sameWeeks;
    } else if (p.type == QLatin1String("absoluteMonthly")) {
        rule.insert(QStringLiteral("frequency"), QStringLiteral("monthly"));
        mappable = p.dayOfMonth == start.day() && p.dayOfMonth <= 28;
    } else if (p.type == QLatin1String("absoluteYearly")) {
        rule.insert(QStringLiteral("frequency"), QStringLiteral("yearly"));
        mappable = p.dayOfMonth == start.day() && p.month == start.month() && !(p.month == 2 && p.dayOfMonth == 29);
    } else {
        mappable = false;                            // relative patterns and anything unknown
    }
    if (p.range == QLatin1String("numbered")) {
        rule.insert(QStringLiteral("endMode"), QStringLiteral("count"));
        rule.insert(QStringLiteral("count"), p.count);
        mappable = mappable && p.count >= 1;
    } else if (p.range == QLatin1String("endDate")) {
        rule.insert(QStringLiteral("endMode"), QStringLiteral("until"));
        rule.insert(QStringLiteral("until"), p.endDate.toString(Qt::ISODate));
        mappable = mappable && p.endDate.isValid();
    }
    if (mappable) return {rule, {}};
    return {QVariant::fromValue(nullptr), rawRule(p)};
}

bool ruleToPattern(const QVariantMap &rule, const QDate &start, bool yearlyInterval,
                   RecurrencePattern *pattern, QString *error)
{
    RecurrencePattern p;
    const QString frequency = rule.value(QStringLiteral("frequency")).toString();
    p.interval = qMax(1, rule.value(QStringLiteral("interval"), 1).toInt());
    p.startDate = start;
    if (frequency == QLatin1String("daily")) {
        p.type = QStringLiteral("daily");
    } else if (frequency == QLatin1String("weekly")) {
        p.type = QStringLiteral("weekly");
        for (const QVariant &day : rule.value(QStringLiteral("weekdays")).toList())
            if (day.toInt() >= 1 && day.toInt() <= 7 && !p.days.contains(day.toInt())) p.days.append(day.toInt());
        if (p.days.isEmpty()) p.days.append(start.dayOfWeek());
        std::sort(p.days.begin(), p.days.end());
        p.firstDayOfWeek = QStringLiteral("monday");
    } else if (frequency == QLatin1String("monthly")) {
        if (start.day() > 28) {
            *error = QStringLiteral("Exchange repeats a monthly event on day %1 on the last day of shorter months, "
                                    "but this series skips those months").arg(start.day());
            return false;
        }
        p.type = QStringLiteral("absoluteMonthly");
        p.dayOfMonth = start.day();
    } else if (frequency == QLatin1String("yearly")) {
        if (start.month() == 2 && start.day() == 29) {
            *error = QStringLiteral("Exchange repeats a 29 February event on 28 February in other years, "
                                    "but this series repeats only in leap years");
            return false;
        }
        if (!yearlyInterval && p.interval > 1) {
            *error = QStringLiteral("Exchange Web Services cannot repeat an event every %1 years").arg(p.interval);
            return false;
        }
        p.type = QStringLiteral("absoluteYearly");
        p.dayOfMonth = start.day();
        p.month = start.month();
    } else {
        *error = QStringLiteral("Unsupported recurrence frequency \"%1\"").arg(frequency);
        return false;
    }
    const QString endMode = rule.value(QStringLiteral("endMode"), QStringLiteral("never")).toString();
    if (endMode == QLatin1String("count")) {
        p.range = QStringLiteral("numbered");
        p.count = rule.value(QStringLiteral("count")).toInt();
        if (p.count < 1) { *error = QStringLiteral("A counted series needs at least one occurrence"); return false; }
    } else if (endMode == QLatin1String("until")) {
        p.range = QStringLiteral("endDate");
        p.endDate = QDate::fromString(rule.value(QStringLiteral("until")).toString().left(10), Qt::ISODate);
        if (!p.endDate.isValid()) { *error = QStringLiteral("The series end date is invalid"); return false; }
    }
    *pattern = p;
    return true;
}

} // namespace QindaOffice::Mail
