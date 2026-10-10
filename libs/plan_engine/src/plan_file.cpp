// SPDX-License-Identifier: GPL-3.0-or-later
// The .qplan JSON format (plan_file.h).
#include "plan_file.h"

#include "mspdi.h"
#include "plan_json.h"
#include "plan_time.h"
#include "scheduler.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace QindaOffice::Plan {

using namespace Json;

namespace {

const char *const kDayKeys[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

QJsonArray hoursToJson(const DayHours &hours)
{
    QJsonArray out;
    for (const WorkingTime &wt : hours) {
        out.append(QJsonArray{clock(wt.from), clock(wt.to)});
    }
    return out;
}

DayHours hoursFromJson(const QJsonValue &value, QStringList *warnings)
{
    DayHours out;
    for (const QJsonValue &v : value.toArray()) {
        const QJsonArray pair = v.toArray();
        const int from = parseClock(pair.at(0).toString());
        const int to = parseClock(pair.at(1).toString());
        if (from < 0 || to <= from) {
            warnings->append(QStringLiteral("A working time %1–%2 is not valid and was dropped.")
                                 .arg(pair.at(0).toString(), pair.at(1).toString()));
            continue;
        }
        out.append({from, to});
    }
    return out;
}

QJsonObject calendarToJson(const Calendar &c)
{
    QJsonObject o = c.extra;
    o.insert(QStringLiteral("uid"), c.uid);
    o.insert(QStringLiteral("name"), c.name);
    if (c.baseUid >= 0) {
        o.insert(QStringLiteral("baseUid"), c.baseUid);
    }
    QJsonObject week;
    for (int d = 0; d < 7; ++d) {
        if (c.week[size_t(d)].has_value()) {
            week.insert(QLatin1String(kDayKeys[d]), hoursToJson(*c.week[size_t(d)]));
        }
    }
    o.insert(QStringLiteral("week"), week);
    QJsonArray exceptions;
    for (const CalendarException &e : c.exceptions) {
        QJsonObject x{{QStringLiteral("from"), e.from.toString(Qt::ISODate)},
                      {QStringLiteral("to"), e.to.toString(Qt::ISODate)},
                      {QStringLiteral("name"), e.name}};
        if (!e.times.isEmpty()) {
            x.insert(QStringLiteral("times"), hoursToJson(e.times));
        }
        exceptions.append(x);
    }
    o.insert(QStringLiteral("exceptions"), exceptions);
    return o;
}

Calendar calendarFromJson(const QJsonObject &o, QStringList *warnings)
{
    Calendar c;
    c.uid = o.value(QStringLiteral("uid")).toInt();
    c.name = o.value(QStringLiteral("name")).toString();
    c.baseUid = o.value(QStringLiteral("baseUid")).toInt(-1);
    const QJsonObject week = o.value(QStringLiteral("week")).toObject();
    for (int d = 0; d < 7; ++d) {
        const QString key = QLatin1String(kDayKeys[d]);
        if (week.contains(key)) {
            c.week[size_t(d)] = hoursFromJson(week.value(key), warnings);
        }
    }
    for (const QJsonValue &v : o.value(QStringLiteral("exceptions")).toArray()) {
        const QJsonObject x = v.toObject();
        CalendarException e;
        e.from = QDate::fromString(x.value(QStringLiteral("from")).toString(), Qt::ISODate);
        e.to = QDate::fromString(x.value(QStringLiteral("to")).toString(), Qt::ISODate);
        e.name = x.value(QStringLiteral("name")).toString();
        e.times = hoursFromJson(x.value(QStringLiteral("times")), warnings);
        if (!e.from.isValid()) {
            warnings->append(QStringLiteral("An exception of calendar “%1” has no date and was dropped.").arg(c.name));
            continue;
        }
        if (!e.to.isValid() || e.to < e.from) {
            e.to = e.from;
        }
        c.exceptions.append(e);
    }
    c.extra = extraOf(o, {"uid", "name", "baseUid", "week", "exceptions"});
    return c;
}

QJsonObject resourceToJson(const Resource &r)
{
    QJsonObject o = r.extra;
    o.insert(QStringLiteral("uid"), r.uid);
    o.insert(QStringLiteral("name"), r.name);
    o.insert(QStringLiteral("type"), r.type == ResourceType::Work       ? QStringLiteral("work")
                                     : r.type == ResourceType::Cost ? QStringLiteral("cost") // M79
                                                                    : QStringLiteral("material"));
    putString(o, "initials", r.initials);
    putString(o, "group", r.group);
    putString(o, "email", r.email);
    putString(o, "materialLabel", r.materialLabel);
    o.insert(QStringLiteral("maxUnits"), r.maxUnits);
    o.insert(QStringLiteral("standardRate"), r.standardRate);
    if (r.costPerUse != 0) {
        o.insert(QStringLiteral("costPerUse"), r.costPerUse);
    }
    if (r.calendarUid >= 0) {
        o.insert(QStringLiteral("calendarUid"), r.calendarUid);
    }
    return o;
}

Resource resourceFromJson(const QJsonObject &o)
{
    Resource r;
    r.uid = o.value(QStringLiteral("uid")).toInt();
    r.name = o.value(QStringLiteral("name")).toString();
    const QString type = o.value(QStringLiteral("type")).toString();
    r.type = type == QLatin1String("material") ? ResourceType::Material
             : type == QLatin1String("cost")   ? ResourceType::Cost
                                               : ResourceType::Work;
    r.initials = o.value(QStringLiteral("initials")).toString();
    r.group = o.value(QStringLiteral("group")).toString();
    r.email = o.value(QStringLiteral("email")).toString();
    r.materialLabel = o.value(QStringLiteral("materialLabel")).toString();
    r.maxUnits = std::clamp(o.value(QStringLiteral("maxUnits")).toInt(100), 1, 10000);
    r.standardRate = std::max<qint64>(0, integer(o, "standardRate"));
    r.costPerUse = std::max<qint64>(0, integer(o, "costPerUse"));
    r.calendarUid = o.value(QStringLiteral("calendarUid")).toInt(-1);
    r.extra = extraOf(o, {"uid", "name", "type", "initials", "group", "email", "materialLabel", "maxUnits",
                          "standardRate", "costPerUse", "calendarUid"});
    return r;
}

QJsonObject infoToJson(const ProjectInfo &info)
{
    QJsonObject o = info.extra;
    putString(o, "title", info.title);
    putString(o, "manager", info.manager);
    putString(o, "company", info.company);
    putString(o, "notes", info.notes);
    putDate(o, "start", info.start);
    putDate(o, "statusDate", info.statusDate);
    o.insert(QStringLiteral("currency"), info.currency);
    o.insert(QStringLiteral("calendarUid"), info.calendarUid);
    o.insert(QStringLiteral("minutesPerDay"), info.minutesPerDay);
    o.insert(QStringLiteral("minutesPerWeek"), info.minutesPerWeek);
    o.insert(QStringLiteral("daysPerMonth"), info.daysPerMonth);
    o.insert(QStringLiteral("defaultStart"), clock(info.defaultStartMinute));
    o.insert(QStringLiteral("defaultFinish"), clock(info.defaultFinishMinute));
    // M79: critical path and leveling options (defaults are not written).
    if (info.criticalSlackDays != 0) {
        o.insert(QStringLiteral("criticalSlackDays"), info.criticalSlackDays);
    }
    if (info.multipleCriticalPaths) {
        o.insert(QStringLiteral("multipleCriticalPaths"), true);
    }
    if (info.levelingOrder != LevelingOrder::Standard) {
        o.insert(QStringLiteral("levelingOrder"), info.levelingOrder == LevelingOrder::IdOnly ? QStringLiteral("id")
                                                                                              : QStringLiteral("priority"));
    }
    if (info.levelWithinSlack) {
        o.insert(QStringLiteral("levelWithinSlack"), true);
    }
    if (!info.levelClearFirst) {
        o.insert(QStringLiteral("levelClearFirst"), false);
    }
    // M83: Project Options' new-task type and effort driving, typed actual costs.
    if (info.defaultTaskType != TaskType::FixedUnits) {
        o.insert(QStringLiteral("defaultTaskType"), int(info.defaultTaskType));
    }
    if (info.newTasksEffortDriven) {
        o.insert(QStringLiteral("newTasksEffortDriven"), true);
    }
    if (info.editableActualCosts) {
        o.insert(QStringLiteral("editableActualCosts"), true);
    }
    return o;
}

ProjectInfo infoFromJson(const QJsonObject &o)
{
    ProjectInfo info;
    info.title = o.value(QStringLiteral("title")).toString();
    info.manager = o.value(QStringLiteral("manager")).toString();
    info.company = o.value(QStringLiteral("company")).toString();
    info.notes = o.value(QStringLiteral("notes")).toString();
    info.start = date(o, "start");
    info.statusDate = date(o, "statusDate");
    info.currency = o.value(QStringLiteral("currency")).toString(QStringLiteral("USD")).left(3).toUpper();
    info.calendarUid = o.value(QStringLiteral("calendarUid")).toInt(1);
    info.minutesPerDay = std::clamp(o.value(QStringLiteral("minutesPerDay")).toInt(480), 1, 1440);
    info.minutesPerWeek = std::clamp(o.value(QStringLiteral("minutesPerWeek")).toInt(2400), 1, 10080);
    info.daysPerMonth = std::clamp(o.value(QStringLiteral("daysPerMonth")).toInt(20), 1, 31);
    const int ds = parseClock(o.value(QStringLiteral("defaultStart")).toString());
    const int df = parseClock(o.value(QStringLiteral("defaultFinish")).toString());
    info.defaultStartMinute = ds >= 0 ? ds : 480;
    info.defaultFinishMinute = df > 0 ? df : 1020;
    info.criticalSlackDays = std::clamp(o.value(QStringLiteral("criticalSlackDays")).toInt(0), 0, 999);
    info.multipleCriticalPaths = o.value(QStringLiteral("multipleCriticalPaths")).toBool();
    const QString order = o.value(QStringLiteral("levelingOrder")).toString();
    info.levelingOrder = order == QLatin1String("id")         ? LevelingOrder::IdOnly
                         : order == QLatin1String("priority") ? LevelingOrder::PriorityStandard
                                                              : LevelingOrder::Standard;
    info.levelWithinSlack = o.value(QStringLiteral("levelWithinSlack")).toBool();
    info.levelClearFirst = o.value(QStringLiteral("levelClearFirst")).toBool(true);
    info.defaultTaskType = TaskType(std::clamp(o.value(QStringLiteral("defaultTaskType")).toInt(), 0, 2));
    info.newTasksEffortDriven = o.value(QStringLiteral("newTasksEffortDriven")).toBool();
    info.editableActualCosts = o.value(QStringLiteral("editableActualCosts")).toBool();
    info.extra = extraOf(o, {"title", "manager", "company", "notes", "start", "statusDate", "currency", "calendarUid",
                             "minutesPerDay", "minutesPerWeek", "daysPerMonth", "defaultStart", "defaultFinish",
                             "criticalSlackDays", "multipleCriticalPaths", "levelingOrder", "levelWithinSlack",
                             "levelClearFirst", "defaultTaskType", "newTasksEffortDriven", "editableActualCosts"});
    return info;
}

// AGENT-NOTE (M79): QindaPlan up to M78 stored leveling as an absolute
// "starts no earlier" floor. One schedule with the floors applied gives each
// task's unleveled early start (with its predecessors already leveled), so
// floor − that start is exactly the relative Leveling Delay that yields the
// same dates; the floors are then dropped.
void convertLegacyFloors(Project &p)
{
    bool any = false;
    for (const Task &t : std::as_const(p.tasks)) {
        any = any || t.levelingFloor != kNoDate;
    }
    if (!any) {
        return;
    }
    schedule(p);
    for (Task &t : p.tasks) {
        if (t.levelingFloor == kNoDate) {
            continue;
        }
        if (t.unleveledStart != kNoDate && t.levelingFloor > t.unleveledStart && t.levelingDelay == 0) {
            t.levelingDelay = t.levelingFloor - t.unleveledStart;
            t.levelingDelayElapsed = true;
        }
        t.levelingFloor = kNoDate;
    }
}

} // namespace

QByteArray writePlanJson(const Project &project)
{
    QJsonObject root = project.extra;
    root.insert(QStringLiteral("format"), QStringLiteral("qindaplan"));
    root.insert(QStringLiteral("version"), kPlanFormatVersion);
    root.insert(QStringLiteral("project"), infoToJson(project.info));
    QJsonArray calendars;
    for (const Calendar &c : project.calendars) {
        calendars.append(calendarToJson(c));
    }
    root.insert(QStringLiteral("calendars"), calendars);
    QJsonArray tasks;
    for (const Task &t : project.tasks) {
        tasks.append(taskToJson(t));
    }
    root.insert(QStringLiteral("tasks"), tasks);
    QJsonArray resources;
    for (const Resource &r : project.resources) {
        resources.append(resourceToJson(r));
    }
    root.insert(QStringLiteral("resources"), resources);
    QJsonArray assignments;
    for (const Assignment &a : project.assignments) {
        QJsonObject o = a.extra;
        o.insert(QStringLiteral("uid"), a.uid);
        o.insert(QStringLiteral("task"), a.taskUid);
        o.insert(QStringLiteral("resource"), a.resourceUid);
        o.insert(QStringLiteral("units"), a.units);
        if (a.costInput != 0) {
            o.insert(QStringLiteral("cost"), a.costInput); // M79: a cost resource's amount
        }
        if (a.workInput >= 0) {
            o.insert(QStringLiteral("work"), a.workInput); // M83: set by a task-type rule
        }
        if (a.workSpan >= 0) {
            o.insert(QStringLiteral("workSpan"), a.workSpan);
        }
        const QJsonArray baselines = baselinesToJson(a.baselines);
        if (!baselines.isEmpty()) {
            o.insert(QStringLiteral("baselines"), baselines); // M79
        }
        assignments.append(o);
    }
    root.insert(QStringLiteral("assignments"), assignments);
    if (!project.customFields.isEmpty()) {
        root.insert(QStringLiteral("customFields"), customFieldsToJson(project.customFields)); // M83
    }
    root.insert(QStringLiteral("view"), project.view);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

LoadResult readPlanJson(const QByteArray &json, Project *out)
{
    LoadResult result;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        result.error = QStringLiteral("This is not a QindaPlan project (%1 at byte %2).")
                           .arg(parseError.errorString())
                           .arg(parseError.offset);
        return result;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qindaplan")) {
        result.error = QStringLiteral("This JSON file is not a QindaPlan project.");
        return result;
    }
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version > kPlanFormatVersion) {
        result.error = QStringLiteral("This project was saved by a newer QindaPlan (file format %1; this version reads %2).")
                           .arg(version)
                           .arg(kPlanFormatVersion);
        return result;
    }
    Project p;
    p.info = infoFromJson(root.value(QStringLiteral("project")).toObject());
    QSet<int> seen;
    for (const QJsonValue &v : root.value(QStringLiteral("calendars")).toArray()) {
        Calendar c = calendarFromJson(v.toObject(), &result.warnings);
        if (c.uid <= 0 || seen.contains(c.uid)) {
            result.warnings.append(QStringLiteral("Calendar “%1” had a duplicate id and was renumbered.").arg(c.name));
            c.uid = p.nextCalendarUid();
        }
        seen.insert(c.uid);
        p.calendars.append(c);
    }
    if (p.calendars.isEmpty() || p.calendarIndex(p.info.calendarUid) < 0) {
        const int uid = p.calendars.isEmpty() ? 1 : p.nextCalendarUid();
        p.calendars.prepend(standardCalendar(uid));
        p.info.calendarUid = uid;
        result.warnings.append(QStringLiteral("The project calendar was missing; the standard calendar is used."));
    }
    seen.clear();
    QSet<QUuid> guids;
    for (const QJsonValue &v : root.value(QStringLiteral("tasks")).toArray()) {
        Task t = taskFromJson(v.toObject(), &result.warnings);
        if (t.uid <= 0 || seen.contains(t.uid)) {
            result.warnings.append(QStringLiteral("Task “%1” had a duplicate id and was renumbered.").arg(t.name));
            t.uid = p.nextTaskUid();
        }
        if (t.guid.isNull() || guids.contains(t.guid)) {
            t.guid = QUuid::createUuid();
        }
        seen.insert(t.uid);
        guids.insert(t.guid);
        p.tasks.append(t);
    }
    seen.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("resources")).toArray()) {
        Resource r = resourceFromJson(v.toObject());
        if (r.uid <= 0 || seen.contains(r.uid)) {
            r.uid = p.nextResourceUid();
        }
        seen.insert(r.uid);
        p.resources.append(r);
    }
    seen.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("assignments")).toArray()) {
        const QJsonObject o = v.toObject();
        Assignment a;
        a.uid = o.value(QStringLiteral("uid")).toInt();
        a.taskUid = o.value(QStringLiteral("task")).toInt();
        a.resourceUid = o.value(QStringLiteral("resource")).toInt();
        a.units = std::clamp(o.value(QStringLiteral("units")).toInt(100), 1, 1000000);
        baselinesFromJson(o.value(QStringLiteral("baselines")), &a.baselines);
        a.costInput = integer(o, "cost");
        a.workInput = o.contains(QStringLiteral("work")) ? std::max<qint64>(0, integer(o, "work")) : kWorkFollowsDuration;
        a.workSpan = o.contains(QStringLiteral("workSpan")) ? std::max<qint64>(0, integer(o, "workSpan")) : -1;
        a.extra = extraOf(o, {"uid", "task", "resource", "units", "baselines", "cost", "work", "workSpan"});
        if (p.taskIndex(a.taskUid) < 0 || p.resourceIndex(a.resourceUid) < 0 || p.assignmentIndex(a.taskUid, a.resourceUid) >= 0) {
            result.warnings.append(QStringLiteral("An assignment to a missing task or resource was dropped."));
            continue;
        }
        if (a.uid <= 0 || seen.contains(a.uid)) {
            a.uid = p.nextAssignmentUid();
        }
        seen.insert(a.uid);
        p.assignments.append(a);
    }
    p.customFields = customFieldsFromJson(root.value(QStringLiteral("customFields"))); // M83
    p.view = root.value(QStringLiteral("view")).toObject();
    p.extra = extraOf(root, {"format", "version", "project", "calendars", "tasks", "resources", "assignments", "view",
                             "customFields"});
    if (p.info.start == kNoDate) {
        p.info.start = toMinutes(QDate::currentDate(), p.info.defaultStartMinute);
        result.warnings.append(QStringLiteral("The project had no start date; today is used."));
    }
    // M79: what earlier versions kept verbatim but QindaPlan now models
    // (baselines 3–10, leveling delays, the critical-path options from
    // Microsoft Project XML), and leveling floors.
    adoptKeptElements(p);
    convertLegacyFloors(p);
    schedule(p);
    *out = p;
    result.ok = true;
    return result;
}

bool savePlanFile(const QString &path, const Project &project, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray bytes = writePlanJson(project);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

LoadResult loadPlanFile(const QString &path, Project *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LoadResult r;
        r.error = file.errorString();
        return r;
    }
    return readPlanJson(file.readAll(), out);
}

} // namespace QindaOffice::Plan
