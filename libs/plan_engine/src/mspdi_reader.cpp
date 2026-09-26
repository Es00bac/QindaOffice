// SPDX-License-Identifier: GPL-3.0-or-later
// MSPDI import (mspdi.h): a streaming read into raw records, then one
// assembly step that validates, converts units (mspdi_p.h) and schedules.
#include "mspdi.h"
#include "mspdi_p.h"

#include "outline.h"
#include "plan_time.h"
#include "scheduler.h"

#include <QHash>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

using namespace Mspdi;

const DayHours kDefaultDay{{8 * 60, 12 * 60}, {13 * 60, 17 * 60}};

class Reader {
public:
    explicit Reader(const QByteArray &xml)
        : m_xml(xml)
    {
    }
    MspdiResult run(Project *out);

private:
    QString text() { return m_xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed(); }
    int integer(int fallback)
    {
        bool ok = false;
        const int v = text().toInt(&ok);
        return ok ? v : fallback;
    }
    bool boolean()
    {
        const QString t = text();
        return t == QLatin1String("1") || t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    qint64 scaled(int scale, qint64 fallback)
    {
        return scaledDecimal(text(), scale).value_or(fallback);
    }
    Minutes duration() { return parseWireDuration(text()).value_or(0); }
    void warn(const QString &message) { m_raw.warn(message); }

    void readProject();
    void readCalendar();
    DayHours readWorkingTimes();
    void readWeekDay(Calendar &calendar);
    void readException(Calendar &calendar);
    bool readTimePeriod(QDate *from, QDate *to);
    void readTask();
    void readResource();
    void readAssignment();

    QXmlStreamReader m_xml;
    Raw m_raw;
    ProjectInfo &m_info = m_raw.info;
    QList<Calendar> &m_calendars = m_raw.calendars;
    QList<RawTask> &m_tasks = m_raw.tasks;
    QList<RawResource> &m_resources = m_raw.resources;
    QList<RawAssignment> &m_assignments = m_raw.assignments;
};

void addException(Calendar &calendar, const CalendarException &ex)
{
    if (!ex.from.isValid()) {
        return;
    }
    // Project writes the legacy (DayType 0, no name) and the 2007 form of
    // the same exception; keep one, with the name.
    for (CalendarException &existing : calendar.exceptions) {
        if (existing.from == ex.from && existing.to == ex.to && existing.times == ex.times) {
            if (existing.name.isEmpty()) {
                existing.name = ex.name;
            }
            return;
        }
    }
    calendar.exceptions.append(ex);
}

MspdiResult Reader::run(Project *out)
{
    MspdiResult result;
    if (!m_xml.readNextStartElement() || m_xml.name() != u"Project") {
        result.error = m_xml.hasError()
                           ? QStringLiteral("line %1: %2").arg(m_xml.lineNumber()).arg(m_xml.errorString())
                           : QStringLiteral("This is not a Microsoft Project XML file (no <Project> root).");
        return result;
    }
    if (!m_xml.namespaceUri().isEmpty() && m_xml.namespaceUri() != kNamespace) {
        warn(QStringLiteral("Unexpected XML namespace %1; read as Microsoft Project XML.").arg(m_xml.namespaceUri()));
    }
    readProject();
    if (m_xml.hasError()) {
        result.error = QStringLiteral("line %1: %2").arg(m_xml.lineNumber()).arg(m_xml.errorString());
        return result;
    }
    assemble(m_raw, out);
    result.ok = true;
    result.warnings = m_raw.warnings;
    return result;
}

void Reader::readProject()
{
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"Name") {
            m_raw.name = text();
        } else if (n == u"Title") {
            m_info.title = text();
        } else if (n == u"Manager") {
            m_info.manager = text();
        } else if (n == u"Company") {
            m_info.company = text();
        } else if (n == u"StartDate") {
            m_info.start = parseWireDateTime(text());
        } else if (n == u"StatusDate") {
            m_info.statusDate = parseWireDateTime(text());
        } else if (n == u"CurrencyCode") {
            const QString code = text().toUpper();
            if (code.size() == 3) {
                m_info.currency = code;
            }
        } else if (n == u"MinutesPerDay") {
            m_info.minutesPerDay = std::clamp(integer(480), 1, 1440);
        } else if (n == u"MinutesPerWeek") {
            m_info.minutesPerWeek = std::clamp(integer(2400), 1, 10080);
        } else if (n == u"DaysPerMonth") {
            m_info.daysPerMonth = std::clamp(integer(20), 1, 31);
        } else if (n == u"DefaultStartTime") {
            m_info.defaultStartMinute = parseTimeOfDay(text(), false).value_or(8 * 60);
        } else if (n == u"DefaultFinishTime") {
            m_info.defaultFinishMinute = parseTimeOfDay(text(), true).value_or(17 * 60);
        } else if (n == u"CalendarUID") {
            m_info.calendarUid = integer(1);
            m_raw.hasCalendarUid = true;
        } else if (n == u"ScheduleFromStart") {
            if (!boolean()) {
                warn(QStringLiteral("The project is scheduled from its finish date; it is imported as scheduled from its start."));
            }
        } else if (n == u"Calendars") {
            while (m_xml.readNextStartElement()) {
                m_xml.name() == u"Calendar" ? readCalendar() : m_xml.skipCurrentElement();
            }
        } else if (n == u"Tasks") {
            while (m_xml.readNextStartElement()) {
                m_xml.name() == u"Task" ? readTask() : m_xml.skipCurrentElement();
            }
        } else if (n == u"Resources") {
            while (m_xml.readNextStartElement()) {
                m_xml.name() == u"Resource" ? readResource() : m_xml.skipCurrentElement();
            }
        } else if (n == u"Assignments") {
            while (m_xml.readNextStartElement()) {
                m_xml.name() == u"Assignment" ? readAssignment() : m_xml.skipCurrentElement();
            }
        } else {
            m_xml.skipCurrentElement();
        }
    }
}

void Reader::readCalendar()
{
    Calendar cal;
    bool isBase = true;
    int baseUid = -1;
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"UID") {
            cal.uid = integer(0);
        } else if (n == u"Name") {
            cal.name = text();
        } else if (n == u"IsBaseCalendar") {
            isBase = boolean();
        } else if (n == u"BaseCalendarUID") {
            baseUid = integer(-1);
        } else if (n == u"WeekDays") {
            while (m_xml.readNextStartElement()) {
                m_xml.name() == u"WeekDay" ? readWeekDay(cal) : m_xml.skipCurrentElement();
            }
        } else if (n == u"Exceptions") {
            while (m_xml.readNextStartElement()) {
                m_xml.name() == u"Exception" ? readException(cal) : m_xml.skipCurrentElement();
            }
        } else if (n == u"WorkWeeks") {
            warn(QStringLiteral("Alternative work weeks (calendar “%1”) are not supported and were skipped.").arg(cal.name));
            m_xml.skipCurrentElement();
        } else {
            m_xml.skipCurrentElement();
        }
    }
    cal.baseUid = isBase ? -1 : baseUid;
    m_calendars.append(cal);
}

DayHours Reader::readWorkingTimes()
{
    DayHours hours;
    while (m_xml.readNextStartElement()) {
        if (m_xml.name() != u"WorkingTime") {
            m_xml.skipCurrentElement();
            continue;
        }
        std::optional<int> from;
        std::optional<int> to;
        while (m_xml.readNextStartElement()) {
            if (m_xml.name() == u"FromTime") {
                from = parseTimeOfDay(text(), false);
            } else if (m_xml.name() == u"ToTime") {
                to = parseTimeOfDay(text(), true);
            } else {
                m_xml.skipCurrentElement();
            }
        }
        if (from && to && *to > *from) {
            hours.append({*from, *to});
        }
    }
    return hours;
}

bool Reader::readTimePeriod(QDate *from, QDate *to)
{
    while (m_xml.readNextStartElement()) {
        if (m_xml.name() == u"FromDate") {
            *from = dateOf(parseWireDateTime(text()));
        } else if (m_xml.name() == u"ToDate") {
            *to = dateOf(parseWireDateTime(text()));
        } else {
            m_xml.skipCurrentElement();
        }
    }
    return from->isValid();
}

void Reader::readWeekDay(Calendar &calendar)
{
    int dayType = -1;
    bool working = false;
    bool hasTimes = false;
    DayHours hours;
    QDate from;
    QDate to;
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"DayType") {
            dayType = integer(-1);
        } else if (n == u"DayWorking") {
            working = boolean();
        } else if (n == u"WorkingTimes") {
            hours = readWorkingTimes();
            hasTimes = true;
        } else if (n == u"TimePeriod") {
            readTimePeriod(&from, &to);
        } else {
            m_xml.skipCurrentElement();
        }
    }
    // AGENT-NOTE: a working day without times gets the standard 08–12,
    // 13–17 (Project always writes the times; other writers do not).
    const DayHours day = working ? (hasTimes && !hours.isEmpty() ? hours : kDefaultDay) : DayHours{};
    if (dayType == 0) {
        addException(calendar, {from, to.isValid() ? to : from, QString(), day});
    } else if (dayType >= 1 && dayType <= 7) {
        calendar.week[size_t(dayType == 1 ? 6 : dayType - 2)] = day;
    }
}

void Reader::readException(Calendar &calendar)
{
    CalendarException ex;
    int type = 1;
    bool working = false;
    bool hasTimes = false;
    DayHours hours;
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"TimePeriod") {
            readTimePeriod(&ex.from, &ex.to);
        } else if (n == u"Name") {
            ex.name = text();
        } else if (n == u"Type") {
            type = integer(1);
        } else if (n == u"DayWorking") {
            working = boolean();
        } else if (n == u"WorkingTimes") {
            hours = readWorkingTimes();
            hasTimes = true;
        } else {
            m_xml.skipCurrentElement();
        }
    }
    if (type != 1) {
        // AGENT-NOTE: only daily ranges map onto CalendarException; weekly,
        // monthly and yearly recurrences would need expanding.
        warn(QStringLiteral("The recurring calendar exception “%1” (calendar “%2”) is not supported and was skipped.")
                 .arg(ex.name, calendar.name));
        return;
    }
    ex.times = working ? (hasTimes && !hours.isEmpty() ? hours : kDefaultDay) : DayHours{};
    if (!ex.to.isValid()) {
        ex.to = ex.from;
    }
    addException(calendar, ex);
}

void Reader::readTask()
{
    RawTask raw;
    Task &t = raw.task;
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"UID") {
            t.uid = integer(-1);
        } else if (n == u"GUID") {
            t.guid = QUuid(text());
        } else if (n == u"ID") {
            raw.id = integer(-1);
        } else if (n == u"Name") {
            t.name = text();
        } else if (n == u"IsNull") {
            raw.isNull = boolean();
        } else if (n == u"OutlineLevel") {
            raw.outlineLevel = integer(1);
        } else if (n == u"Start") {
            raw.start = parseWireDateTime(text());
        } else if (n == u"Finish") {
            raw.finish = parseWireDateTime(text());
        } else if (n == u"ManualStart") {
            raw.manualStart = parseWireDateTime(text());
        } else if (n == u"ManualFinish") {
            raw.manualFinish = parseWireDateTime(text());
        } else if (n == u"Duration") {
            t.duration = duration();
        } else if (n == u"DurationFormat") {
            raw.durationFormat = integer(7);
        } else if (n == u"Estimated") {
            t.estimated = boolean();
        } else if (n == u"Milestone") {
            t.milestone = boolean();
        } else if (n == u"Manual") {
            t.manual = boolean();
        } else if (n == u"Active") {
            t.active = boolean();
        } else if (n == u"ConstraintType") {
            raw.constraintCode = integer(0);
        } else if (n == u"ConstraintDate") {
            t.constraintDate = parseWireDateTime(text());
        } else if (n == u"PercentComplete") {
            t.percentComplete = std::clamp(integer(0), 0, 100);
        } else if (n == u"ActualStart") {
            t.actualStart = parseWireDateTime(text());
        } else if (n == u"ActualFinish") {
            t.actualFinish = parseWireDateTime(text());
        } else if (n == u"Notes") {
            t.notes = m_xml.readElementText(QXmlStreamReader::SkipChildElements);
        } else if (n == u"CalendarUID") {
            raw.calendarUid = integer(-1);
        } else if (n == u"FixedCost") {
            raw.fixedCostCents = scaled(0, 0);
        } else if (n == u"PredecessorLink") {
            RawLink link;
            while (m_xml.readNextStartElement()) {
                const QStringView l = m_xml.name();
                if (l == u"PredecessorUID") {
                    link.predecessorUid = integer(-1);
                } else if (l == u"Type") {
                    link.type = integer(1);
                } else if (l == u"LinkLag") {
                    link.lagTenths = scaled(0, 0);
                } else if (l == u"LagFormat") {
                    link.lagFormat = integer(7);
                } else if (l == u"CrossProject") {
                    link.crossProject = boolean();
                } else {
                    m_xml.skipCurrentElement();
                }
            }
            raw.links.append(link);
        } else if (n == u"Baseline") {
            RawBaseline b;
            b.data.set = true;
            while (m_xml.readNextStartElement()) {
                const QStringView l = m_xml.name();
                if (l == u"Number") {
                    b.number = integer(-1);
                } else if (l == u"Start") {
                    b.data.start = parseWireDateTime(text());
                } else if (l == u"Finish") {
                    b.data.finish = parseWireDateTime(text());
                } else if (l == u"Duration") {
                    b.data.duration = duration();
                } else if (l == u"Work") {
                    b.data.work = duration();
                } else if (l == u"Cost") {
                    b.costCents = scaled(0, 0);
                } else {
                    m_xml.skipCurrentElement();
                }
            }
            raw.baselines.append(b);
        } else {
            m_xml.skipCurrentElement();
        }
    }
    m_tasks.append(raw);
}

void Reader::readResource()
{
    RawResource raw;
    Resource &r = raw.resource;
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"UID") {
            r.uid = integer(-1);
        } else if (n == u"Name") {
            r.name = text();
        } else if (n == u"Type") {
            raw.type = integer(1);
        } else if (n == u"IsNull") {
            raw.isNull = boolean();
        } else if (n == u"Initials") {
            r.initials = text();
        } else if (n == u"Group") {
            r.group = text();
        } else if (n == u"MaterialLabel") {
            r.materialLabel = text();
        } else if (n == u"EmailAddress") {
            r.email = text();
        } else if (n == u"MaxUnits") {
            r.maxUnits = int(std::clamp<qint64>(scaled(2, 100), 0, 100000));
        } else if (n == u"StandardRate") {
            raw.rateCents = scaled(2, 0);
        } else if (n == u"CostPerUse") {
            raw.costPerUseCents = scaled(0, 0);
        } else if (n == u"CalendarUID") {
            r.calendarUid = integer(-1);
        } else {
            m_xml.skipCurrentElement();
        }
    }
    m_resources.append(raw);
}

void Reader::readAssignment()
{
    RawAssignment raw;
    while (m_xml.readNextStartElement()) {
        const QStringView n = m_xml.name();
        if (n == u"UID") {
            raw.uid = integer(0);
        } else if (n == u"TaskUID") {
            raw.taskUid = integer(-1);
        } else if (n == u"ResourceUID") {
            raw.resourceUid = integer(kUnassignedResource);
        } else if (n == u"Units") {
            raw.units = std::clamp<qint64>(scaled(2, 100), 0, 100000000);
        } else {
            m_xml.skipCurrentElement();
        }
    }
    m_assignments.append(raw);
}

} // namespace

MspdiResult readMspdi(const QByteArray &xml, Project *out)
{
    if (out == nullptr) {
        return {false, QStringLiteral("No project to read into."), {}};
    }
    return Reader(xml).run(out);
}

} // namespace QindaOffice::Plan
