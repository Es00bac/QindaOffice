// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to plan_fields.cpp and plan_field_edit.cpp.

#include "plan_types.h"

#include <QLocale>

#include <algorithm>

namespace QindaOffice::Plan {

// M79: the tracking, work and leveling fields of plan_fields.cpp (Act.
// Dur., Rem. Dur., Act./Rem. Work, Baseline Work, Work Var., Leveling
// Delay, Preleveled Start/Finish); empty for any other field.
[[nodiscard]] QString trackingFieldText(const Project &project, int index, const QString &field, const QLocale &locale);
// AGENT-NOTE (M79): % complete is the stored truth (an integer, as in
// Project and MSPDI's PercentComplete); the actual duration it stands for
// is any value that rounds to it. The roundest such value — whole days,
// then half days, tenths of a day, whole hours — is shown (what was typed,
// as a rule), else the exact share: "50 %, then 2 days remaining" reads
// back as 1 day actual, not 0.99, and 30 % of 39 days as 11.7 days.
inline Minutes actualDurationOf(const Task &t, int minutesPerDay)
{
    const Minutes exact = (t.duration * t.percentComplete + 50) / 100;
    if (t.percentComplete <= 0 || t.percentComplete >= 100 || t.duration <= 0) {
        return exact;
    }
    const double lo = double(t.duration) * (t.percentComplete - 0.5) / 100.0;
    const double hi = double(t.duration) * (t.percentComplete + 0.5) / 100.0;
    const Minutes day = std::max(1, minutesPerDay);
    for (const Minutes step : {day, day / 2, day / 10, Minutes(60)}) {
        if (step <= 0) {
            continue;
        }
        const Minutes round = (exact + step / 2) / step * step;
        if (double(round) >= lo && double(round) < hi) {
            return round;
        }
    }
    return exact;
}

// Typing Leveling Delay, Act. Dur. or Rem. Dur. (plan_field_edit.cpp).
struct EditResult;
EditResult setTrackingField(Project &project, int index, const QString &field, const QString &text);

// ---- M83 (plan_field_work.cpp) ----
// Work, Type, Effort Driven, Act. Work, Act. Cost and the custom fields
// (Text1 … Finish10): typed, and as text (empty for any other field).
EditResult setWorkOrCustomField(Project &project, int index, const QString &field, const QString &text,
                                const QLocale &locale);
[[nodiscard]] QString workOrCustomFieldText(const Project &project, int index, const QString &field,
                                            const QLocale &locale);
// The actual work % complete stands for, read as actualDurationOf reads the
// actual duration: the roundest value that rounds to it (whole days, whole
// hours, half and quarter hours), else the exact share.
[[nodiscard]] Minutes actualWorkOf(const Task &task, int minutesPerDay);
// Actual Cost: typed (while the project lets it be typed) or the task's
// cost × % complete, as Project calculates it.
[[nodiscard]] qint64 actualCostOf(const Project &project, const Task &task);

// The constraint's name as the sheet shows it ("Start No Earlier Than").
inline QString constraintName(ConstraintType type)
{
    static const QString names[] = {
        QStringLiteral("As Soon As Possible"),    QStringLiteral("As Late As Possible"),
        QStringLiteral("Must Start On"),          QStringLiteral("Must Finish On"),
        QStringLiteral("Start No Earlier Than"),  QStringLiteral("Start No Later Than"),
        QStringLiteral("Finish No Earlier Than"), QStringLiteral("Finish No Later Than"),
    };
    return names[int(type)];
}

} // namespace QindaOffice::Plan
