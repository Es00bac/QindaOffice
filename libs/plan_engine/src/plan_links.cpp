// SPDX-License-Identifier: GPL-3.0-or-later
// Predecessors and Resource Names as text (plan_edit.h).
#include "plan_edit.h"
#include "plan_time.h"
#include "scheduler.h"
#include "task_types.h"

#include <QRegularExpression>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

QString linkText(const Project &project, int otherIndex, const Link &link, const QLocale &locale)
{
    const QString id = QString::number(project.tasks.at(otherIndex).id);
    const QString lag = formatLag(link, durationFormatOf(project.info), locale);
    if (link.type == LinkType::FinishToStart && lag.isEmpty()) {
        return id;
    }
    return id + linkTypeCode(link.type) + lag;
}

QString quantityText(int hundredths, const QLocale &locale)
{
    QString s = locale.toString(double(hundredths) / 100.0, 'f', 2);
    const QString dot = locale.decimalPoint();
    if (s.contains(dot)) {
        while (s.endsWith(QLatin1Char('0'))) {
            s.chop(1);
        }
        if (s.endsWith(dot)) {
            s.chop(dot.size());
        }
    }
    return s;
}

// "50%", "0.5" (work) or "2.5", "2.5 sheets" (material) → units.
std::optional<int> parseUnits(const QString &text, ResourceType type)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+(?:[.,]\\d+)?)\\s*(%)?\\s*([^\\d%]*)$"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    QString number = m.captured(1);
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    const double value = number.toDouble();
    if (type == ResourceType::Work) {
        const double percent = m.captured(2).isEmpty() && value <= 10.0 ? value * 100.0 : value;
        return percent > 0 && percent <= 10000 ? std::optional<int>(qRound(percent)) : std::nullopt;
    }
    return value > 0 && value <= 1e7 ? std::optional<int>(qRound(value * 100.0)) : std::nullopt;
}

} // namespace

QString predecessorsText(const Project &project, int index, const QLocale &locale)
{
    QStringList parts;
    for (const Link &l : project.tasks.at(index).predecessors) {
        const int p = project.taskIndex(l.predecessorUid);
        if (p >= 0) {
            parts.append(linkText(project, p, l, locale));
        }
    }
    return parts.join(QStringLiteral(", "));
}

QString successorsText(const Project &project, int index, const QLocale &locale)
{
    QStringList parts;
    const int uid = project.tasks.at(index).uid;
    for (int s = 0; s < project.tasks.size(); ++s) {
        for (const Link &l : project.tasks.at(s).predecessors) {
            if (l.predecessorUid == uid) {
                parts.append(linkText(project, s, l, locale));
            }
        }
    }
    return parts.join(QStringLiteral(", "));
}

std::optional<QList<Link>> parsePredecessors(const Project &project, int index, const QString &text, QString *error,
                                             const QLocale &locale)
{
    Q_UNUSED(locale)
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    static const QRegularExpression token(
        QStringLiteral("^\\s*(\\d+)\\s*(FS|SS|FF|SF)?\\s*(?:([+-])\\s*(.+))?$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression percent(QStringLiteral("^(\\d+)\\s*%$"));
    Project copy = project;
    copy.tasks[index].predecessors.clear();
    QList<Link> out;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        if (raw.trimmed().isEmpty()) {
            continue;
        }
        const QRegularExpressionMatch m = token.match(raw);
        if (!m.hasMatch()) {
            return fail(QStringLiteral("“%1” is not a predecessor. Write a task ID with an optional type and lag, "
                                       "like 3, 4SS or 5FS+2d.")
                            .arg(raw.trimmed()));
        }
        const int id = m.captured(1).toInt();
        if (id < 1 || id > project.tasks.size()) {
            return fail(QStringLiteral("There is no task %1.").arg(id));
        }
        const int p = id - 1;
        Link link;
        link.predecessorUid = project.tasks.at(p).uid;
        link.type = m.captured(2).isEmpty() ? LinkType::FinishToStart : *linkTypeFromCode(m.captured(2));
        if (!m.captured(3).isEmpty()) {
            const bool negative = m.captured(3) == QLatin1String("-");
            const QString lag = m.captured(4).trimmed();
            const QRegularExpressionMatch pm = percent.match(lag);
            if (pm.hasMatch()) {
                link.lagPercent = pm.captured(1).toInt() * (negative ? -1 : 1);
            } else {
                const std::optional<ParsedDuration> d = parseDuration(lag, durationFormatOf(project.info));
                if (!d) {
                    return fail(QStringLiteral("“%1” is not a lag. Try +2d, -1 day or +50%.").arg(lag));
                }
                link.lag = d->minutes * (negative ? -1 : 1);
            }
        }
        // Keep what the file said about an existing link to the same task.
        for (const Link &old : project.tasks.at(index).predecessors) {
            if (old.predecessorUid == link.predecessorUid) {
                link.extra = old.extra;
            }
        }
        const QString problem = linkProblem(copy, p, index, link.type);
        if (!problem.isEmpty()) {
            return fail(QStringLiteral("Task %1: %2").arg(id).arg(problem));
        }
        copy.tasks[index].predecessors.append(link);
        out.append(link);
    }
    return out;
}

QString resourceNamesText(const Project &project, int index, const QLocale &locale)
{
    QStringList parts;
    const int uid = project.tasks.at(index).uid;
    for (const Assignment &a : project.assignments) {
        if (a.taskUid != uid) {
            continue;
        }
        const int r = project.resourceIndex(a.resourceUid);
        if (r < 0) {
            continue;
        }
        const Resource &res = project.resources.at(r);
        if (res.type == ResourceType::Work) {
            parts.append(a.units == 100 ? res.name
                                        : QStringLiteral("%1[%2%]").arg(res.name, locale.toString(a.units)));
        } else if (res.type == ResourceType::Cost) {
            // M79: a cost resource with what the assignment costs.
            parts.append(QStringLiteral("%1[%2]").arg(res.name, formatMoney(a.costInput, project.info.currency, locale)));
        } else {
            const QString quantity = quantityText(a.units, locale);
            parts.append(QStringLiteral("%1[%2]").arg(
                res.name, res.materialLabel.isEmpty() ? quantity : quantity + QLatin1Char(' ') + res.materialLabel));
        }
    }
    return parts.join(QStringLiteral(", "));
}

EditResult setResourceNames(Project &project, int index, const QString &text, const QLocale &locale)
{
    if (index < 0 || index >= project.tasks.size()) {
        return EditResult::fail(QStringLiteral("There is no such task."));
    }
    if (project.tasks.at(index).summary) {
        return EditResult::fail(QStringLiteral("Assign resources to subtasks, not to their summary task."));
    }
    static const QRegularExpression token(QStringLiteral("^\\s*([^\\[\\]]+?)\\s*(?:\\[([^\\]]*)\\])?\\s*$"));
    Project copy = project;
    struct Wanted {
        int resourceUid;
        int units;
        std::optional<qint64> cost; // M79: a cost resource's amount, when typed
    };
    QList<Wanted> wanted;
    for (const QString &raw : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (raw.trimmed().isEmpty()) {
            continue;
        }
        const QRegularExpressionMatch m = token.match(raw);
        if (!m.hasMatch()) {
            return EditResult::fail(QStringLiteral("“%1” is not a resource. Write a name with optional units, like Ana[50%].")
                                        .arg(raw.trimmed()));
        }
        const QString name = m.captured(1).trimmed();
        int r = copy.resourceIndexByName(name);
        if (r < 0) {
            r = addResource(copy, int(copy.resources.size()), name, ResourceType::Work);
        }
        const Resource &res = copy.resources.at(r);
        int units = 100; // full-time, or one unit of material
        std::optional<qint64> cost;
        if (res.type == ResourceType::Cost && !m.captured(2).trimmed().isEmpty()) {
            const std::optional<qint64> amount = parseMoney(m.captured(2), copy.info.currency, locale);
            if (!amount || *amount < 0) {
                return EditResult::fail(QStringLiteral("“%1” is not an amount for %2.").arg(m.captured(2).trimmed(), name));
            }
            cost = *amount;
        } else if (!m.captured(2).trimmed().isEmpty()) {
            const std::optional<int> parsed = parseUnits(m.captured(2), res.type);
            if (!parsed) {
                return EditResult::fail(QStringLiteral("“%1” is not an amount of %2.").arg(m.captured(2).trimmed(), name));
            }
            units = *parsed;
        }
        for (const Wanted &w : std::as_const(wanted)) {
            if (w.resourceUid == res.uid) {
                return EditResult::fail(QStringLiteral("%1 is listed twice.").arg(name));
            }
        }
        wanted.append({res.uid, units, cost});
    }
    // M83 (task_types.h): units changed on kept assignments first, then the
    // resources added and removed in one change, so an effort-driven task
    // shares its work as Project does.
    const int taskUid = copy.tasks.at(index).uid;
    QList<int> removed;
    for (const Assignment &a : std::as_const(copy.assignments)) {
        const bool kept = std::any_of(wanted.cbegin(), wanted.cend(),
                                      [&](const Wanted &w) { return w.resourceUid == a.resourceUid; });
        if (a.taskUid == taskUid && !kept) {
            removed.append(a.resourceUid);
        }
    }
    QList<ResourceRequest> added;
    for (const Wanted &w : std::as_const(wanted)) {
        const int existing = copy.assignmentIndex(taskUid, w.resourceUid);
        if (existing < 0) {
            added.append({w.resourceUid, w.units});
            continue;
        }
        if (copy.assignments.at(existing).units != w.units) {
            const EditResult r = setAssignmentUnits(copy, existing, w.units);
            if (!r.ok) {
                return r;
            }
        }
    }
    const EditResult r = changeAssignments(copy, index, added, removed);
    if (!r.ok) {
        return r;
    }
    for (const Wanted &w : std::as_const(wanted)) {
        if (w.cost) {
            copy.assignments[copy.assignmentIndex(taskUid, w.resourceUid)].costInput = *w.cost;
        }
    }
    project = copy;
    return {};
}

} // namespace QindaOffice::Plan
