// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: resources, assignments and leveling (plan_service.h).
#include "leveling.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_service.h"
#include "plan_time.h"
#include "resources.h"

#include <QRegularExpression>

namespace QindaOffice::Apps::Plan {

namespace {

// "50%" / "50" (percent) for work; "2.5" / "2.5 sheets" for material.
std::optional<int> parseUnitsText(const QString &text, Engine::ResourceType type)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+(?:[.,]\\d+)?)\\s*%?\\s*[^\\d%]*$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    QString number = m.captured(1);
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    const double value = number.toDouble();
    const int units = type == Engine::ResourceType::Work ? qRound(value) : qRound(value * 100.0);
    return units > 0 ? std::optional<int>(units) : std::nullopt;
}

} // namespace

QVariantList PlanService::resourceList() const
{
    QVariantList out;
    const Engine::Project &p = project();
    for (int i = 0; i < p.resources.size(); ++i) {
        const Engine::Resource &r = p.resources.at(i);
        out.append(QVariantMap{
            {QStringLiteral("uid"), r.uid},
            {QStringLiteral("name"), r.name},
            {QStringLiteral("initials"), r.initials},
            {QStringLiteral("material"), r.type == Engine::ResourceType::Material},
            {QStringLiteral("type"), Engine::resourceFieldText(p, i, QStringLiteral("type"), m_locale)},
            {QStringLiteral("maxUnits"), Engine::resourceFieldText(p, i, QStringLiteral("maxUnits"), m_locale)},
            {QStringLiteral("rate"), Engine::rateText(p, r, m_locale)},
            {QStringLiteral("work"), Engine::resourceFieldText(p, i, QStringLiteral("work"), m_locale)},
            {QStringLiteral("cost"), Engine::formatMoney(r.cost, p.info.currency, m_locale)},
            {QStringLiteral("overallocated"), r.overallocated}});
    }
    return out;
}

QVariantList PlanService::assignmentsOf(const QString &taskId) const
{
    QVariantList out;
    const Engine::Project &p = project();
    const int row = rowOf(taskId);
    const int taskUid = row >= 0 ? p.tasks.at(row).uid : -1;
    for (const Engine::Resource &r : p.resources) {
        const int a = p.assignmentIndex(taskUid, r.uid);
        QString units;
        if (a >= 0) {
            const int u = p.assignments.at(a).units;
            units = r.type == Engine::ResourceType::Work
                ? m_locale.toString(u) + QLatin1Char('%')
                : m_locale.toString(double(u) / 100.0, 'g', 10) + (r.materialLabel.isEmpty() ? QString() : QLatin1Char(' ') + r.materialLabel);
        }
        out.append(QVariantMap{{QStringLiteral("resourceUid"), r.uid},
                               {QStringLiteral("name"), r.name},
                               {QStringLiteral("material"), r.type == Engine::ResourceType::Material},
                               {QStringLiteral("assigned"), a >= 0},
                               {QStringLiteral("units"), units},
                               {QStringLiteral("cost"), a >= 0 ? Engine::formatMoney(p.assignments.at(a).cost, p.info.currency, m_locale)
                                                               : QString()},
                               {QStringLiteral("overallocated"), r.overallocated}});
    }
    return out;
}

QVariantMap PlanService::assign(int resourceUid, const QString &unitsText)
{
    const QList<int> rows = selectedRows();
    const int ri = project().resourceIndex(resourceUid);
    if (rows.isEmpty() || ri < 0) {
        return fail(QStringLiteral("Select a task and a resource."));
    }
    const std::optional<int> units = unitsText.trimmed().isEmpty()
        ? std::optional<int>(100)
        : parseUnitsText(unitsText, project().resources.at(ri).type);
    if (!units) {
        return fail(QStringLiteral("“%1” is not an amount of work or material.").arg(unitsText.trimmed()));
    }
    return apply(QStringLiteral("Assign Resource"), [&](Engine::Project &p) {
        for (int row : rows) {
            if (p.tasks.at(row).summary) {
                continue;
            }
            const Engine::EditResult r = Engine::assignResource(p, row, resourceUid, *units);
            if (!r.ok) {
                return r.error;
            }
        }
        return QString();
    });
}

QVariantMap PlanService::unassign(int resourceUid)
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Remove Assignment"), [&](Engine::Project &p) {
        bool any = false;
        for (int row : rows) {
            any = Engine::unassignResource(p, row, resourceUid).ok || any;
        }
        return any ? QString() : QStringLiteral("That resource is not assigned to the selected tasks.");
    });
}

QVariantMap PlanService::addResource(const QString &name, const QString &type)
{
    int uid = 0;
    const QVariantMap r = apply(QStringLiteral("Add Resource"), [&](Engine::Project &p) {
        const int index = Engine::addResource(p, int(p.resources.size()),
                                              name.isEmpty() ? QStringLiteral("New resource") : name,
                                              type == QLatin1String("material") ? Engine::ResourceType::Material
                                                                                : Engine::ResourceType::Work);
        uid = p.resources.at(index).uid;
        return QString();
    });
    if (r.value(QStringLiteral("ok")).toBool()) {
        setCurrentResourceUid(uid);
    }
    QVariantMap out = r;
    out.insert(QStringLiteral("uid"), uid);
    return out;
}

QVariantMap PlanService::deleteResource(int resourceUid)
{
    const int ri = project().resourceIndex(resourceUid);
    if (ri < 0) {
        return fail(QStringLiteral("Select a resource to delete."));
    }
    return apply(QStringLiteral("Delete Resource"), [&](Engine::Project &p) {
        Engine::removeResources(p, {ri});
        return QString();
    });
}

QVariantMap PlanService::setResourceField(int resourceUid, const QString &field, const QString &text)
{
    const int ri = project().resourceIndex(resourceUid);
    const QLocale locale = m_locale;
    return apply(QStringLiteral("Edit %1").arg(Engine::resourceFieldTitle(field)), [&](Engine::Project &p) {
        const Engine::EditResult r = Engine::setResourceField(p, ri, field, text, locale);
        return r.ok ? QString() : r.error;
    });
}

QVariantMap PlanService::level(int resourceUid)
{
    Engine::LevelingResult result;
    const QVariantMap r = apply(QStringLiteral("Level Resources"), [&](Engine::Project &p) {
        result = Engine::levelResources(p, resourceUid);
        return result.delayedTasks > 0 ? QString()
                                       : (result.resolved ? QStringLiteral("Nothing is overallocated.")
                                                          : QStringLiteral("Leveling could not delay any task."));
    });
    QString text = result.delayedTasks == 1 ? QStringLiteral("1 task was delayed")
                                             : QStringLiteral("%1 tasks were delayed").arg(result.delayedTasks);
    text += result.finishMoved ? QStringLiteral("; the finish moved.") : QStringLiteral(" within their slack.");
    if (!result.resolved && !result.unresolvedDays.isEmpty()) {
        text += QStringLiteral(" Still overallocated on %1.").arg(result.unresolvedDays.join(QStringLiteral(", ")));
    }
    if (r.value(QStringLiteral("ok")).toBool()) {
        emit notify(QStringLiteral("Leveled"), text, result.resolved ? QStringLiteral("info") : QStringLiteral("warning"));
    }
    QVariantMap out = r;
    out.insert(QStringLiteral("delayed"), result.delayedTasks);
    out.insert(QStringLiteral("resolved"), result.resolved);
    out.insert(QStringLiteral("finishMoved"), result.finishMoved);
    out.insert(QStringLiteral("message"), text);
    return out;
}

QVariantMap PlanService::clearLeveling()
{
    return apply(QStringLiteral("Clear Leveling"), [](Engine::Project &p) {
        return Engine::clearLeveling(p) > 0 ? QString() : QStringLiteral("No task has a leveling delay.");
    });
}

QVariantMap PlanService::nextOverallocation()
{
    const Engine::Project &p = project();
    const int current = rowOf(currentTaskId());
    const QDate after = current >= 0 ? Engine::dateOf(p.tasks.at(current).start).addDays(1) : QDate();
    QDate best;
    int bestResource = 0;
    for (const Engine::Resource &r : p.resources) {
        const QDate d = Engine::firstOverallocatedDay(p, r.uid, after);
        if (d.isValid() && (!best.isValid() || d < best)) {
            best = d;
            bestResource = r.uid;
        }
    }
    if (!best.isValid() && after.isValid()) {
        setCurrentTaskId({});
        return nextOverallocation(); // wrap around to the first one
    }
    if (!best.isValid()) {
        return fail(QStringLiteral("No resource is overallocated."));
    }
    const Engine::Minutes dayStart = Engine::toMinutes(best, 0);
    const Engine::Minutes dayEnd = Engine::toMinutes(best.addDays(1), 0);
    for (const Engine::Assignment &a : p.assignments) {
        const int row = p.taskIndex(a.taskUid);
        if (a.resourceUid != bestResource || row < 0) {
            continue;
        }
        const Engine::Task &t = p.tasks.at(row);
        if (t.start < dayEnd && t.finish > dayStart) {
            const QString id = idOfRow(row);
            setCurrentTaskId(id);
            emit revealTask(id);
            const int ri = p.resourceIndex(bestResource);
            return ok({{QStringLiteral("taskId"), id},
                       {QStringLiteral("resource"), p.resources.at(ri).name},
                       {QStringLiteral("date"), Engine::formatDate(dayStart, m_locale)}});
        }
    }
    return fail(QStringLiteral("No resource is overallocated."));
}

QString PlanService::commitResourceSheetChanges(const QVariantList &changes)
{
    const QLocale locale = m_locale;
    int created = 0;
    const QString refused = m_document->apply(QStringLiteral("Edit Resources"), [&](Engine::Project &p) {
        int newIndex = -1;
        for (const QVariant &v : changes) {
            const QVariantMap c = v.toMap();
            const QString rowId = c.value(QStringLiteral("rowId")).toString();
            const QString field = c.value(QStringLiteral("columnId")).toString();
            const QString text = c.value(QStringLiteral("after")).toString();
            int index = rowId == QLatin1String("new") ? newIndex : p.resourceIndex(rowId.mid(1).toInt());
            if (rowId == QLatin1String("new") && index < 0) {
                if (text.trimmed().isEmpty()) {
                    continue;
                }
                index = Engine::addResource(p, int(p.resources.size()),
                                            field == QLatin1String("name") ? text.trimmed() : QStringLiteral("New resource"));
                newIndex = index;
                created = p.resources.at(index).uid;
                if (field == QLatin1String("name")) {
                    continue;
                }
            }
            if (index < 0) {
                return QStringLiteral("That resource no longer exists.");
            }
            const Engine::EditResult r = Engine::setResourceField(p, index, field, text, locale);
            if (!r.ok) {
                return r.error;
            }
        }
        return QString();
    });
    if (refused.isEmpty() && created > 0) {
        setCurrentResourceUid(created);
    }
    if (!refused.isEmpty()) {
        emit notify(QStringLiteral("Not changed"), refused, QStringLiteral("warning"));
    }
    return refused;
}

} // namespace QindaOffice::Apps::Plan
