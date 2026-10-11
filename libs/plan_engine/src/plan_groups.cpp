// SPDX-License-Identifier: GPL-3.0-or-later
// Project's Group By (M89, D-097): bands of task rows by a field's value,
// with their rollups. See plan_groups.h for the contract.
#include "plan_groups.h"

#include "custom_fields.h"
#include "plan_edit.h"
#include "plan_reports.h"
#include "plan_time.h"
#include "work_calendar.h"

#include <QCollator>
#include <QHash>

#include <algorithm>
#include <numeric>
#include <optional>

namespace QindaOffice::Plan {

namespace {

// Where a task falls: its band's key and label, and how bands sort.
struct Placement {
    QString key;
    QString label;
    std::optional<double> number; // sorts by number when every band has one
    bool empty = false;           // a task without a value sorts last
};

QString yesNo(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

Placement placeBuiltIn(const Project &p, int i, const QString &id, Minutes reference, const QLocale &locale)
{
    const Task &t = p.tasks.at(i);
    if (id == QLatin1String("complete")) {
        const int stage = t.percentComplete >= 100 ? 2 : t.percentComplete > 0 ? 1 : 0;
        static const char *const labels[] = {"Not started", "In progress", "Complete"};
        return {QString::number(stage), QString::fromLatin1(labels[stage]), stage};
    }
    if (id == QLatin1String("constraint")) {
        const QString text = taskFieldText(p, i, QStringLiteral("constraint"), locale);
        return {text, QStringLiteral("Constraint Type: %1").arg(text), double(int(t.constraint))};
    }
    if (id == QLatin1String("critical")) {
        return {yesNo(t.critical), QStringLiteral("Critical: %1").arg(yesNo(t.critical)), t.critical ? 1.0 : 0.0};
    }
    if (id == QLatin1String("duration")) {
        const QString text = taskFieldText(p, i, QStringLiteral("duration"), locale);
        return {text, QStringLiteral("Duration: %1").arg(text), double(t.duration)};
    }
    if (id == QLatin1String("milestones")) {
        const bool m = t.isMilestone();
        return {yesNo(m), QStringLiteral("Milestone: %1").arg(yesNo(m)), m ? 1.0 : 0.0};
    }
    if (id == QLatin1String("priority")) {
        return {QString::number(t.priority), QStringLiteral("Priority: %1").arg(t.priority), -double(t.priority)};
    }
    if (id == QLatin1String("resource")) {
        const QString text = resourceNamesText(p, i, locale);
        return {text, text.isEmpty() ? QStringLiteral("No Resource Names") : QStringLiteral("Resource Names: %1").arg(text),
                std::nullopt, text.isEmpty()};
    }
    if (id == QLatin1String("status")) {
        // Project's Status: complete, not started before the status date,
        // behind its schedule, or on it.
        const int stage = t.percentComplete >= 100                                  ? 0
                          : t.start != kNoDate && reference != kNoDate && t.start > reference ? 3
                          : isLate(p, t, reference)                                  ? 2
                                                                                     : 1;
        static const char *const labels[] = {"Complete", "On Schedule", "Late", "Future Task"};
        return {QString::number(stage), QStringLiteral("Status: %1").arg(QString::fromLatin1(labels[stage])), stage};
    }
    if (id == QLatin1String("mode")) {
        return {t.manual ? QStringLiteral("manual") : QStringLiteral("auto"),
                t.manual ? QStringLiteral("Task Mode: Manually Scheduled") : QStringLiteral("Task Mode: Auto Scheduled"),
                t.manual ? 1.0 : 0.0};
    }
    // "active"
    return {yesNo(t.effectiveActive), QStringLiteral("Active: %1").arg(yesNo(t.effectiveActive)),
            t.effectiveActive ? 0.0 : 1.0};
}

Placement placeCustom(const Project &p, int i, const QString &field, const QLocale &locale)
{
    const QString text = customFieldText(p, i, field, locale);
    const QString title = customFieldTitle(p, field);
    Placement out{text, text.isEmpty() ? QStringLiteral("%1: (none)").arg(title) : QStringLiteral("%1: %2").arg(title, text),
                  std::nullopt, text.isEmpty()};
    if (const std::optional<QVariant> value = customFieldValue(p, i, field); value && !text.isEmpty()) {
        bool numeric = false;
        const double n = value->toDouble(&numeric);
        if (numeric && value->typeId() != QMetaType::QString) {
            out.number = n;
        }
    }
    return out;
}

} // namespace

QStringList taskGroupIds()
{
    return {QStringLiteral("none"),       QStringLiteral("complete"), QStringLiteral("constraint"),
            QStringLiteral("critical"),   QStringLiteral("duration"), QStringLiteral("milestones"),
            QStringLiteral("priority"),   QStringLiteral("resource"), QStringLiteral("status"),
            QStringLiteral("mode"),       QStringLiteral("active")};
}

bool isTaskGroupId(const QString &id)
{
    return taskGroupIds().contains(id) || (id.startsWith(QLatin1String("custom:")) && isCustomField(id.mid(7)));
}

QString taskGroupTitle(const Project &project, const QString &id)
{
    static const QHash<QString, QString> titles{
        {QStringLiteral("none"), QStringLiteral("No Group")},
        {QStringLiteral("complete"), QStringLiteral("Complete and Incomplete Tasks")},
        {QStringLiteral("constraint"), QStringLiteral("Constraint Type")},
        {QStringLiteral("critical"), QStringLiteral("Critical")},
        {QStringLiteral("duration"), QStringLiteral("Duration")},
        {QStringLiteral("milestones"), QStringLiteral("Milestones")},
        {QStringLiteral("priority"), QStringLiteral("Priority")},
        {QStringLiteral("resource"), QStringLiteral("Resource Names")},
        {QStringLiteral("status"), QStringLiteral("Status")},
        {QStringLiteral("mode"), QStringLiteral("Auto Scheduled v. Manually Scheduled")},
        {QStringLiteral("active"), QStringLiteral("Active v. Inactive")}};
    if (id.startsWith(QLatin1String("custom:"))) {
        return QStringLiteral("Custom Field: %1").arg(customFieldTitle(project, id.mid(7)));
    }
    return titles.value(id, id);
}

QList<TaskGroup> groupTasks(const Project &project, const QList<int> &rows, const QString &id, Minutes reference,
                            const QLocale &locale)
{
    QList<TaskGroup> groups;
    QList<Placement> placements;
    if (!isTaskGroupId(id) || id == QLatin1String("none")) {
        return groups;
    }
    const bool custom = id.startsWith(QLatin1String("custom:"));
    QHash<QString, int> byKey;
    for (int i : rows) {
        if (i < 0 || i >= project.tasks.size() || project.tasks.at(i).summary) {
            continue;
        }
        const Placement place = custom ? placeCustom(project, i, id.mid(7), locale) : placeBuiltIn(project, i, id, reference, locale);
        auto at = byKey.constFind(place.key);
        if (at == byKey.constEnd()) {
            at = byKey.insert(place.key, int(groups.size()));
            groups.append(TaskGroup{place.key, place.label, {}});
            placements.append(place);
        }
        groups[*at].tasks.append(i);
    }
    // Bands in their value's order: numbers when every band has one, else
    // text; bands without a value last; ties keep their first appearance.
    const bool numeric = std::all_of(placements.cbegin(), placements.cend(),
                                     [](const Placement &p) { return p.empty || p.number.has_value(); });
    QCollator collator(locale);
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    QList<int> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Placement &pa = placements.at(a);
        const Placement &pb = placements.at(b);
        if (pa.empty != pb.empty) {
            return pb.empty;
        }
        if (numeric) {
            return pa.number.value_or(0) < pb.number.value_or(0);
        }
        return collator.compare(pa.key, pb.key) < 0;
    });
    QList<TaskGroup> sorted;
    sorted.reserve(groups.size());
    const WorkCalendar calendar(project, project.info.calendarUid);
    for (int g : std::as_const(order)) {
        TaskGroup group = groups.at(g);
        Minutes weighted = 0;
        Minutes durations = 0;
        int percents = 0;
        for (int i : std::as_const(group.tasks)) {
            const Task &t = project.tasks.at(i);
            if (t.start != kNoDate && (group.start == kNoDate || t.start < group.start)) {
                group.start = t.start;
            }
            if (t.finish != kNoDate && (group.finish == kNoDate || t.finish > group.finish)) {
                group.finish = t.finish;
            }
            group.work += t.work;
            group.cost += t.cost;
            weighted += t.duration * t.percentComplete;
            durations += t.duration;
            percents += t.percentComplete;
        }
        if (group.start != kNoDate && group.finish != kNoDate && group.finish > group.start) {
            group.duration = calendar.workBetween(group.start, group.finish);
        }
        group.percentComplete = durations > 0 ? int(weighted / durations)
                                              : group.tasks.isEmpty() ? 0 : percents / int(group.tasks.size());
        sorted.append(group);
    }
    return sorted;
}

} // namespace QindaOffice::Plan
