// SPDX-License-Identifier: GPL-3.0-or-later
// MSPDI export (mspdi.h).
//
// AGENT-GUARD: Project validates MSPDI against its schema, whose element
// lists are ORDERED sequences. Every writer below emits its elements in the
// schema's order (optional ones omitted); inserting a new element anywhere
// else makes Project refuse the whole file.
#include "mspdi.h"
#include "mspdi_p.h"

#include "plan_time.h"
#include "work_calendar.h"

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
    void text(const char *name, const QString &value) { m_xml.writeTextElement(QString::fromLatin1(name), value); }
    void number(const char *name, qint64 value) { text(name, QString::number(value)); }
    void flag(const char *name, bool value) { text(name, value ? QStringLiteral("1") : QStringLiteral("0")); }
    void date(const char *name, Minutes value)
    {
        if (value != kNoDate) {
            text(name, wireDateTime(value));
        }
    }
    void cents(const char *name, qint64 minor) { number(name, minorToCents(minor, m_decimals)); }
    void project();
    void calendar(const Calendar &cal);
    void workingTimes(const DayHours &hours);
    void summaryTask();
    void task(const Task &t);
    void resource(const Resource &r);
    void assignment(const Assignment &a);

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
    m_xml.writeStartElement(QStringLiteral("Calendars"));
    for (const Calendar &cal : m_p.calendars) {
        calendar(cal);
    }
    m_xml.writeEndElement();
    m_xml.writeStartElement(QStringLiteral("Tasks"));
    summaryTask();
    for (const Task &t : m_p.tasks) {
        task(t);
    }
    m_xml.writeEndElement();
    m_xml.writeStartElement(QStringLiteral("Resources"));
    for (const Resource &r : m_p.resources) {
        resource(r);
    }
    m_xml.writeEndElement();
    m_xml.writeStartElement(QStringLiteral("Assignments"));
    for (const Assignment &a : m_p.assignments) {
        assignment(a);
    }
    m_xml.writeEndElement();
    m_xml.writeEndElement(); // Project
    m_xml.writeEndDocument();
    return m_out;
}

void Writer::project()
{
    const ProjectInfo &info = m_p.info;
    number("SaveVersion", 14); // Project 2010 and later
    text("Name", (info.title.isEmpty() ? QStringLiteral("Project") : info.title) + QStringLiteral(".xml"));
    text("Title", info.title);
    if (!info.company.isEmpty()) {
        text("Company", info.company);
    }
    if (!info.manager.isEmpty()) {
        text("Manager", info.manager);
    }
    flag("ScheduleFromStart", true);
    date("StartDate", info.start);
    date("FinishDate", m_p.finish);
    number("FYStartDate", 1);
    number("CriticalSlackLimit", 0);
    number("CurrencyDigits", m_decimals);
    text("CurrencySymbol", info.currency == QLatin1String("USD") ? QStringLiteral("$") : info.currency);
    text("CurrencyCode", info.currency);
    number("CurrencySymbolPosition", 0);
    number("CalendarUID", info.calendarUid);
    text("DefaultStartTime", formatTimeOfDay(info.defaultStartMinute));
    text("DefaultFinishTime", formatTimeOfDay(info.defaultFinishMinute));
    number("MinutesPerDay", info.minutesPerDay);
    number("MinutesPerWeek", info.minutesPerWeek);
    number("DaysPerMonth", info.daysPerMonth);
    number("DefaultTaskType", 0); // fixed units: work follows duration × units
    number("DurationFormat", 7);
    number("WorkFormat", 2);
    flag("HonorConstraints", true);
    flag("MultipleCriticalPaths", false);
    flag("NewTasksEffortDriven", false);
    flag("NewTasksEstimated", false);
    number("WeekStartDay", 1);
    date("StatusDate", info.statusDate);
}

void Writer::workingTimes(const DayHours &hours)
{
    if (hours.isEmpty()) {
        return;
    }
    m_xml.writeStartElement(QStringLiteral("WorkingTimes"));
    for (const WorkingTime &wt : hours) {
        m_xml.writeStartElement(QStringLiteral("WorkingTime"));
        text("FromTime", formatTimeOfDay(wt.from));
        text("ToTime", formatTimeOfDay(wt.to));
        m_xml.writeEndElement();
    }
    m_xml.writeEndElement();
}

void Writer::calendar(const Calendar &cal)
{
    const bool base = cal.baseUid < 0;
    m_xml.writeStartElement(QStringLiteral("Calendar"));
    number("UID", cal.uid);
    text("Name", cal.name);
    flag("IsBaseCalendar", base);
    flag("IsBaselineCalendar", false);
    number("BaseCalendarUID", base ? -1 : cal.baseUid);
    m_xml.writeStartElement(QStringLiteral("WeekDays"));
    for (int dayType = 1; dayType <= 7; ++dayType) {
        const std::optional<DayHours> &day = cal.week[size_t(dayType == 1 ? 6 : dayType - 2)];
        if (!day && !base) {
            continue; // a derived calendar inherits this day
        }
        const DayHours hours = day.value_or(DayHours{});
        m_xml.writeStartElement(QStringLiteral("WeekDay"));
        number("DayType", dayType);
        flag("DayWorking", !hours.isEmpty());
        workingTimes(hours);
        m_xml.writeEndElement();
    }
    m_xml.writeEndElement();
    if (!cal.exceptions.isEmpty()) {
        m_xml.writeStartElement(QStringLiteral("Exceptions"));
        for (const CalendarException &ex : cal.exceptions) {
            m_xml.writeStartElement(QStringLiteral("Exception"));
            flag("EnteredByOccurrences", false);
            m_xml.writeStartElement(QStringLiteral("TimePeriod"));
            text("FromDate", wireDateTime(toMinutes(ex.from, 0)));
            text("ToDate", wireDateTime(toMinutes(ex.to.isValid() ? ex.to : ex.from, 23 * 60 + 59)));
            m_xml.writeEndElement();
            number("Occurrences", 1);
            text("Name", ex.name);
            number("Type", 1); // daily
            flag("DayWorking", !ex.times.isEmpty());
            workingTimes(ex.times);
            m_xml.writeEndElement();
        }
        m_xml.writeEndElement();
    }
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
    number("UID", 0);
    number("ID", 0);
    text("Name", m_p.info.title);
    number("Type", 1);
    flag("IsNull", false);
    text("WBS", QStringLiteral("0"));
    text("OutlineNumber", QStringLiteral("0"));
    number("OutlineLevel", 0);
    date("Start", m_p.info.start);
    date("Finish", m_p.finish);
    text("Duration", wireDuration(duration));
    number("DurationFormat", 7);
    text("Work", wireDuration(work));
    flag("Summary", true);
    cents("Cost", cost);
    if (!m_p.info.notes.isEmpty()) {
        text("Notes", m_p.info.notes);
    }
    m_xml.writeEndElement();
}

void Writer::task(const Task &t)
{
    m_xml.writeStartElement(QStringLiteral("Task"));
    number("UID", t.uid);
    if (!t.guid.isNull()) {
        text("GUID", t.guid.toString(QUuid::WithoutBraces).toUpper());
    }
    number("ID", t.id);
    text("Name", t.name);
    number("Type", 0);
    flag("IsNull", false);
    text("WBS", t.wbs);
    text("OutlineNumber", t.wbs);
    number("OutlineLevel", t.outlineLevel);
    number("Priority", 500);
    date("Start", t.start);
    date("Finish", t.finish);
    text("Duration", wireDuration(t.duration));
    if (t.manual) {
        date("ManualStart", t.manualStart);
        date("ManualFinish", t.manualFinish);
        text("ManualDuration", wireDuration(t.duration));
    }
    number("DurationFormat", durationFormat(t.durationUnit, t.estimated));
    text("Work", wireDuration(t.work));
    flag("OverAllocated", t.overallocated);
    flag("Estimated", t.estimated);
    flag("Milestone", t.milestone);
    flag("Summary", t.summary);
    flag("Critical", t.critical);
    date("EarlyStart", t.earlyStart);
    date("EarlyFinish", t.earlyFinish);
    date("LateStart", t.lateStart);
    date("LateFinish", t.lateFinish);
    number("FreeSlack", t.freeSlack * 10);
    number("TotalSlack", t.totalSlack * 10);
    cents("FixedCost", t.fixedCost);
    number("FixedCostAccrual", 3); // prorated
    number("PercentComplete", t.percentComplete);
    cents("Cost", t.cost);
    date("ActualStart", t.actualStart);
    date("ActualFinish", t.actualFinish);
    number("ConstraintType", int(t.constraint));
    number("CalendarUID", t.calendarUid);
    date("ConstraintDate", t.constraintDate);
    if (!t.notes.isEmpty()) {
        text("Notes", t.notes);
    }
    for (const Link &l : t.predecessors) {
        m_xml.writeStartElement(QStringLiteral("PredecessorLink"));
        number("PredecessorUID", l.predecessorUid);
        number("Type", wireLinkType(l.type));
        flag("CrossProject", false);
        if (l.lagPercent) {
            number("LinkLag", qint64(*l.lagPercent) * 10);
            number("LagFormat", 19);
        } else {
            number("LinkLag", l.lag * 10);
            number("LagFormat", l.lag % m_p.info.minutesPerDay == 0 ? 7 : 5);
        }
        m_xml.writeEndElement();
    }
    flag("Active", t.active);
    flag("Manual", t.manual);
    for (int n = 0; n < kBaselineCount; ++n) {
        const BaselineData &b = t.baselines[size_t(n)];
        if (!b.set) {
            continue;
        }
        m_xml.writeStartElement(QStringLiteral("Baseline"));
        number("Number", n);
        date("Start", b.start);
        date("Finish", b.finish);
        text("Duration", wireDuration(b.duration));
        number("DurationFormat", 7);
        text("Work", wireDuration(b.work));
        cents("Cost", b.cost);
        m_xml.writeEndElement();
    }
    m_xml.writeEndElement();
}

void Writer::resource(const Resource &r)
{
    const bool work = r.type == ResourceType::Work;
    m_xml.writeStartElement(QStringLiteral("Resource"));
    number("UID", r.uid);
    number("ID", m_p.resourceIndex(r.uid) + 1);
    text("Name", r.name);
    number("Type", work ? 1 : 0);
    flag("IsNull", false);
    if (!r.initials.isEmpty()) {
        text("Initials", r.initials);
    }
    if (!work && !r.materialLabel.isEmpty()) {
        text("MaterialLabel", r.materialLabel);
    }
    if (!r.group.isEmpty()) {
        text("Group", r.group);
    }
    if (!r.email.isEmpty()) {
        text("EmailAddress", r.email);
    }
    text("MaxUnits", decimalText(r.maxUnits, 2));
    flag("OverAllocated", r.overallocated);
    text("Work", work ? wireDuration(r.work) : wireDuration(0));
    // AGENT-NOTE: rates are currency units per hour on the wire (the
    // format element is display only); material rates are per unit.
    text("StandardRate", decimalText(r.standardRate, m_decimals));
    number("StandardRateFormat", 2);
    cents("Cost", r.cost);
    cents("CostPerUse", r.costPerUse);
    number("CalendarUID", r.calendarUid);
    m_xml.writeEndElement();
}

void Writer::assignment(const Assignment &a)
{
    const int ti = m_p.taskIndex(a.taskUid);
    const Task *t = ti >= 0 ? &m_p.tasks.at(ti) : nullptr;
    m_xml.writeStartElement(QStringLiteral("Assignment"));
    number("UID", a.uid);
    number("TaskUID", a.taskUid);
    number("ResourceUID", a.resourceUid);
    cents("Cost", a.cost);
    if (t != nullptr) {
        date("Finish", t->finish);
        date("Start", t->start);
    }
    text("Units", decimalText(a.units, 2));
    const int ri = m_p.resourceIndex(a.resourceUid);
    if (ri >= 0 && m_p.resources.at(ri).type == ResourceType::Work) {
        text("Work", wireDuration(a.work)); // a material's "work" is its quantity (Units)
    }
    m_xml.writeEndElement();
}

} // namespace

QByteArray writeMspdi(const Project &project)
{
    return Writer(project).run();
}

} // namespace QindaOffice::Plan
