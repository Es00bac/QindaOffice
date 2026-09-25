// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The recurrence pattern Exchange Web Services and Microsoft Graph share
// (the same Exchange store object behind both), and its mapping to the
// toolkit's RecurrenceService rule subset.
//
// AGENT-NOTE (semantic mismatches that decide "rule" vs "raw"):
// - Exchange repeats an absolute monthly pattern on day 29-31 on the LAST day
//   of shorter months, and a yearly 29 February on 28 February; RFC 5545 and
//   RecurrenceService skip those months/years. Such series map to
//   `recurrenceRaw` (an equivalent RRULE using BYSETPOS=-1) and a local rule
//   on those days is not written to the server.
// - EWS AbsoluteYearlyRecurrence has no interval; Graph's does.
// - RecurrenceService weeks start on Monday; a multi-day weekly pattern with
//   an interval > 1 and another first day of week groups weeks differently
//   (only a Sunday start with no Sunday in the set is equivalent).
// Relative monthly/yearly patterns ("second Tuesday") are always raw and
// read-only; writes leave the server's pattern untouched.

#include <QDate>
#include <QList>
#include <QString>
#include <QVariant>

namespace QindaOffice::Mail {

struct RecurrencePattern {
    QString type;                   // daily weekly absoluteMonthly relativeMonthly absoluteYearly relativeYearly
    int interval = 1;
    QList<int> days;                // 1 = Monday ... 7 = Sunday
    int dayOfMonth = 0, month = 0;  // month 1-12
    QString index;                  // first second third fourth last (relative patterns)
    QString firstDayOfWeek;         // lower-case English day name; empty = monday
    QString range = QStringLiteral("noEnd");   // noEnd endDate numbered
    QDate startDate, endDate;
    int count = 0;
};

struct MappedRecurrence {
    QVariant rule;                  // RecurrenceService rule map, or null
    QString raw;                    // RRULE text when `rule` is null
};

// `start` is the series' first local date.
MappedRecurrence patternToRule(const RecurrencePattern &pattern, const QDate &start);
// Fails with a user-facing reason when the server cannot express the rule.
bool ruleToPattern(const QVariantMap &rule, const QDate &start, bool yearlyInterval,
                   RecurrencePattern *pattern, QString *error);

// Day names: 1-7 <-> "monday".."sunday" (case-insensitive in; lower-case out).
// EWS also uses the sets "Day", "Weekday" and "WeekendDay".
QList<int> dayNumbers(const QString &name);
QString dayName(int day);

} // namespace QindaOffice::Mail
