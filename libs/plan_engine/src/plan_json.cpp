// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_json.h"

#include "plan_time.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Plan::Json {

namespace {
const char *const kUnitCodes[] = {"m", "h", "d", "w", "mo"};
// M83: Task::type and TaskRecurrence's kinds as the .qplan writes them.
const char *const kTypeCodes[] = {"units", "duration", "work"};
const char *const kKindCodes[] = {"everyDays", "weekly", "monthlyByDay", "monthlyByPosition", "yearlyByDay",
                                  "yearlyByPosition"};
} // namespace

QJsonObject recurrenceToJson(const TaskRecurrence &r)
{
    const RecurringException &p = r.pattern;
    QJsonObject o{{QStringLiteral("kind"), QLatin1String(kKindCodes[int(p.kind)])},
                  {QStringLiteral("every"), p.every},
                  {QStringLiteral("days"), p.daysOfWeek},
                  {QStringLiteral("day"), p.day},
                  {QStringLiteral("position"), p.position},
                  {QStringLiteral("weekday"), p.weekday},
                  {QStringLiteral("month"), p.month},
                  {QStringLiteral("from"), p.from.toString(Qt::ISODate)},
                  {QStringLiteral("duration"), r.duration},
                  {QStringLiteral("startMinute"), r.startMinute}};
    if (r.count > 0) {
        o.insert(QStringLiteral("count"), r.count);
    } else {
        o.insert(QStringLiteral("to"), p.to.toString(Qt::ISODate));
    }
    if (r.workdays) {
        o.insert(QStringLiteral("workdays"), true);
    }
    return o;
}

TaskRecurrence recurrenceFromJson(const QJsonObject &o)
{
    TaskRecurrence r;
    RecurringException &p = r.pattern;
    const QString kind = o.value(QStringLiteral("kind")).toString();
    for (int k = 0; k < 6; ++k) {
        if (kind == QLatin1String(kKindCodes[k])) {
            p.kind = RecurringException::Kind(k);
        }
    }
    p.every = std::max(1, o.value(QStringLiteral("every")).toInt(1));
    p.daysOfWeek = o.value(QStringLiteral("days")).toInt() & 0x7F;
    p.day = std::clamp(o.value(QStringLiteral("day")).toInt(1), 1, 31);
    p.position = std::clamp(o.value(QStringLiteral("position")).toInt(), 0, 4);
    p.weekday = std::clamp(o.value(QStringLiteral("weekday")).toInt(1), 1, 7);
    p.month = std::clamp(o.value(QStringLiteral("month")).toInt(1), 1, 12);
    p.from = QDate::fromString(o.value(QStringLiteral("from")).toString(), Qt::ISODate);
    p.to = QDate::fromString(o.value(QStringLiteral("to")).toString(), Qt::ISODate);
    r.count = std::clamp(o.value(QStringLiteral("count")).toInt(), 0, 999);
    r.workdays = o.value(QStringLiteral("workdays")).toBool();
    r.duration = std::max<qint64>(0, integer(o, "duration", 480));
    r.startMinute = std::clamp(o.value(QStringLiteral("startMinute")).toInt(8 * 60), 0, 1439);
    return r;
}

QString clock(int minute)
{
    return QStringLiteral("%1:%2").arg(minute / 60, 2, 10, QLatin1Char('0')).arg(minute % 60, 2, 10, QLatin1Char('0'));
}

int parseClock(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return -1;
    }
    bool okH = false;
    bool okM = false;
    const int h = parts.at(0).toInt(&okH);
    const int m = parts.at(1).toInt(&okM);
    if (!okH || !okM || h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) {
        return -1;
    }
    return h * 60 + m;
}

QJsonObject extraOf(const QJsonObject &o, std::initializer_list<const char *> known)
{
    QJsonObject extra = o;
    for (const char *key : known) {
        extra.remove(QLatin1String(key));
    }
    return extra;
}

void putString(QJsonObject &o, const char *key, const QString &value)
{
    if (!value.isEmpty()) {
        o.insert(QLatin1String(key), value);
    }
}

void putDate(QJsonObject &o, const char *key, Minutes value)
{
    if (value != kNoDate) {
        o.insert(QLatin1String(key), toIso(value));
    }
}

Minutes date(const QJsonObject &o, const char *key)
{
    return fromIso(o.value(QLatin1String(key)).toString());
}

qint64 integer(const QJsonObject &o, const char *key, qint64 fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble()) {
        return fallback;
    }
    // AGENT-GUARD: JSON numbers are doubles; money and minutes stay far
    // below 2^53, and anything fractional is rounded, never truncated.
    return qint64(std::llround(v.toDouble()));
}

QJsonObject taskToJson(const Task &t)
{
    QJsonObject o = t.extra;
    o.insert(QStringLiteral("uid"), t.uid);
    o.insert(QStringLiteral("guid"), t.guid.toString(QUuid::WithoutBraces));
    o.insert(QStringLiteral("name"), t.name);
    o.insert(QStringLiteral("level"), t.outlineLevel);
    if (!t.summary) {
        o.insert(QStringLiteral("duration"), t.duration);
        o.insert(QStringLiteral("durationUnit"), QLatin1String(kUnitCodes[int(t.durationUnit)]));
    }
    if (t.estimated) {
        o.insert(QStringLiteral("estimated"), true);
    }
    if (t.milestone) {
        o.insert(QStringLiteral("milestone"), true);
    }
    if (t.manual) {
        o.insert(QStringLiteral("manual"), true);
        putDate(o, "manualStart", t.manualStart);
        putDate(o, "manualFinish", t.manualFinish);
    }
    if (!t.active) {
        o.insert(QStringLiteral("active"), false);
    }
    if (t.constraint != ConstraintType::AsSoonAsPossible) {
        o.insert(QStringLiteral("constraint"), constraintCode(t.constraint));
        putDate(o, "constraintDate", t.constraintDate);
    }
    if (t.priority != 500) {
        o.insert(QStringLiteral("priority"), t.priority); // M63
    }
    putDate(o, "deadline", t.deadline);
    if (!t.predecessors.isEmpty()) {
        QJsonArray links;
        for (const Link &l : t.predecessors) {
            QJsonObject x = l.extra;
            x.insert(QStringLiteral("uid"), l.predecessorUid);
            x.insert(QStringLiteral("type"), linkTypeCode(l.type));
            if (l.lagPercent) {
                x.insert(QStringLiteral("lagPercent"), *l.lagPercent);
            } else if (l.lag != 0) {
                x.insert(QStringLiteral("lag"), l.lag);
            }
            if (l.lagElapsed) {
                x.insert(QStringLiteral("lagElapsed"), true); // M83
            }
            links.append(x);
        }
        o.insert(QStringLiteral("predecessors"), links);
    }
    if (t.percentComplete != 0 && !t.summary) {
        o.insert(QStringLiteral("percentComplete"), t.percentComplete);
    }
    putDate(o, "actualStart", t.actualStart);
    putDate(o, "actualFinish", t.actualFinish);
    if (t.calendarUid >= 0) {
        o.insert(QStringLiteral("calendarUid"), t.calendarUid);
    }
    if (t.fixedCost != 0) {
        o.insert(QStringLiteral("fixedCost"), t.fixedCost);
    }
    // M79: Project's Leveling Delay (a legacy "levelingFloor" is only read).
    if (t.levelingDelay != 0) {
        o.insert(QStringLiteral("levelingDelay"), t.levelingDelay);
        if (!t.levelingDelayElapsed) {
            o.insert(QStringLiteral("levelingDelayWorking"), true);
        }
    }
    putDate(o, "preLeveledStart", t.preLeveledStart);
    putDate(o, "preLeveledFinish", t.preLeveledFinish);
    putString(o, "notes", t.notes);
    const QJsonArray baselines = baselinesToJson(t.baselines);
    if (!baselines.isEmpty()) {
        o.insert(QStringLiteral("baselines"), baselines);
    }
    putM83(o, t);
    return o;
}

void putM83(QJsonObject &o, const Task &t)
{
    // M83 (D-092): defaults are not written, so older files read the same.
    if (t.type != TaskType::FixedUnits) {
        o.insert(QStringLiteral("type"), QLatin1String(kTypeCodes[int(t.type)]));
    }
    if (t.effortDriven) {
        o.insert(QStringLiteral("effortDriven"), true);
    }
    if (t.unassignedWork != 0) {
        o.insert(QStringLiteral("unassignedWork"), t.unassignedWork);
    }
    if (t.durationElapsed) {
        o.insert(QStringLiteral("durationElapsed"), true);
    }
    if (!t.splits.isEmpty()) {
        QJsonArray splits;
        for (const TaskSplit &s : t.splits) {
            splits.append(QJsonObject{{QStringLiteral("at"), s.at}, {QStringLiteral("gap"), s.gap}});
        }
        o.insert(QStringLiteral("splits"), splits);
    }
    if (t.recurring) {
        o.insert(QStringLiteral("recurring"), true);
    }
    if (t.recurrence) {
        o.insert(QStringLiteral("recurrence"), recurrenceToJson(*t.recurrence));
    }
    if (!t.custom.isEmpty()) {
        QJsonObject custom;
        for (auto it = t.custom.cbegin(); it != t.custom.cend(); ++it) {
            custom.insert(it.key(), it.value());
        }
        o.insert(QStringLiteral("custom"), custom);
    }
    if (t.actualCostInput >= 0) {
        o.insert(QStringLiteral("actualCost"), t.actualCostInput);
    }
}

void takeM83(const QJsonObject &o, Task &t)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    for (int k = 0; k < 3; ++k) {
        if (type == QLatin1String(kTypeCodes[k])) {
            t.type = TaskType(k);
        }
    }
    t.effortDriven = o.value(QStringLiteral("effortDriven")).toBool() || t.type == TaskType::FixedWork;
    t.unassignedWork = std::max<qint64>(0, integer(o, "unassignedWork"));
    t.durationElapsed = o.value(QStringLiteral("durationElapsed")).toBool();
    for (const QJsonValue &v : o.value(QStringLiteral("splits")).toArray()) {
        const QJsonObject x = v.toObject();
        const TaskSplit s{integer(x, "at"), integer(x, "gap")};
        if (s.at > 0 && s.gap > 0) {
            t.splits.append(s);
        }
    }
    t.recurring = o.value(QStringLiteral("recurring")).toBool();
    if (o.value(QStringLiteral("recurrence")).isObject()) {
        t.recurrence = recurrenceFromJson(o.value(QStringLiteral("recurrence")).toObject());
    }
    const QJsonObject custom = o.value(QStringLiteral("custom")).toObject();
    for (auto it = custom.constBegin(); it != custom.constEnd(); ++it) {
        if (it.value().isString()) {
            t.custom.insert(it.key(), it.value().toString());
        }
    }
    t.actualCostInput = o.contains(QStringLiteral("actualCost")) ? std::max<qint64>(0, integer(o, "actualCost")) : -1;
}

// M83: Project ▸ Custom Fields — the definitions (custom_fields.h).
QJsonArray customFieldsToJson(const QList<CustomFieldDef> &defs)
{
    QJsonArray out;
    for (const CustomFieldDef &d : defs) {
        QJsonObject o = d.extra;
        o.insert(QStringLiteral("field"), d.field);
        putString(o, "alias", d.alias);
        putString(o, "formula", d.formula);
        if (!d.lookup.isEmpty()) {
            QJsonArray lookup;
            for (const CustomLookupEntry &e : d.lookup) {
                QJsonObject x{{QStringLiteral("value"), e.value}};
                putString(x, "description", e.description);
                lookup.append(x);
            }
            o.insert(QStringLiteral("lookup"), lookup);
        }
        if (d.restrictToList) {
            o.insert(QStringLiteral("restrict"), true);
        }
        out.append(o);
    }
    return out;
}

QList<CustomFieldDef> customFieldsFromJson(const QJsonValue &value)
{
    QList<CustomFieldDef> out;
    for (const QJsonValue &v : value.toArray()) {
        const QJsonObject o = v.toObject();
        CustomFieldDef d;
        d.field = o.value(QStringLiteral("field")).toString();
        d.alias = o.value(QStringLiteral("alias")).toString();
        d.formula = o.value(QStringLiteral("formula")).toString();
        for (const QJsonValue &e : o.value(QStringLiteral("lookup")).toArray()) {
            d.lookup.append({e.toObject().value(QStringLiteral("value")).toString(),
                             e.toObject().value(QStringLiteral("description")).toString()});
        }
        d.restrictToList = o.value(QStringLiteral("restrict")).toBool();
        d.extra = extraOf(o, {"field", "alias", "formula", "lookup", "restrict"});
        if (!d.field.isEmpty()) {
            out.append(d);
        }
    }
    return out;
}

QJsonArray baselinesToJson(const std::array<BaselineData, kBaselineCount> &list)
{
    QJsonArray out;
    for (int n = 0; n < kBaselineCount; ++n) {
        const BaselineData &b = list[size_t(n)];
        if (!b.set) {
            continue;
        }
        QJsonObject x{{QStringLiteral("number"), n},
                      {QStringLiteral("duration"), b.duration},
                      {QStringLiteral("work"), b.work},
                      {QStringLiteral("cost"), b.cost}};
        putDate(x, "start", b.start);
        putDate(x, "finish", b.finish);
        out.append(x);
    }
    return out;
}

void baselinesFromJson(const QJsonValue &value, std::array<BaselineData, kBaselineCount> *list)
{
    for (const QJsonValue &v : value.toArray()) {
        const QJsonObject x = v.toObject();
        const int n = x.value(QStringLiteral("number")).toInt(-1);
        if (n < 0 || n >= kBaselineCount) {
            continue;
        }
        BaselineData &b = (*list)[size_t(n)];
        b.set = true;
        b.start = date(x, "start");
        b.finish = date(x, "finish");
        b.duration = integer(x, "duration");
        b.work = integer(x, "work");
        b.cost = integer(x, "cost");
    }
}

Task taskFromJson(const QJsonObject &o, QStringList *warnings)
{
    Task t;
    t.uid = o.value(QStringLiteral("uid")).toInt();
    t.guid = QUuid::fromString(o.value(QStringLiteral("guid")).toString());
    t.name = o.value(QStringLiteral("name")).toString();
    t.outlineLevel = std::max(1, o.value(QStringLiteral("level")).toInt(1));
    t.duration = std::max<qint64>(0, integer(o, "duration", 480));
    const QString unit = o.value(QStringLiteral("durationUnit")).toString(QStringLiteral("d"));
    for (int u = 0; u < 5; ++u) {
        if (unit == QLatin1String(kUnitCodes[u])) {
            t.durationUnit = DurationUnit(u);
        }
    }
    t.estimated = o.value(QStringLiteral("estimated")).toBool();
    t.milestone = o.value(QStringLiteral("milestone")).toBool();
    t.manual = o.value(QStringLiteral("manual")).toBool();
    t.manualStart = date(o, "manualStart");
    t.manualFinish = date(o, "manualFinish");
    t.active = o.value(QStringLiteral("active")).toBool(true);
    if (o.contains(QStringLiteral("constraint"))) {
        const std::optional<ConstraintType> c = constraintFromCode(o.value(QStringLiteral("constraint")).toString());
        if (c) {
            t.constraint = *c;
            t.constraintDate = date(o, "constraintDate");
            if (constraintNeedsDate(t.constraint) && t.constraintDate == kNoDate) {
                warnings->append(QStringLiteral("Task “%1” had a constraint without a date; it now starts as soon as possible.")
                                     .arg(t.name));
                t.constraint = ConstraintType::AsSoonAsPossible;
            }
        } else {
            warnings->append(QStringLiteral("Task “%1” had an unknown constraint type.").arg(t.name));
        }
    }
    for (const QJsonValue &v : o.value(QStringLiteral("predecessors")).toArray()) {
        const QJsonObject x = v.toObject();
        Link l;
        l.predecessorUid = x.value(QStringLiteral("uid")).toInt();
        l.type = linkTypeFromCode(x.value(QStringLiteral("type")).toString()).value_or(LinkType::FinishToStart);
        l.lag = integer(x, "lag");
        if (x.contains(QStringLiteral("lagPercent"))) {
            l.lagPercent = std::clamp(x.value(QStringLiteral("lagPercent")).toInt(), -1000, 1000);
        }
        l.lagElapsed = x.value(QStringLiteral("lagElapsed")).toBool(); // M83
        l.extra = extraOf(x, {"uid", "type", "lag", "lagPercent", "lagElapsed"});
        t.predecessors.append(l);
    }
    t.priority = std::clamp(o.value(QStringLiteral("priority")).toInt(500), 0, 1000);
    t.deadline = date(o, "deadline");
    t.percentComplete = std::clamp(o.value(QStringLiteral("percentComplete")).toInt(), 0, 100);
    t.actualStart = date(o, "actualStart");
    t.actualFinish = date(o, "actualFinish");
    t.calendarUid = o.value(QStringLiteral("calendarUid")).toInt(-1);
    t.fixedCost = integer(o, "fixedCost");
    t.levelingFloor = date(o, "levelingFloor"); // legacy: readPlanJson converts it
    t.levelingDelay = std::max<qint64>(0, integer(o, "levelingDelay"));
    t.levelingDelayElapsed = !o.value(QStringLiteral("levelingDelayWorking")).toBool();
    t.preLeveledStart = date(o, "preLeveledStart");
    t.preLeveledFinish = date(o, "preLeveledFinish");
    t.notes = o.value(QStringLiteral("notes")).toString();
    baselinesFromJson(o.value(QStringLiteral("baselines")), &t.baselines);
    takeM83(o, t);
    t.extra = extraOf(o, {"uid", "guid", "name", "level", "duration", "durationUnit", "estimated", "milestone", "manual",
                          "manualStart", "manualFinish", "active", "constraint", "constraintDate", "priority",
                          "deadline", "predecessors",
                          "percentComplete", "actualStart", "actualFinish", "calendarUid", "fixedCost", "levelingFloor",
                          "levelingDelay", "levelingDelayWorking", "preLeveledStart", "preLeveledFinish",
                          "notes", "baselines", "type", "effortDriven", "unassignedWork", "durationElapsed", "splits",
                          "recurring", "recurrence", "custom", "actualCost"});
    return t;
}

} // namespace QindaOffice::Plan::Json
