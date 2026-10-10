// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to plan_service_project.cpp and plan_service_schedule.cpp (M83
// moved them here from the former): working hours and recurrences as
// Change Working Time shows and takes them.

#include "plan_json.h"
#include "plan_types.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <optional>

namespace QindaOffice::Apps::Plan {

namespace Engine = ::QindaOffice::Plan;

inline QString hoursText(const Engine::DayHours &hours)
{
    if (hours.isEmpty()) {
        return QStringLiteral("Nonworking");
    }
    QStringList parts;
    for (const Engine::WorkingTime &wt : hours) {
        parts.append(Engine::Json::clock(wt.from) + QStringLiteral("–") + Engine::Json::clock(wt.to));
    }
    return parts.join(QStringLiteral(", "));
}

// "08:00-12:00, 13:00-17:00" | "nonworking" | "" (non-working).
inline std::optional<Engine::DayHours> parseHours(const QString &text, QString *error)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t.compare(QLatin1String("nonworking"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("non-working"), Qt::CaseInsensitive) == 0) {
        return Engine::DayHours{};
    }
    static const QRegularExpression range(QStringLiteral("^\\s*(\\d{1,2}:\\d{2})\\s*[-–]\\s*(\\d{1,2}:\\d{2})\\s*$"));
    Engine::DayHours out;
    for (const QString &part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = range.match(part);
        const int from = m.hasMatch() ? Engine::Json::parseClock(m.captured(1)) : -1;
        const int to = m.hasMatch() ? Engine::Json::parseClock(m.captured(2)) : -1;
        if (from < 0 || to <= from || (!out.isEmpty() && from < out.last().to)) {
            *error = QStringLiteral("Write working times like 08:00–12:00, 13:00–17:00 (in order, not overlapping).");
            return std::nullopt;
        }
        out.append({from, to});
    }
    return out;
}

// M79: how Project's Change Working Time describes a recurrence.
inline QString describeRecurrence(const Engine::RecurringException &rx, const QLocale &locale)
{
    using Kind = Engine::RecurringException::Kind;
    static const char *const kPositions[] = {"first", "second", "third", "fourth", "last"};
    const QString weekday = locale.dayName(rx.weekday, QLocale::LongFormat);
    const QString position = QString::fromLatin1(kPositions[std::clamp(rx.position, 0, 4)]);
    switch (rx.kind) {
    case Kind::EveryDays:
        return rx.every == 1 ? QStringLiteral("Every day") : QStringLiteral("Every %1 days").arg(rx.every);
    case Kind::Weekly: {
        QStringList days;
        for (int d = 0; d < 7; ++d) {
            if ((rx.daysOfWeek >> d) & 1) {
                days.append(locale.dayName(d + 1, QLocale::ShortFormat));
            }
        }
        const QString list = days.join(QStringLiteral(", "));
        return rx.every == 1 ? QStringLiteral("Every week on %1").arg(list)
                             : QStringLiteral("Every %1 weeks on %2").arg(rx.every).arg(list);
    }
    case Kind::MonthlyByDay:
        return QStringLiteral("Day %1 of every %2").arg(rx.day).arg(rx.every == 1 ? QStringLiteral("month")
                                                                                  : QStringLiteral("%1 months").arg(rx.every));
    case Kind::MonthlyByPosition:
        return QStringLiteral("The %1 %2 of every %3").arg(position, weekday,
                                                          rx.every == 1 ? QStringLiteral("month") : QStringLiteral("%1 months").arg(rx.every));
    case Kind::YearlyByDay:
        return QStringLiteral("Every year on %1 %2").arg(rx.day).arg(locale.monthName(rx.month, QLocale::LongFormat));
    case Kind::YearlyByPosition:
        return QStringLiteral("Every year on the %1 %2 of %3").arg(position, weekday, locale.monthName(rx.month, QLocale::LongFormat));
    }
    return {};
}

} // namespace QindaOffice::Apps::Plan
