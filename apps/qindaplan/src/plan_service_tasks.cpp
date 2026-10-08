// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: task edits (plan_service.h). Every call is one undo step.
#include "outline.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_service.h"
#include "plan_time.h"
#include "scheduler.h"

namespace QindaOffice::Apps::Plan {

namespace {

QString errorOf(const Engine::EditResult &r)
{
    return r.ok ? QString() : r.error;
}

// The fields the Task Information dialog edits, in the order they apply
// (the mode first, so dates typed for a manual task stay manual).
const QStringList &dialogFields()
{
    static const QStringList fields{
        QStringLiteral("mode"),       QStringLiteral("name"),           QStringLiteral("duration"),
        QStringLiteral("constraint"), QStringLiteral("constraintDate"), QStringLiteral("start"),
        QStringLiteral("finish"),     QStringLiteral("percent"),        QStringLiteral("actualStart"),
        QStringLiteral("actualFinish"), QStringLiteral("predecessors"), QStringLiteral("resources"),
        QStringLiteral("active"),     QStringLiteral("milestone"),      QStringLiteral("calendar"),
        QStringLiteral("fixedCost"),  QStringLiteral("priority"),       QStringLiteral("deadline"),
        QStringLiteral("notes")};
    return fields;
}

} // namespace

QVariantMap PlanService::setTaskField(const QString &taskId, const QString &field, const QString &text)
{
    const int row = rowOf(taskId);
    if (row < 0) {
        return fail(QStringLiteral("There is no such task."));
    }
    const QLocale locale = m_locale;
    return apply(QStringLiteral("Edit %1").arg(Engine::taskFieldTitle(field)), [&](Engine::Project &p) {
        return errorOf(Engine::setTaskField(p, row, field, text, locale));
    });
}

QString PlanService::taskFieldText(const QString &taskId, const QString &field) const
{
    const int row = rowOf(taskId);
    return row < 0 ? QString() : Engine::taskFieldText(project(), row, field, m_locale);
}

QVariantMap PlanService::taskInfo(const QString &taskId) const
{
    const int row = rowOf(taskId);
    if (row < 0) {
        return {};
    }
    const Engine::Project &p = project();
    const Engine::Task &t = p.tasks.at(row);
    QVariantMap info{{QStringLiteral("taskId"), taskId},
                     {QStringLiteral("summary"), t.summary},
                     {QStringLiteral("critical"), t.critical},
                     {QStringLiteral("warning"), t.warning},
                     {QStringLiteral("manual"), t.manual},
                     {QStringLiteral("activeFlag"), t.active},
                     {QStringLiteral("milestoneFlag"), t.isMilestone()},
                     {QStringLiteral("constraintCode"), Engine::constraintCode(t.constraint)}};
    for (const QString &f : Engine::taskFieldIds()) {
        info.insert(f, Engine::taskFieldEditText(p, row, f, m_locale));
    }
    QVariantList links;
    for (const Engine::Link &l : t.predecessors) {
        const int pred = p.taskIndex(l.predecessorUid);
        if (pred < 0) {
            continue;
        }
        links.append(QVariantMap{{QStringLiteral("id"), p.tasks.at(pred).id},
                                 {QStringLiteral("name"), p.tasks.at(pred).name},
                                 {QStringLiteral("type"), Engine::linkTypeCode(l.type)},
                                 {QStringLiteral("lag"), Engine::formatLag(l, Engine::durationFormatOf(p.info), m_locale)}});
    }
    info.insert(QStringLiteral("predecessorList"), links);
    QStringList calendars{QStringLiteral("None")};
    for (const Engine::Calendar &c : p.calendars) {
        calendars.append(c.name);
    }
    info.insert(QStringLiteral("calendars"), calendars);
    return info;
}

QVariantMap PlanService::saveTaskInfo(const QString &taskId, const QVariantMap &fields)
{
    const int row = rowOf(taskId);
    if (row < 0) {
        return fail(QStringLiteral("There is no such task."));
    }
    const QLocale locale = m_locale;
    return apply(QStringLiteral("Task Information"), [&](Engine::Project &p) {
        for (const QString &f : dialogFields()) {
            if (!fields.contains(f)) {
                continue;
            }
            const QString text = fields.value(f).toString();
            // Only what changed: retyping an unchanged start would pin it.
            if (text == Engine::taskFieldEditText(p, row, f, locale)) {
                continue;
            }
            const Engine::EditResult r = Engine::setTaskField(p, row, f, text, locale);
            if (!r.ok) {
                return QStringLiteral("%1: %2").arg(Engine::taskFieldTitle(f), r.error);
            }
            // Later fields read the effect of earlier ones (a new duration
            // moves the finish the dialog still shows as it was).
            Engine::schedule(p);
        }
        return QString();
    });
}

QVariantMap PlanService::insertTask(const QString &kind, const QString &name)
{
    const QList<int> rows = selectedRows();
    const int at = rows.isEmpty() ? int(project().tasks.size()) : rows.first();
    const Engine::NewTaskKind k = kind == QLatin1String("milestone") ? Engine::NewTaskKind::Milestone
                                  : kind == QLatin1String("summary") ? Engine::NewTaskKind::Summary
                                                                     : Engine::NewTaskKind::Task;
    QString label = name;
    if (label.isEmpty()) {
        label = k == Engine::NewTaskKind::Milestone ? QStringLiteral("New milestone")
                : k == Engine::NewTaskKind::Summary ? QStringLiteral("New summary task")
                                                    : QStringLiteral("New task");
    }
    QString created;
    QVariantMap r;
    if (k == Engine::NewTaskKind::Summary && !rows.isEmpty()) {
        // Project's Insert Summary Task: wrap the selection.
        r = apply(QStringLiteral("Insert Summary Task"), [&](Engine::Project &p) {
            const int s = Engine::insertSummaryAbove(p, rows, label);
            if (s < 0) {
                return QStringLiteral("The selected tasks cannot be put under one summary task.");
            }
            created = p.tasks.at(s).guid.toString(QUuid::WithoutBraces);
            return QString();
        });
    } else {
        r = apply(QStringLiteral("Insert Task"), [&](Engine::Project &p) {
            const int index = Engine::addTask(p, at, k, label);
            created = p.tasks.at(index).guid.toString(QUuid::WithoutBraces);
            return QString();
        });
    }
    if (r.value(QStringLiteral("ok")).toBool()) {
        setCurrentTaskId(created);
        emit revealTask(created);
        r.insert(QStringLiteral("taskId"), created);
    }
    return r;
}

QVariantMap PlanService::deleteSelectedTasks()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks to delete."));
    }
    return apply(QStringLiteral("Delete Task"), [&](Engine::Project &p) {
        Engine::removeTasks(p, rows);
        return QString();
    });
}

QVariantMap PlanService::indentSelected()
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Indent"), [&](Engine::Project &p) {
        QString error;
        return Engine::indentTasks(p, rows, &error) ? QString() : (error.isEmpty() ? QStringLiteral("Select a task.") : error);
    });
}

QVariantMap PlanService::outdentSelected()
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Outdent"), [&](Engine::Project &p) {
        QString error;
        return Engine::outdentTasks(p, rows, &error) ? QString() : (error.isEmpty() ? QStringLiteral("Select a task.") : error);
    });
}

QVariantMap PlanService::moveSelected(bool up)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select a task to move."));
    }
    return apply(up ? QStringLiteral("Move Up") : QStringLiteral("Move Down"), [&](Engine::Project &p) {
        return Engine::moveTask(p, rows.first(), up) < 0 ? QStringLiteral("The task cannot move further.") : QString();
    });
}

QVariantMap PlanService::linkSelected()
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Link Tasks"), [&](Engine::Project &p) { return errorOf(Engine::linkTasks(p, rows)); });
}

QVariantMap PlanService::unlinkSelected()
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Unlink Tasks"), [&](Engine::Project &p) {
        return Engine::unlinkTasks(p, rows) > 0 ? QString() : QStringLiteral("The selected tasks have no links to remove.");
    });
}

QVariantMap PlanService::markSelected(int percent)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return fail(QStringLiteral("Select the tasks to update."));
    }
    return apply(QStringLiteral("Mark %1% Complete").arg(percent), [&](Engine::Project &p) {
        Engine::setPercentComplete(p, rows, percent);
        return QString();
    });
}

QVariantMap PlanService::setSelectedManual(bool manual)
{
    const QList<int> rows = selectedRows();
    return apply(manual ? QStringLiteral("Manually Schedule") : QStringLiteral("Auto Schedule"), [&](Engine::Project &p) {
        Engine::setManual(p, rows, manual);
        return QString();
    });
}

QVariantMap PlanService::toggleSelectedActive()
{
    const QList<int> rows = selectedRows();
    bool allActive = true;
    for (int r : rows) {
        allActive = allActive && project().tasks.at(r).active;
    }
    return apply(allActive ? QStringLiteral("Inactivate") : QStringLiteral("Activate"), [&](Engine::Project &p) {
        Engine::setActive(p, rows, !allActive);
        return QString();
    });
}

QVariantMap PlanService::respectLinksSelected()
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Respect Links"), [&](Engine::Project &p) {
        Engine::respectLinks(p, rows);
        return QString();
    });
}

QVariantMap PlanService::moveTaskStart(const QString &taskId, const QString &startUtc)
{
    const int row = rowOf(taskId);
    const Engine::Minutes start = minutesOfUtc(startUtc);
    return apply(QStringLiteral("Move Task"),
                 [&](Engine::Project &p) { return errorOf(Engine::moveTaskStart(p, row, start)); });
}

void PlanService::toggleSubtasks(const QString &taskId)
{
    const int row = rowOf(taskId.isEmpty() ? currentTaskId() : taskId);
    if (row < 0 || !project().tasks.at(row).summary) {
        return;
    }
    const int uid = project().tasks.at(row).uid;
    if (m_collapsed.contains(uid)) {
        m_collapsed.remove(uid);
    } else {
        m_collapsed.insert(uid);
    }
    rebuild();
}

QVariantMap PlanService::findTask(const QString &text, bool forward)
{
    const QString needle = text.trimmed();
    const Engine::Project &p = project();
    const int n = int(p.tasks.size());
    if (needle.isEmpty() || n == 0) {
        return fail(QStringLiteral("Type part of a task name to find."));
    }
    const int current = rowOf(currentTaskId());
    for (int step = 1; step <= n; ++step) {
        const int start = current < 0 ? (forward ? -1 : n) : current;
        const int row = ((start + (forward ? step : -step)) % n + n) % n;
        if (!p.tasks.at(row).name.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        // Open every collapsed summary above it.
        bool opened = false;
        for (int a = p.tasks.at(row).parentIndex; a >= 0; a = p.tasks.at(a).parentIndex) {
            opened = m_collapsed.remove(p.tasks.at(a).uid) || opened;
        }
        if (opened) {
            rebuild();
        }
        const QString id = idOfRow(row);
        setCurrentTaskId(id);
        emit revealTask(id);
        return ok({{QStringLiteral("taskId"), id}, {QStringLiteral("name"), p.tasks.at(row).name}});
    }
    return fail(QStringLiteral("No task name contains “%1”.").arg(needle));
}

QVariantMap PlanService::replaceInNames(const QString &text, const QString &replacement)
{
    if (text.isEmpty()) {
        return fail(QStringLiteral("Type the text to replace."));
    }
    int count = 0;
    QVariantMap r = apply(QStringLiteral("Replace"), [&](Engine::Project &p) {
        for (Engine::Task &t : p.tasks) {
            if (t.name.contains(text, Qt::CaseInsensitive)) {
                t.name.replace(text, replacement, Qt::CaseInsensitive);
                ++count;
            }
        }
        return count > 0 ? QString() : QStringLiteral("No task name contains “%1”.").arg(text);
    });
    r.insert(QStringLiteral("count"), count);
    return r;
}

void PlanService::expandAll()
{
    if (!m_collapsed.isEmpty()) {
        m_collapsed.clear();
        rebuild();
    }
}

QString PlanService::commitSheetChanges(const QVariantList &changes)
{
    const QLocale locale = m_locale;
    QString created;
    const QString refused = m_document->apply(QStringLiteral("Edit Cells"), [&](Engine::Project &p) {
        int newRow = -1;
        for (const QVariant &v : changes) {
            const QVariantMap c = v.toMap();
            const QString rowId = c.value(QStringLiteral("rowId")).toString();
            const QString field = c.value(QStringLiteral("columnId")).toString();
            const QString text = c.value(QStringLiteral("after")).toString();
            int row = rowId == QLatin1String("new") ? newRow : rowOf(rowId);
            if (rowId == QLatin1String("new") && row < 0) {
                if (text.trimmed().isEmpty()) {
                    continue;
                }
                row = Engine::addTask(p, int(p.tasks.size()), Engine::NewTaskKind::Task,
                                      field == QLatin1String("name") ? text : QStringLiteral("New task"));
                newRow = row;
                created = p.tasks.at(row).guid.toString(QUuid::WithoutBraces);
                if (field == QLatin1String("name")) {
                    continue;
                }
            }
            if (row < 0) {
                return QStringLiteral("That task no longer exists.");
            }
            const Engine::EditResult r = Engine::setTaskField(p, row, field, text, locale);
            if (!r.ok) {
                return r.error;
            }
        }
        return QString();
    });
    if (refused.isEmpty() && !created.isEmpty()) {
        setCurrentTaskId(created);
    }
    if (!refused.isEmpty()) {
        emit notify(QStringLiteral("Not changed"), refused, QStringLiteral("warning"));
    }
    return refused;
}

} // namespace QindaOffice::Apps::Plan
