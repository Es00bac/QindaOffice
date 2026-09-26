// SPDX-License-Identifier: GPL-3.0-or-later
// Typing into the task sheet (plan_edit.h setTaskField): Project's
// semantics for each field (a typed start pins Start No Earlier Than, …).
#include "plan_edit.h"
#include "plan_fields_p.h"
#include "plan_time.h"
#include "scheduler.h"
#include "work_calendar.h"

#include <QRegularExpression>

namespace QindaOffice::Plan {

namespace {

std::optional<bool> parseYesNo(const QString &text)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("yes") || t == QLatin1String("y") || t == QLatin1String("true") || t == QLatin1String("1")) {
        return true;
    }
    if (t == QLatin1String("no") || t == QLatin1String("n") || t == QLatin1String("false") || t == QLatin1String("0")) {
        return false;
    }
    return std::nullopt;
}

std::optional<int> parsePercent(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3})\\s*%?\\s*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    const int v = m.captured(1).toInt();
    return v <= 100 ? std::optional<int>(v) : std::nullopt;
}

bool isNa(const QString &text)
{
    const QString t = text.trimmed();
    return t.isEmpty() || t.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0;
}

EditResult setDuration(Project &project, int index, const QString &text)
{
    Task &t = project.tasks[index];
    if (t.summary) {
        return EditResult::fail(QStringLiteral("A summary task's duration comes from its subtasks."));
    }
    const std::optional<ParsedDuration> d = parseDuration(text, durationFormatOf(project.info));
    if (!d) {
        return EditResult::fail(QStringLiteral("“%1” is not a duration. Try 5d, 3 days, 4h or 2w.").arg(text.trimmed()));
    }
    t.duration = d->minutes;
    t.durationUnit = d->unit;
    t.estimated = d->estimated;
    if (d->minutes > 0) {
        t.milestone = false;
    }
    if (t.manual && t.manualStart != kNoDate) {
        const WorkCalendar c(project, effectiveCalendarUid(project, index));
        t.manualFinish = d->minutes > 0 ? c.add(c.snapForward(t.manualStart), d->minutes) : t.manualStart;
    }
    return {};
}

EditResult setDateField(Project &project, int index, const QString &field, const QString &text, const QLocale &locale)
{
    Task &t = project.tasks[index];
    const bool isStart = field == QLatin1String("start");
    const std::optional<Minutes> date =
        parseDate(text, isStart ? project.info.defaultStartMinute : project.info.defaultFinishMinute, locale);
    if (!date) {
        return EditResult::fail(QStringLiteral("“%1” is not a date.").arg(text.trimmed()));
    }
    const WorkCalendar c(project, effectiveCalendarUid(project, index));
    if (t.summary && !isStart) {
        return EditResult::fail(QStringLiteral("A summary task's finish comes from its subtasks."));
    }
    if (t.manual) {
        if (isStart) {
            t.manualStart = *date;
            t.manualFinish = t.duration > 0 ? c.add(c.snapForward(*date), t.duration) : *date;
        } else {
            if (t.manualStart == kNoDate) {
                t.manualStart = t.start;
            }
            if (*date < t.manualStart) {
                return EditResult::fail(QStringLiteral("The finish cannot be before the start."));
            }
            t.manualFinish = *date;
            t.duration = c.workBetween(t.manualStart, *date);
        }
        return {};
    }
    t.constraint = isStart ? ConstraintType::StartNoEarlierThan : ConstraintType::FinishNoEarlierThan;
    t.constraintDate = *date;
    return {};
}

EditResult setConstraint(Project &project, int index, const QString &text)
{
    Task &t = project.tasks[index];
    std::optional<ConstraintType> type = constraintFromCode(text);
    for (int i = 0; i < 8 && !type; ++i) {
        if (constraintName(ConstraintType(i)).compare(text.trimmed(), Qt::CaseInsensitive) == 0) {
            type = ConstraintType(i);
        }
    }
    if (!type) {
        return EditResult::fail(QStringLiteral("“%1” is not a constraint type.").arg(text.trimmed()));
    }
    if (t.summary && *type != ConstraintType::AsSoonAsPossible && *type != ConstraintType::StartNoEarlierThan) {
        return EditResult::fail(QStringLiteral("A summary task can only start as soon as possible or no earlier than a date."));
    }
    t.constraint = *type;
    if (!constraintNeedsDate(*type)) {
        t.constraintDate = kNoDate;
    } else if (t.constraintDate == kNoDate) {
        const bool finishType = *type == ConstraintType::MustFinishOn || *type == ConstraintType::FinishNoEarlierThan
            || *type == ConstraintType::FinishNoLaterThan;
        t.constraintDate = finishType ? t.finish : t.start;
    }
    return {};
}

} // namespace

EditResult setTaskField(Project &project, int index, const QString &field, const QString &text, const QLocale &locale)
{
    if (index < 0 || index >= project.tasks.size()) {
        return EditResult::fail(QStringLiteral("There is no such task."));
    }
    if (!isTaskFieldEditable(field)) {
        return EditResult::fail(QStringLiteral("%1 is calculated and cannot be typed.").arg(taskFieldTitle(field)));
    }
    Task &t = project.tasks[index];
    if (field == QLatin1String("name")) {
        t.name = text.trimmed();
        return {};
    }
    if (field == QLatin1String("notes")) {
        t.notes = text;
        return {};
    }
    if (field == QLatin1String("duration")) {
        return setDuration(project, index, text);
    }
    if (field == QLatin1String("start") || field == QLatin1String("finish")) {
        return setDateField(project, index, field, text, locale);
    }
    if (field == QLatin1String("predecessors")) {
        QString error;
        const std::optional<QList<Link>> links = parsePredecessors(project, index, text, &error, locale);
        if (!links) {
            return EditResult::fail(error);
        }
        project.tasks[index].predecessors = *links;
        return {};
    }
    if (field == QLatin1String("resources")) {
        return setResourceNames(project, index, text, locale);
    }
    if (field == QLatin1String("percent")) {
        const std::optional<int> p = parsePercent(text);
        if (!p) {
            return EditResult::fail(QStringLiteral("% Complete is a whole number from 0 to 100."));
        }
        setPercentComplete(project, {index}, *p);
        return {};
    }
    if (field == QLatin1String("mode")) {
        const QString m = text.trimmed().toLower();
        if (m.startsWith(QLatin1String("auto")) || m.startsWith(QLatin1String("manual"))) {
            setManual(project, {index}, m.startsWith(QLatin1String("manual")));
            return {};
        }
        return EditResult::fail(QStringLiteral("Mode is Auto or Manual."));
    }
    if (field == QLatin1String("constraint")) {
        return setConstraint(project, index, text);
    }
    if (field == QLatin1String("constraintDate")) {
        if (isNa(text)) {
            t.constraint = ConstraintType::AsSoonAsPossible;
            t.constraintDate = kNoDate;
            return {};
        }
        const std::optional<Minutes> d = parseDate(text, project.info.defaultStartMinute, locale);
        if (!d) {
            return EditResult::fail(QStringLiteral("“%1” is not a date.").arg(text.trimmed()));
        }
        if (!constraintNeedsDate(t.constraint)) {
            t.constraint = ConstraintType::StartNoEarlierThan;
        }
        t.constraintDate = *d;
        return {};
    }
    if (field == QLatin1String("actualStart") || field == QLatin1String("actualFinish")) {
        const bool start = field == QLatin1String("actualStart");
        if (isNa(text)) {
            (start ? t.actualStart : t.actualFinish) = kNoDate;
            if (start) {
                t.percentComplete = 0;
                t.actualFinish = kNoDate;
            } else if (t.percentComplete >= 100) {
                t.percentComplete = 99;
            }
            return {};
        }
        const std::optional<Minutes> d =
            parseDate(text, start ? project.info.defaultStartMinute : project.info.defaultFinishMinute, locale);
        if (!d) {
            return EditResult::fail(QStringLiteral("“%1” is not a date.").arg(text.trimmed()));
        }
        if (start) {
            t.actualStart = *d;
        } else {
            if (t.actualStart == kNoDate) {
                t.actualStart = t.start;
            }
            if (*d < t.actualStart) {
                return EditResult::fail(QStringLiteral("The actual finish cannot be before the actual start."));
            }
            t.actualFinish = *d;
            t.percentComplete = 100;
        }
        return {};
    }
    if (field == QLatin1String("fixedCost")) {
        const std::optional<qint64> money = parseMoney(text, project.info.currency, locale);
        if (!money) {
            return EditResult::fail(QStringLiteral("“%1” is not an amount.").arg(text.trimmed()));
        }
        t.fixedCost = *money;
        return {};
    }
    if (field == QLatin1String("active") || field == QLatin1String("milestone")) {
        const std::optional<bool> yes = parseYesNo(text);
        if (!yes) {
            return EditResult::fail(QStringLiteral("Type Yes or No."));
        }
        if (field == QLatin1String("active")) {
            setActive(project, {index}, *yes);
        } else {
            t.milestone = *yes;
        }
        return {};
    }
    if (field == QLatin1String("calendar")) {
        const QString name = text.trimmed();
        if (name.isEmpty() || name.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0) {
            t.calendarUid = -1;
            return {};
        }
        for (const Calendar &c : std::as_const(project.calendars)) {
            if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
                t.calendarUid = c.uid;
                return {};
            }
        }
        return EditResult::fail(QStringLiteral("There is no calendar called “%1”.").arg(name));
    }
    return EditResult::fail(QStringLiteral("Unknown field %1.").arg(field));
}

} // namespace QindaOffice::Plan
