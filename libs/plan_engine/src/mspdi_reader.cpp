// SPDX-License-Identifier: GPL-3.0-or-later
// MSPDI import (mspdi.h): a streaming read into raw records, then one
// assembly step that validates, converts units (mspdi_p.h) and schedules.
// Every child of Project, Task, Resource and Assignment the model does not
// hold is kept verbatim (mspdi_keep.h, M63); calendars: mspdi_reader_calendar.cpp.
#include "mspdi_reader_p.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace Mspdi {

namespace {

void store(QJsonObject &extra, const QList<Kept> &kept, const QJsonObject &stamp)
{
    if (kept.isEmpty()) {
        return; // nothing kept, nothing to stamp
    }
    extra.insert(kKeptKey, keptToJson(kept));
    if (!stamp.isEmpty()) {
        extra.insert(kStampKey, stamp);
    }
}

} // namespace

QJsonObject Reader::stampOf(Kind kind, const QHash<QString, QString> &parts)
{
    QJsonObject out;
    for (const QString &e : stampElements(kind)) {
        out.insert(e, parts.value(e));
    }
    return out;
}

MspdiResult Reader::run(Project *out)
{
    MspdiResult result;
    if (!in().readNextStartElement() || in().name() != u"Project") {
        result.error = in().hasError()
                           ? QStringLiteral("line %1: %2").arg(in().lineNumber()).arg(in().errorString())
                           : QStringLiteral("This is not a Microsoft Project XML file (no <Project> root).");
        return result;
    }
    if (!in().namespaceUri().isEmpty() && in().namespaceUri() != kNamespace) {
        warn(QStringLiteral("Unexpected XML namespace %1; read as Microsoft Project XML.").arg(in().namespaceUri()));
    }
    readProject();
    if (in().hasError()) {
        result.error = QStringLiteral("line %1: %2").arg(in().lineNumber()).arg(in().errorString());
        return result;
    }
    assemble(m_raw, out);
    result.ok = true;
    result.warnings = m_raw.warnings;
    return result;
}

void Reader::readProject()
{
    QList<Kept> kept;
    QString previous;
    while (in().readNextStartElement()) {
        const QString n = in().name().toString();
        if (n == QLatin1String("Name")) {
            m_raw.name = text();
        } else if (n == QLatin1String("Title")) {
            m_info.title = text();
        } else if (n == QLatin1String("Manager")) {
            m_info.manager = text();
        } else if (n == QLatin1String("Company")) {
            m_info.company = text();
        } else if (n == QLatin1String("StartDate")) {
            m_info.start = parseWireDateTime(text());
        } else if (n == QLatin1String("FinishDate") || n == QLatin1String("SaveVersion")) {
            in().skipCurrentElement(); // ours to compute / to write
        } else if (n == QLatin1String("StatusDate")) {
            m_info.statusDate = parseWireDateTime(text());
        } else if (n == QLatin1String("CurrencyCode")) {
            const QString code = text().toUpper();
            if (code.size() == 3) {
                m_info.currency = code;
            }
        } else if (n == QLatin1String("CurrencyDigits")) {
            in().skipCurrentElement(); // follows the currency
        } else if (n == QLatin1String("MinutesPerDay")) {
            m_info.minutesPerDay = std::clamp(integer(480), 1, 1440);
        } else if (n == QLatin1String("MinutesPerWeek")) {
            m_info.minutesPerWeek = std::clamp(integer(2400), 1, 10080);
        } else if (n == QLatin1String("DaysPerMonth")) {
            m_info.daysPerMonth = std::clamp(integer(20), 1, 31);
        } else if (n == QLatin1String("DefaultStartTime")) {
            m_info.defaultStartMinute = parseTimeOfDay(text(), false).value_or(8 * 60);
        } else if (n == QLatin1String("DefaultFinishTime")) {
            m_info.defaultFinishMinute = parseTimeOfDay(text(), true).value_or(17 * 60);
        } else if (n == QLatin1String("CalendarUID")) {
            m_info.calendarUid = integer(1);
            m_raw.hasCalendarUid = true;
        } else if (n == QLatin1String("CriticalSlackLimit")) {
            m_info.criticalSlackDays = std::clamp(integer(0), 0, 999); // M79
        } else if (n == QLatin1String("MultipleCriticalPaths")) {
            m_info.multipleCriticalPaths = boolean(); // M79
        } else if (n == QLatin1String("ScheduleFromStart")) {
            if (!boolean()) {
                warn(QStringLiteral("The project is scheduled from its finish date; it is imported as scheduled from its start."));
            }
        } else if (n == QLatin1String("Calendars")) {
            while (in().readNextStartElement()) {
                in().name() == u"Calendar" ? readCalendar() : in().skipCurrentElement();
            }
        } else if (n == QLatin1String("Tasks")) {
            while (in().readNextStartElement()) {
                in().name() == u"Task" ? readTask() : in().skipCurrentElement();
            }
        } else if (n == QLatin1String("Resources")) {
            while (in().readNextStartElement()) {
                in().name() == u"Resource" ? readResource() : in().skipCurrentElement();
            }
        } else if (n == QLatin1String("Assignments")) {
            while (in().readNextStartElement()) {
                in().name() == u"Assignment" ? readAssignment() : in().skipCurrentElement();
            }
        } else {
            keep(kept, Kind::Project, previous);
        }
        previous = n;
    }
    store(m_info.extra, kept, {});
}

void Reader::readLink(RawTask &raw)
{
    RawLink link;
    while (in().readNextStartElement()) {
        const QStringView l = in().name();
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
            in().skipCurrentElement();
        }
    }
    raw.links.append(link);
}

// M79: every baseline (0–10) is modelled; the element is kept as written
// so an unchanged baseline goes back byte for byte (mspdi_baselines.cpp).
RawBaseline Reader::readBaseline()
{
    RawBaseline b = parseBaseline(capture(in()));
    if (b.number < 0 || b.number >= kBaselineCount) {
        warn(QStringLiteral("A baseline with an unknown number (%1) was dropped.").arg(b.number));
    }
    return b;
}

void Reader::readTask()
{
    RawTask raw;
    Task &t = raw.task;
    QList<Kept> kept;
    QHash<QString, QString> stamp;
    QString previous;
    while (in().readNextStartElement()) {
        const QString n = in().name().toString();
        if (stampElements(Kind::Task).contains(n) && n != QLatin1String("Duration")) {
            const QString value = text();
            stamp.insert(n, stampPart(n, value));
            if (n == QLatin1String("UID")) {
                t.uid = value.toInt();
            } else if (n == QLatin1String("Start")) {
                raw.start = parseWireDateTime(value);
            } else if (n == QLatin1String("Finish")) {
                raw.finish = parseWireDateTime(value);
            } else if (n == QLatin1String("PercentComplete")) {
                t.percentComplete = std::clamp(value.toInt(), 0, 100);
            } else if (n == QLatin1String("ActualStart")) {
                t.actualStart = parseWireDateTime(value);
            } else if (n == QLatin1String("ActualFinish")) {
                t.actualFinish = parseWireDateTime(value);
            }
        } else if (n == QLatin1String("Duration")) {
            const QString value = text();
            stamp.insert(n, stampPart(n, value));
            t.duration = parseWireDuration(value).value_or(0);
        } else if (n == QLatin1String("GUID")) {
            t.guid = QUuid(text());
        } else if (n == QLatin1String("ID")) {
            raw.id = integer(-1);
        } else if (n == QLatin1String("Name")) {
            t.name = text();
        } else if (n == QLatin1String("IsNull")) {
            raw.isNull = boolean();
        } else if (n == QLatin1String("OutlineLevel")) {
            raw.outlineLevel = integer(1);
        } else if (n == QLatin1String("Priority")) {
            t.priority = std::clamp(integer(500), 0, 1000);
        } else if (n == QLatin1String("ManualStart")) {
            raw.manualStart = parseWireDateTime(text());
        } else if (n == QLatin1String("ManualFinish")) {
            raw.manualFinish = parseWireDateTime(text());
        } else if (n == QLatin1String("DurationFormat")) {
            raw.durationFormat = integer(7);
        } else if (n == QLatin1String("Estimated")) {
            t.estimated = boolean();
        } else if (n == QLatin1String("Milestone")) {
            t.milestone = boolean();
        } else if (n == QLatin1String("Manual")) {
            t.manual = boolean();
        } else if (n == QLatin1String("Active")) {
            t.active = boolean();
        } else if (n == QLatin1String("ConstraintType")) {
            raw.constraintCode = integer(0);
        } else if (n == QLatin1String("ConstraintDate")) {
            t.constraintDate = parseWireDateTime(text());
        } else if (n == QLatin1String("Deadline")) {
            t.deadline = parseWireDateTime(text());
        } else if (n == QLatin1String("Notes")) {
            t.notes = in().readElementText(QXmlStreamReader::SkipChildElements);
        } else if (n == QLatin1String("CalendarUID")) {
            raw.calendarUid = integer(-1);
        } else if (n == QLatin1String("FixedCost")) {
            raw.fixedCostCents = scaled(0, 0);
        } else if (n == QLatin1String("PredecessorLink")) {
            readLink(raw);
        } else if (n == QLatin1String("Baseline")) {
            const RawBaseline b = readBaseline();
            if (b.number >= 0 && b.number < kBaselineCount) {
                raw.baselines.append(b);
            }
        } else if (n == QLatin1String("LevelingDelay")) {
            raw.hasLevelingDelay = true; // M79
            raw.levelingDelayTenths = scaled(0, 0);
        } else if (n == QLatin1String("LevelingDelayFormat")) {
            raw.levelingDelayFormat = integer(8);
        } else if (n == QLatin1String("PreLeveledStart")) {
            t.preLeveledStart = parseWireDateTime(text());
        } else if (n == QLatin1String("PreLeveledFinish")) {
            t.preLeveledFinish = parseWireDateTime(text());
        } else if (n == QLatin1String("WBS") || n == QLatin1String("OutlineNumber") || n == QLatin1String("Work")
                   || n == QLatin1String("ManualDuration") || n == QLatin1String("Summary")
                   || n == QLatin1String("Critical") || n == QLatin1String("OverAllocated")
                   || n == QLatin1String("EarlyStart") || n == QLatin1String("EarlyFinish")
                   || n == QLatin1String("LateStart") || n == QLatin1String("LateFinish")
                   || n == QLatin1String("FreeSlack") || n == QLatin1String("TotalSlack") || n == QLatin1String("Cost")) {
            in().skipCurrentElement(); // computed: the writer writes ours
        } else {
            keep(kept, Kind::Task, previous);
        }
        previous = n;
    }
    store(t.extra, kept, stampOf(Kind::Task, stamp));
    m_tasks.append(raw);
}

void Reader::readResource()
{
    RawResource raw;
    Resource &r = raw.resource;
    QList<Kept> kept;
    QHash<QString, QString> stamp;
    QString previous;
    while (in().readNextStartElement()) {
        const QString n = in().name().toString();
        if (n == QLatin1String("UID") || n == QLatin1String("MaxUnits") || n == QLatin1String("StandardRate")
            || n == QLatin1String("CostPerUse") || n == QLatin1String("Work")) {
            const QString value = text();
            stamp.insert(n, stampPart(n, value));
            if (n == QLatin1String("UID")) {
                bool ok = false;
                r.uid = value.toInt(&ok);
                r.uid = ok ? r.uid : -1;
            } else if (n == QLatin1String("MaxUnits")) {
                r.maxUnits = int(std::clamp<qint64>(scaledDecimal(value, 2).value_or(100), 0, 100000));
            } else if (n == QLatin1String("StandardRate")) {
                raw.rateCents = scaledDecimal(value, 2).value_or(0);
            } else if (n == QLatin1String("CostPerUse")) {
                raw.costPerUseCents = scaledDecimal(value, 0).value_or(0);
            }
        } else if (n == QLatin1String("Name")) {
            r.name = text();
        } else if (n == QLatin1String("Type")) {
            raw.type = integer(1);
        } else if (n == QLatin1String("IsNull")) {
            raw.isNull = boolean();
        } else if (n == QLatin1String("Initials")) {
            r.initials = text();
        } else if (n == QLatin1String("Group")) {
            r.group = text();
        } else if (n == QLatin1String("MaterialLabel")) {
            r.materialLabel = text();
        } else if (n == QLatin1String("EmailAddress")) {
            r.email = text();
        } else if (n == QLatin1String("CalendarUID")) {
            r.calendarUid = integer(-1);
        } else if (n == QLatin1String("ID") || n == QLatin1String("OverAllocated") || n == QLatin1String("Cost")) {
            in().skipCurrentElement(); // computed
        } else {
            keep(kept, Kind::Resource, previous);
        }
        previous = n;
    }
    store(r.extra, kept, stampOf(Kind::Resource, stamp));
    m_resources.append(raw);
}

void Reader::readAssignment()
{
    RawAssignment raw;
    QList<Kept> kept;
    QHash<QString, QString> stamp;
    QString previous;
    while (in().readNextStartElement()) {
        const QString n = in().name().toString();
        if (stampElements(Kind::Assignment).contains(n)) {
            const QString value = text();
            stamp.insert(n, stampPart(n, value));
            bool ok = false;
            if (n == QLatin1String("UID")) {
                raw.uid = value.toInt(&ok);
                raw.uid = ok ? raw.uid : 0;
            } else if (n == QLatin1String("TaskUID")) {
                raw.taskUid = value.toInt(&ok);
                raw.taskUid = ok ? raw.taskUid : -1;
            } else if (n == QLatin1String("ResourceUID")) {
                raw.resourceUid = value.toInt(&ok);
                raw.resourceUid = ok ? raw.resourceUid : kUnassignedResource;
            } else if (n == QLatin1String("Units")) {
                raw.units = std::clamp<qint64>(scaledDecimal(value, 2).value_or(100), 0, 100000000);
            }
        } else if (n == QLatin1String("Cost")) {
            // Computed — except for a cost resource, whose assignment's
            // Cost is what was entered (M79, assemble decides).
            raw.costCents = scaled(0, 0);
        } else if (n == QLatin1String("Baseline")) {
            const RawBaseline b = readBaseline(); // M79
            if (b.number >= 0 && b.number < kBaselineCount) {
                raw.baselines.append(b);
            }
        } else {
            keep(kept, Kind::Assignment, previous);
        }
        previous = n;
    }
    store(raw.extra, kept, stampOf(Kind::Assignment, stamp));
    m_assignments.append(raw);
}

} // namespace Mspdi

MspdiResult readMspdi(const QByteArray &xml, Project *out)
{
    if (out == nullptr) {
        return {false, QStringLiteral("No project to read into."), {}};
    }
    return Mspdi::Reader(xml).run(out);
}

} // namespace QindaOffice::Plan
