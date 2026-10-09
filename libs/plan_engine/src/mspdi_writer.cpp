// SPDX-License-Identifier: GPL-3.0-or-later
// MSPDI export (mspdi.h).
//
// AGENT-GUARD: Project reads MSPDI as ordered sequences. Every object's
// children go through Mspdi::Children (mspdi_keep.h), which writes them in
// the order Project itself writes (mspdi_order.cpp) and slots in what the
// file had that QindaPlan does not model (M63, D-071). Write a new element
// through Children, never straight to the stream, or it lands out of order.
#include "mspdi.h"
#include "mspdi_keep.h"
#include "mspdi_p.h"

#include "plan_time.h"
#include "work_calendar.h"

#include <QSet>
#include <QXmlStreamWriter>

namespace QindaOffice::Plan {

namespace {

using namespace Mspdi;

class Writer {
public:
    explicit Writer(const Project &project)
        : m_p(project)
        , m_decimals(currencyDecimals(project.info.currency))
    {
    }
    QByteArray run();

private:
    QString cents(qint64 minor) const { return QString::number(minorToCents(minor, m_decimals)); }
    static QString flag(bool value) { return value ? QStringLiteral("1") : QStringLiteral("0"); }
    // A text child, and one only when the value is set.
    void put(Children &c, const char *name, const QString &value) { c.text(m_xml, QString::fromLatin1(name), value); }
    void put(Children &c, const char *name, qint64 value) { put(c, name, QString::number(value)); }
    void date(Children &c, const char *name, Minutes value)
    {
        if (value != kNoDate) {
            put(c, name, wireDateTime(value));
        }
    }
    // A default the file's own value replaces (QindaPlan keeps it).
    void fallback(Children &c, const char *name, const QString &value)
    {
        if (!c.kept(QString::fromLatin1(name))) {
            put(c, name, value);
        }
    }
    void collection(Children &c, const char *name, const std::function<void()> &items)
    {
        c.add(QString::fromLatin1(name), [this, name, items] {
            m_xml.writeStartElement(QString::fromLatin1(name));
            items();
            m_xml.writeEndElement();
        });
    }
    void project();
    void calendar(const Calendar &cal);
    void workingTimes(const DayHours &hours);
    void summaryTask();
    void task(const Task &t);
    void resource(const Resource &r);
    void assignment(const Assignment &a);
    // M79 (mspdi_p.h "byte-faithful baselines"): a task's or an
    // assignment's baselines — the file's element while it still says what
    // the model holds, else ours.
    void baselines(Children &c, const std::array<BaselineData, kBaselineCount> &list, const QJsonObject &extra,
                   bool assignment);
    void baselineElement(int number, const BaselineData &b, bool assignment);
    // The numbers whose baseline differs from the file's on some task
    // (the project summary task's are rewritten for those).
    [[nodiscard]] QSet<int> changedBaselines() const;

    const Project &m_p;
    int m_decimals;
    QByteArray m_out;
    QXmlStreamWriter m_xml{&m_out};
};

QByteArray Writer::run()
{
    m_xml.setAutoFormatting(true);
    m_xml.setAutoFormattingIndent(1);
    m_xml.writeStartDocument(QStringLiteral("1.0"), true);
    m_xml.writeDefaultNamespace(kNamespace);
    m_xml.writeStartElement(kNamespace, QStringLiteral("Project"));
    project();
    m_xml.writeEndElement(); // Project
    m_xml.writeEndDocument();
    return m_out;
}

void Writer::project()
{
    const ProjectInfo &info = m_p.info;
    Children c(Kind::Project, info.extra);
    put(c, "SaveVersion", 14); // Project 2010 and later
    put(c, "Name", (info.title.isEmpty() ? QStringLiteral("Project") : info.title) + QStringLiteral(".xml"));
    put(c, "Title", info.title);
    if (!info.company.isEmpty()) {
        put(c, "Company", info.company);
    }
    if (!info.manager.isEmpty()) {
        put(c, "Manager", info.manager);
    }
    put(c, "ScheduleFromStart", flag(true));
    date(c, "StartDate", info.start);
    date(c, "FinishDate", m_p.finish);
    fallback(c, "FYStartDate", QStringLiteral("1"));
    put(c, "CriticalSlackLimit", info.criticalSlackDays); // M79: modelled
    put(c, "CurrencyDigits", m_decimals);
    fallback(c, "CurrencySymbol", info.currency == QLatin1String("USD") ? QStringLiteral("$") : info.currency);
    put(c, "CurrencyCode", info.currency);
    fallback(c, "CurrencySymbolPosition", QStringLiteral("0"));
    put(c, "CalendarUID", info.calendarUid);
    put(c, "DefaultStartTime", formatTimeOfDay(info.defaultStartMinute));
    put(c, "DefaultFinishTime", formatTimeOfDay(info.defaultFinishMinute));
    put(c, "MinutesPerDay", info.minutesPerDay);
    put(c, "MinutesPerWeek", info.minutesPerWeek);
    put(c, "DaysPerMonth", info.daysPerMonth);
    fallback(c, "DefaultTaskType", QStringLiteral("0")); // fixed units: work follows duration × units
    fallback(c, "DurationFormat", QStringLiteral("7"));
    fallback(c, "WorkFormat", QStringLiteral("2"));
    fallback(c, "HonorConstraints", flag(true));
    put(c, "MultipleCriticalPaths", flag(info.multipleCriticalPaths)); // M79: modelled
    fallback(c, "NewTasksEffortDriven", flag(false));
    fallback(c, "NewTasksEstimated", flag(false));
    fallback(c, "WeekStartDay", QStringLiteral("1"));
    date(c, "StatusDate", info.statusDate);
    collection(c, "Calendars", [this] {
        for (const Calendar &cal : m_p.calendars) {
            calendar(cal);
        }
    });
    collection(c, "Tasks", [this] {
        summaryTask();
        for (const Task &t : m_p.tasks) {
            task(t);
        }
    });
    collection(c, "Resources", [this] {
        for (const Resource &r : m_p.resources) {
            resource(r);
        }
    });
    collection(c, "Assignments", [this] {
        for (const Assignment &a : m_p.assignments) {
            assignment(a);
        }
    });
    c.write(m_xml);
}

void Writer::workingTimes(const DayHours &hours)
{
    if (hours.isEmpty()) {
        return;
    }
    m_xml.writeStartElement(QStringLiteral("WorkingTimes"));
    for (const WorkingTime &wt : hours) {
        m_xml.writeStartElement(QStringLiteral("WorkingTime"));
        m_xml.writeTextElement(QStringLiteral("FromTime"), formatTimeOfDay(wt.from));
        m_xml.writeTextElement(QStringLiteral("ToTime"), formatTimeOfDay(wt.to));
        m_xml.writeEndElement();
    }
    m_xml.writeEndElement();
}

void Writer::calendar(const Calendar &cal)
{
    const bool base = cal.baseUid < 0;
    m_xml.writeStartElement(QStringLiteral("Calendar"));
    Children c(Kind::Calendar, cal.extra);
    put(c, "UID", cal.uid);
    put(c, "Name", cal.name);
    put(c, "IsBaseCalendar", flag(base));
    fallback(c, "IsBaselineCalendar", flag(false));
    put(c, "BaseCalendarUID", base ? -1 : cal.baseUid);
    c.add(QStringLiteral("WeekDays"), [this, &cal, base] {
        m_xml.writeStartElement(QStringLiteral("WeekDays"));
        for (int dayType = 1; dayType <= 7; ++dayType) {
            const std::optional<DayHours> &day = cal.week[size_t(dayType == 1 ? 6 : dayType - 2)];
            if (!day && !base) {
                continue; // a derived calendar inherits this day
            }
            const DayHours hours = day.value_or(DayHours{});
            m_xml.writeStartElement(QStringLiteral("WeekDay"));
            m_xml.writeTextElement(QStringLiteral("DayType"), QString::number(dayType));
            m_xml.writeTextElement(QStringLiteral("DayWorking"), flag(!hours.isEmpty()));
            workingTimes(hours);
            m_xml.writeEndElement();
        }
        m_xml.writeEndElement();
    });
    const QJsonArray unsupported = cal.extra.value(kExceptionsKey).toArray();
    if (!cal.exceptions.isEmpty() || !unsupported.isEmpty()) {
        c.add(QStringLiteral("Exceptions"), [this, &cal, unsupported] {
            m_xml.writeStartElement(QStringLiteral("Exceptions"));
            for (const CalendarException &ex : cal.exceptions) {
                m_xml.writeStartElement(QStringLiteral("Exception"));
                m_xml.writeTextElement(QStringLiteral("EnteredByOccurrences"), flag(false));
                m_xml.writeStartElement(QStringLiteral("TimePeriod"));
                m_xml.writeTextElement(QStringLiteral("FromDate"), wireDateTime(toMinutes(ex.from, 0)));
                m_xml.writeTextElement(QStringLiteral("ToDate"),
                                       wireDateTime(toMinutes(ex.to.isValid() ? ex.to : ex.from, 23 * 60 + 59)));
                m_xml.writeEndElement();
                m_xml.writeTextElement(QStringLiteral("Occurrences"), QStringLiteral("1"));
                m_xml.writeTextElement(QStringLiteral("Name"), ex.name);
                m_xml.writeTextElement(QStringLiteral("Type"), QStringLiteral("1")); // daily
                m_xml.writeTextElement(QStringLiteral("DayWorking"), flag(!ex.times.isEmpty()));
                workingTimes(ex.times);
                m_xml.writeEndElement();
            }
            // Recurring exceptions the file had, as it had them (M63).
            for (const QJsonValue &fragment : unsupported) {
                replay(m_xml, fragment.toString());
            }
            m_xml.writeEndElement();
        });
    }
    c.write(m_xml);
    m_xml.writeEndElement();
}

void Writer::summaryTask()
{
    // The project summary task: UID 0, outline level 0.
    const WorkCalendar cal(m_p, m_p.info.calendarUid);
    const Minutes duration = cal.workBetween(m_p.info.start, m_p.finish);
    qint64 cost = 0;
    Minutes work = 0;
    for (const Task &t : m_p.tasks) {
        if (t.parentIndex < 0 && t.effectiveActive) {
            cost += t.cost;
            work += t.work;
        }
    }
    m_xml.writeStartElement(QStringLiteral("Task"));
    Children c(Kind::Task, m_p.info.extra, kSummaryStampKey, kSummaryKey);
    put(c, "UID", 0);
    put(c, "ID", 0);
    put(c, "Name", m_p.info.title);
    fallback(c, "Type", QStringLiteral("1"));
    put(c, "IsNull", flag(false));
    put(c, "WBS", QStringLiteral("0"));
    put(c, "OutlineNumber", QStringLiteral("0"));
    put(c, "OutlineLevel", 0);
    date(c, "Start", m_p.info.start);
    date(c, "Finish", m_p.finish);
    put(c, "Duration", wireDuration(duration));
    put(c, "DurationFormat", 7);
    put(c, "Work", wireDuration(work));
    put(c, "Summary", flag(true));
    put(c, "Cost", cents(cost));
    if (!m_p.info.notes.isEmpty()) {
        put(c, "Notes", m_p.info.notes);
    }
    // M79: the summary task's baselines — the file's own while no task's
    // baseline of that number changed; a changed one is rolled up from the
    // top-level tasks (gone when none has it any more).
    const QHash<int, QString> kept = baselineFragments(m_p.info.extra, kSummaryBaselinesKey);
    const QSet<int> changed = changedBaselines();
    for (int n = 0; n < kBaselineCount; ++n) {
        BaselineData roll;
        for (const Task &t : m_p.tasks) {
            const BaselineData &b = t.baselines[size_t(n)];
            if (t.parentIndex >= 0 || !b.set) {
                continue;
            }
            roll.start = !roll.set || b.start < roll.start ? b.start : roll.start;
            roll.finish = !roll.set || b.finish > roll.finish ? b.finish : roll.finish;
            roll.work += b.work;
            roll.cost += b.cost;
            roll.set = true;
        }
        if (kept.contains(n) && !changed.contains(n)) {
            const QString xml = kept.value(n);
            c.add(QStringLiteral("Baseline"), [this, xml] { replay(m_xml, xml); });
        } else if (changed.contains(n) && roll.set) {
            roll.duration = cal.workBetween(roll.start, roll.finish);
            c.add(QStringLiteral("Baseline"), [this, n, roll] { baselineElement(n, roll, false); });
        }
    }
    c.write(m_xml);
    m_xml.writeEndElement();
}

QSet<int> Writer::changedBaselines() const
{
    QSet<int> out;
    for (const Task &t : m_p.tasks) {
        const QHash<int, QString> kept = baselineFragments(t.extra);
        for (int n = 0; n < kBaselineCount; ++n) {
            const BaselineData &b = t.baselines[size_t(n)];
            const bool same = kept.contains(n) ? baselineMatches(kept.value(n), b, m_decimals, false) : !b.set;
            if (!same) {
                out.insert(n);
            }
        }
    }
    return out;
}

void Writer::baselineElement(int number, const BaselineData &b, bool assignment)
{
    m_xml.writeStartElement(QStringLiteral("Baseline"));
    m_xml.writeTextElement(QStringLiteral("Number"), QString::number(number));
    if (b.start != kNoDate) {
        m_xml.writeTextElement(QStringLiteral("Start"), wireDateTime(b.start));
    }
    if (b.finish != kNoDate) {
        m_xml.writeTextElement(QStringLiteral("Finish"), wireDateTime(b.finish));
    }
    if (!assignment) {
        m_xml.writeTextElement(QStringLiteral("Duration"), wireDuration(b.duration));
        m_xml.writeTextElement(QStringLiteral("DurationFormat"), QStringLiteral("7"));
    }
    m_xml.writeTextElement(QStringLiteral("Work"), wireDuration(b.work));
    m_xml.writeTextElement(QStringLiteral("Cost"), cents(b.cost));
    m_xml.writeEndElement();
}

void Writer::baselines(Children &c, const std::array<BaselineData, kBaselineCount> &list, const QJsonObject &extra,
                       bool assignment)
{
    const QHash<int, QString> kept = baselineFragments(extra);
    for (int n = 0; n < kBaselineCount; ++n) {
        const BaselineData &b = list[size_t(n)];
        if (!b.set) {
            continue; // a baseline cleared in QindaPlan leaves the file
        }
        if (kept.contains(n) && baselineMatches(kept.value(n), b, m_decimals, assignment)) {
            const QString xml = kept.value(n);
            c.add(QStringLiteral("Baseline"), [this, xml] { replay(m_xml, xml); });
        } else {
            c.add(QStringLiteral("Baseline"), [this, n, b, assignment] { baselineElement(n, b, assignment); });
        }
    }
}

void Writer::task(const Task &t)
{
    m_xml.writeStartElement(QStringLiteral("Task"));
    Children c(Kind::Task, t.extra);
    put(c, "UID", t.uid);
    if (!t.guid.isNull()) {
        put(c, "GUID", t.guid.toString(QUuid::WithoutBraces).toUpper());
    }
    put(c, "ID", t.id);
    put(c, "Name", t.name);
    put(c, "Active", flag(t.active));
    put(c, "Manual", flag(t.manual));
    fallback(c, "Type", QStringLiteral("0")); // fixed units, unless the file said otherwise
    put(c, "IsNull", flag(false));
    put(c, "WBS", t.wbs);
    put(c, "OutlineNumber", t.wbs);
    put(c, "OutlineLevel", t.outlineLevel);
    put(c, "Priority", t.priority);
    date(c, "Start", t.start);
    date(c, "Finish", t.finish);
    put(c, "Duration", wireDuration(t.duration));
    if (t.manual) {
        date(c, "ManualStart", t.manualStart);
        date(c, "ManualFinish", t.manualFinish);
        put(c, "ManualDuration", wireDuration(t.duration));
    }
    put(c, "DurationFormat", durationFormat(t.durationUnit, t.estimated));
    put(c, "Work", wireDuration(t.work));
    put(c, "OverAllocated", flag(t.overallocated));
    put(c, "Estimated", flag(t.estimated));
    put(c, "Milestone", flag(t.milestone));
    put(c, "Summary", flag(t.summary));
    put(c, "Critical", flag(t.critical));
    date(c, "EarlyStart", t.earlyStart);
    date(c, "EarlyFinish", t.earlyFinish);
    date(c, "LateStart", t.lateStart);
    date(c, "LateFinish", t.lateFinish);
    put(c, "FreeSlack", t.freeSlack * 10);
    put(c, "TotalSlack", t.totalSlack * 10);
    put(c, "FixedCost", cents(t.fixedCost));
    fallback(c, "FixedCostAccrual", QStringLiteral("3")); // prorated
    put(c, "PercentComplete", t.percentComplete);
    put(c, "Cost", cents(t.cost));
    date(c, "ActualStart", t.actualStart);
    date(c, "ActualFinish", t.actualFinish);
    put(c, "ConstraintType", int(t.constraint));
    put(c, "CalendarUID", t.calendarUid);
    date(c, "ConstraintDate", t.constraintDate);
    date(c, "Deadline", t.deadline);
    // M79: Project's Leveling Delay in tenths of a minute, in the file's
    // format when it still fits (edays when elapsed, days when not).
    if (t.levelingDelay != 0 || t.extra.contains(kLevelingFormatKey)) {
        const int keptFormat = t.extra.value(kLevelingFormatKey).toInt(-1);
        const DurationCode code = durationCode(keptFormat);
        const int format = keptFormat >= 0 && code.known && !code.percent && code.elapsed == t.levelingDelayElapsed
                               ? keptFormat
                               : (t.levelingDelayElapsed ? 8 : 7);
        put(c, "LevelingDelay", t.levelingDelay * 10);
        put(c, "LevelingDelayFormat", format);
    }
    date(c, "PreLeveledStart", t.preLeveledStart);
    date(c, "PreLeveledFinish", t.preLeveledFinish);
    if (!t.notes.isEmpty()) {
        put(c, "Notes", t.notes);
    }
    for (const Link &l : t.predecessors) {
        c.add(QStringLiteral("PredecessorLink"), [this, l] {
            m_xml.writeStartElement(QStringLiteral("PredecessorLink"));
            m_xml.writeTextElement(QStringLiteral("PredecessorUID"), QString::number(l.predecessorUid));
            m_xml.writeTextElement(QStringLiteral("Type"), QString::number(wireLinkType(l.type)));
            m_xml.writeTextElement(QStringLiteral("CrossProject"), flag(false));
            if (l.lagPercent) {
                m_xml.writeTextElement(QStringLiteral("LinkLag"), QString::number(qint64(*l.lagPercent) * 10));
                m_xml.writeTextElement(QStringLiteral("LagFormat"), QStringLiteral("19"));
            } else {
                m_xml.writeTextElement(QStringLiteral("LinkLag"), QString::number(l.lag * 10));
                m_xml.writeTextElement(QStringLiteral("LagFormat"),
                                       l.lag % m_p.info.minutesPerDay == 0 ? QStringLiteral("7") : QStringLiteral("5"));
            }
            m_xml.writeEndElement();
        });
    }
    baselines(c, t.baselines, t.extra, false);
    c.write(m_xml);
    m_xml.writeEndElement();
}

void Writer::resource(const Resource &r)
{
    const bool work = r.type == ResourceType::Work;
    m_xml.writeStartElement(QStringLiteral("Resource"));
    Children c(Kind::Resource, r.extra);
    put(c, "UID", r.uid);
    put(c, "ID", m_p.resourceIndex(r.uid) + 1);
    put(c, "Name", r.name);
    put(c, "Type", work ? 1 : r.type == ResourceType::Cost ? 2 : 0); // M79: cost resources
    put(c, "IsNull", flag(false));
    if (!r.initials.isEmpty()) {
        put(c, "Initials", r.initials);
    }
    if (!work && !r.materialLabel.isEmpty()) {
        put(c, "MaterialLabel", r.materialLabel);
    }
    if (!r.group.isEmpty()) {
        put(c, "Group", r.group);
    }
    if (!r.email.isEmpty()) {
        put(c, "EmailAddress", r.email);
    }
    put(c, "MaxUnits", decimalText(r.maxUnits, 2));
    put(c, "OverAllocated", flag(r.overallocated));
    put(c, "Work", work ? wireDuration(r.work) : wireDuration(0));
    // AGENT-NOTE: rates are currency units per hour on the wire (the
    // format element is display only); material rates are per unit.
    put(c, "StandardRate", decimalText(r.standardRate, m_decimals));
    fallback(c, "StandardRateFormat", QStringLiteral("2"));
    put(c, "Cost", cents(r.cost));
    put(c, "CostPerUse", cents(r.costPerUse));
    put(c, "CalendarUID", r.calendarUid);
    if (r.type == ResourceType::Cost) {
        fallback(c, "IsCostResource", flag(true));
    }
    c.write(m_xml);
    m_xml.writeEndElement();
}

void Writer::assignment(const Assignment &a)
{
    const int ti = m_p.taskIndex(a.taskUid);
    const Task *t = ti >= 0 ? &m_p.tasks.at(ti) : nullptr;
    m_xml.writeStartElement(QStringLiteral("Assignment"));
    Children c(Kind::Assignment, a.extra);
    put(c, "UID", a.uid);
    put(c, "TaskUID", a.taskUid);
    put(c, "ResourceUID", a.resourceUid);
    put(c, "Cost", cents(a.cost));
    if (t != nullptr) {
        date(c, "Finish", t->finish);
        date(c, "Start", t->start);
    }
    put(c, "Units", decimalText(a.units, 2));
    const int ri = m_p.resourceIndex(a.resourceUid);
    if (ri >= 0 && m_p.resources.at(ri).type == ResourceType::Work) {
        put(c, "Work", wireDuration(a.work)); // a material's "work" is its quantity (Units)
    }
    baselines(c, a.baselines, a.extra, true); // M79
    c.write(m_xml);
    m_xml.writeEndElement();
}

} // namespace

QByteArray writeMspdi(const Project &project)
{
    return Writer(project).run();
}

} // namespace QindaOffice::Plan
