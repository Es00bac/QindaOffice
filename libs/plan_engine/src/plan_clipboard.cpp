// SPDX-License-Identifier: GPL-3.0-or-later
// Task copy/paste and the task sheet as CSV (plan_file.h).
#include "outline.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_json.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace QindaOffice::Plan {

namespace {

QList<int> expandedRows(const Project &project, const QList<int> &indices)
{
    QList<int> rows;
    for (int root : selectionRoots(project, indices)) {
        const int end = subtreeEnd(project, root);
        for (int i = root; i < end; ++i) {
            rows.append(i);
        }
    }
    return rows;
}

QString csvField(const QString &text)
{
    if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n'))
        || text.contains(QLatin1Char('\r'))) {
        QString quoted = text;
        quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    return text;
}

} // namespace

QByteArray copyTasks(const Project &project, const QList<int> &indices)
{
    const QList<int> rows = expandedRows(project, indices);
    if (rows.isEmpty()) {
        return {};
    }
    QSet<int> uids;
    for (int i : rows) {
        uids.insert(project.tasks.at(i).uid);
    }
    const int baseLevel = project.tasks.at(rows.first()).outlineLevel;
    QJsonArray tasks;
    for (int i : rows) {
        Task t = project.tasks.at(i);
        t.outlineLevel = t.outlineLevel - baseLevel + 1;
        t.predecessors.removeIf([&](const Link &l) { return !uids.contains(l.predecessorUid); });
        for (auto &b : t.baselines) {
            b = BaselineData{};
        }
        QJsonObject o = taskToJson(t);
        o.remove(QStringLiteral("actualStart"));
        o.remove(QStringLiteral("actualFinish"));
        o.remove(QStringLiteral("percentComplete"));
        o.remove(QStringLiteral("levelingFloor"));
        // A pasted task starts unleveled, without the source's baselines.
        for (const char *key : {"levelingDelay", "levelingDelayWorking", "preLeveledStart", "preLeveledFinish",
                                "mspdiBaselines"}) {
            o.remove(QLatin1String(key));
        }
        tasks.append(o);
    }
    QJsonArray assignments;
    for (const Assignment &a : project.assignments) {
        const int r = project.resourceIndex(a.resourceUid);
        if (!uids.contains(a.taskUid) || r < 0) {
            continue;
        }
        const Resource &res = project.resources.at(r);
        assignments.append(QJsonObject{{QStringLiteral("task"), a.taskUid},
                                       {QStringLiteral("resource"), res.name},
                                       {QStringLiteral("material"), res.type == ResourceType::Material},
                                       {QStringLiteral("costResource"), res.type == ResourceType::Cost}, // M79
                                       {QStringLiteral("cost"), a.costInput},
                                       {QStringLiteral("units"), a.units}});
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("qindaplan-tasks")},
                           {QStringLiteral("version"), kPlanFormatVersion},
                           {QStringLiteral("tasks"), tasks},
                           {QStringLiteral("assignments"), assignments}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString copyTasksText(const Project &project, const QList<int> &indices, const QLocale &locale)
{
    const QStringList fields{QStringLiteral("id"),     QStringLiteral("name"),         QStringLiteral("duration"),
                             QStringLiteral("start"),  QStringLiteral("finish"),       QStringLiteral("predecessors"),
                             QStringLiteral("resources")};
    QStringList lines;
    QStringList header;
    for (const QString &f : fields) {
        header.append(taskFieldTitle(f));
    }
    lines.append(header.join(QLatin1Char('\t')));
    for (int i : expandedRows(project, indices)) {
        QStringList cells;
        for (const QString &f : fields) {
            QString text = taskFieldText(project, i, f, locale);
            text.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (f == QLatin1String("name")) {
                text = QString(qMax(0, project.tasks.at(i).outlineLevel - 1) * 2, QLatin1Char(' ')) + text;
            }
            cells.append(text);
        }
        lines.append(cells.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QList<int> pasteTasks(Project &project, int at, const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qindaplan-tasks")) {
        return {};
    }
    QStringList warnings;
    QList<Task> tasks;
    for (const QJsonValue &v : root.value(QStringLiteral("tasks")).toArray()) {
        tasks.append(taskFromJson(v.toObject(), &warnings));
    }
    if (tasks.isEmpty()) {
        return {};
    }
    at = std::clamp(at, 0, int(project.tasks.size()));
    // The level the block lands at: that of the row it is pasted above.
    const int landing = at < project.tasks.size() ? project.tasks.at(at).outlineLevel
                                                   : (project.tasks.isEmpty() ? 1 : project.tasks.last().outlineLevel);
    QHash<int, int> uidMap;
    int nextUid = project.nextTaskUid();
    for (Task &t : tasks) {
        uidMap.insert(t.uid, nextUid);
        t.uid = nextUid++;
        t.guid = QUuid::createUuid();
        t.outlineLevel = t.outlineLevel - 1 + landing;
    }
    for (Task &t : tasks) {
        for (Link &l : t.predecessors) {
            l.predecessorUid = uidMap.value(l.predecessorUid, -1);
        }
        t.predecessors.removeIf([](const Link &l) { return l.predecessorUid < 0; });
    }
    for (int k = 0; k < tasks.size(); ++k) {
        project.tasks.insert(at + k, tasks.at(k));
    }
    for (const QJsonValue &v : root.value(QStringLiteral("assignments")).toArray()) {
        const QJsonObject o = v.toObject();
        const int taskUid = uidMap.value(o.value(QStringLiteral("task")).toInt(), -1);
        if (taskUid < 0) {
            continue;
        }
        const QString name = o.value(QStringLiteral("resource")).toString();
        int r = project.resourceIndexByName(name);
        if (r < 0) {
            r = addResource(project, int(project.resources.size()), name,
                            o.value(QStringLiteral("material")).toBool()       ? ResourceType::Material
                            : o.value(QStringLiteral("costResource")).toBool() ? ResourceType::Cost
                                                                               : ResourceType::Work);
        }
        Assignment a;
        a.uid = project.nextAssignmentUid();
        a.taskUid = taskUid;
        a.resourceUid = project.resources.at(r).uid;
        a.units = std::clamp(o.value(QStringLiteral("units")).toInt(100), 1, 1000000);
        a.costInput = qint64(o.value(QStringLiteral("cost")).toDouble());
        project.assignments.append(a);
    }
    computeStructure(project);
    QList<int> rows;
    for (int k = 0; k < tasks.size(); ++k) {
        rows.append(at + k);
    }
    return rows;
}

QByteArray taskSheetCsv(const Project &project, const QStringList &fields, const QLocale &locale)
{
    QStringList lines;
    QStringList header;
    for (const QString &f : fields) {
        header.append(csvField(taskFieldTitle(f)));
    }
    lines.append(header.join(QLatin1Char(',')));
    for (int i = 0; i < project.tasks.size(); ++i) {
        QStringList cells;
        for (const QString &f : fields) {
            QString text = f == QLatin1String("notes") ? project.tasks.at(i).notes : taskFieldText(project, i, f, locale);
            if (f == QLatin1String("name")) {
                text = QString(qMax(0, project.tasks.at(i).outlineLevel - 1) * 2, QLatin1Char(' ')) + text;
            }
            cells.append(csvField(text));
        }
        lines.append(cells.join(QLatin1Char(',')));
    }
    return (lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8();
}

} // namespace QindaOffice::Plan
