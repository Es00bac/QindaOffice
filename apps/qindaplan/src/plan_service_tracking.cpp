// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: Project's tracking and leveling commands (M79, D-088) —
// Mark on Track, Update Tasks, the Resource Leveling options, Level
// Selection and Clear Leveling of the selected tasks. Each is one undo step
// through PlanDocument::apply; a refusal changes nothing.
#include "leveling.h"
#include "outline.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_reports.h"
#include "plan_service.h"
#include "scheduler.h"

namespace QindaOffice::Apps::Plan {

namespace {

// The selected tasks and everything under a selected summary, as uids.
QList<int> selectedTree(const Engine::Project &p, const QList<int> &rows)
{
    QList<int> uids;
    for (int row : rows) {
        const int end = Engine::subtreeEnd(p, row);
        for (int k = row; k < end; ++k) {
            if (!uids.contains(p.tasks.at(k).uid)) {
                uids.append(p.tasks.at(k).uid);
            }
        }
    }
    return uids;
}

QString orderKey(Engine::LevelingOrder order)
{
    return order == Engine::LevelingOrder::IdOnly            ? QStringLiteral("id")
           : order == Engine::LevelingOrder::PriorityStandard ? QStringLiteral("priority")
                                                              : QStringLiteral("standard");
}

} // namespace

QVariantMap PlanService::markOnTrack()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks that are on track."));
    }
    const Engine::Minutes through = Engine::reportReference(project(), m_today);
    return apply(QStringLiteral("Mark on Track"), [&](Engine::Project &p) {
        Engine::updateProgress(p, through, rows);
        return QString();
    });
}

QVariantMap PlanService::updateTasks(const QVariantMap &fields)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks to update."));
    }
    // Project's Update Tasks: what was typed applies to every selected task,
    // in this order (a % complete first, so a typed actual start wins).
    static const QStringList order{QStringLiteral("percent"), QStringLiteral("actualDuration"),
                                   QStringLiteral("remainingDuration"), QStringLiteral("actualStart"),
                                   QStringLiteral("actualFinish")};
    const QLocale locale = m_locale;
    return apply(QStringLiteral("Update Tasks"), [&](Engine::Project &p) {
        for (int row : rows) {
            for (const QString &f : order) {
                if (!fields.contains(f)) {
                    continue;
                }
                const QString text = fields.value(f).toString();
                if (text.trimmed().isEmpty() || text == Engine::taskFieldEditText(p, row, f, locale)) {
                    continue;
                }
                const Engine::EditResult r = Engine::setTaskField(p, row, f, text, locale);
                if (!r.ok) {
                    return QStringLiteral("%1 (%2): %3").arg(Engine::taskFieldTitle(f), p.tasks.at(row).name, r.error);
                }
                Engine::schedule(p);
            }
        }
        return QString();
    });
}

QVariantMap PlanService::levelingOptions() const
{
    const Engine::ProjectInfo &info = project().info;
    return {{QStringLiteral("order"), orderKey(info.levelingOrder)},
            {QStringLiteral("withinSlack"), info.levelWithinSlack},
            {QStringLiteral("clearFirst"), info.levelClearFirst}};
}

QVariantMap PlanService::setLevelingOptions(const QVariantMap &options)
{
    const QString order = options.value(QStringLiteral("order"), orderKey(project().info.levelingOrder)).toString();
    if (order != QLatin1String("id") && order != QLatin1String("standard") && order != QLatin1String("priority")) {
        return fail(QStringLiteral("The leveling order is ID Only, Standard or Priority, Standard."));
    }
    const QVariantMap now = levelingOptions();
    QVariantMap merged = now;
    for (auto it = options.cbegin(); it != options.cend(); ++it) {
        merged.insert(it.key(), it.value());
    }
    if (merged == now) {
        return ok();
    }
    return apply(QStringLiteral("Leveling Options"), [&](Engine::Project &p) {
        p.info.levelingOrder = order == QLatin1String("id")         ? Engine::LevelingOrder::IdOnly
                               : order == QLatin1String("priority") ? Engine::LevelingOrder::PriorityStandard
                                                                    : Engine::LevelingOrder::Standard;
        p.info.levelWithinSlack = merged.value(QStringLiteral("withinSlack")).toBool();
        p.info.levelClearFirst = merged.value(QStringLiteral("clearFirst")).toBool();
        return QString();
    });
}

QVariantMap PlanService::levelSelection()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks to level."));
    }
    Engine::LevelingOptions options = Engine::projectLevelingOptions(project());
    options.taskUids = selectedTree(project(), rows);
    Engine::LevelingResult result;
    const QVariantMap r = apply(QStringLiteral("Level Selection"), [&](Engine::Project &p) {
        result = Engine::levelResources(p, options);
        return result.delayedTasks > 0 ? QString()
                                       : (result.resolved ? QStringLiteral("The selected tasks are not overallocated.")
                                                          : QStringLiteral("Leveling could not delay a selected task."));
    });
    return leveled(r, result);
}

QVariantMap PlanService::clearLevelingSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks whose leveling to clear."));
    }
    const QList<int> uids = selectedTree(project(), rows);
    return apply(QStringLiteral("Clear Leveling"), [&](Engine::Project &p) {
        return Engine::clearLeveling(p, uids) > 0 ? QString() : QStringLiteral("No selected task has a leveling delay.");
    });
}

} // namespace QindaOffice::Apps::Plan
