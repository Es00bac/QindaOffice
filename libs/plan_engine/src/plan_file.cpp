// SPDX-License-Identifier: GPL-3.0-or-later
// The .qplan JSON format (plan_file.h).
#include "plan_file.h"

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
    o.insert(QStringLiteral("type"), r.type == ResourceType::Work ? QStringLiteral("work") : QStringLiteral("material"));
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
    r.type = o.value(QStringLiteral("type")).toString() == QLatin1String("material") ? ResourceType::Material
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
    info.extra = extraOf(o, {"title", "manager", "company", "notes", "start", "statusDate", "currency", "calendarUid",
                             "minutesPerDay", "minutesPerWeek", "daysPerMonth", "defaultStart", "defaultFinish"});
    return info;
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
        assignments.append(o);
    }
    root.insert(QStringLiteral("assignments"), assignments);
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
        a.extra = extraOf(o, {"uid", "task", "resource", "units"});
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
    p.view = root.value(QStringLiteral("view")).toObject();
    p.extra = extraOf(root, {"format", "version", "project", "calendars", "tasks", "resources", "assignments", "view"});
    if (p.info.start == kNoDate) {
        p.info.start = toMinutes(QDate::currentDate(), p.info.defaultStartMinute);
        result.warnings.append(QStringLiteral("The project had no start date; today is used."));
    }
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
