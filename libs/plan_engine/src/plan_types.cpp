// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_types.h"

#include "plan_time.h"

#include <algorithm>

namespace QindaOffice::Plan {

int Project::taskIndex(int uid) const
{
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks.at(i).uid == uid) {
            return i;
        }
    }
    return -1;
}

int Project::taskIndexByGuid(const QUuid &guid) const
{
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks.at(i).guid == guid) {
            return i;
        }
    }
    return -1;
}

int Project::resourceIndex(int uid) const
{
    for (int i = 0; i < resources.size(); ++i) {
        if (resources.at(i).uid == uid) {
            return i;
        }
    }
    return -1;
}

int Project::resourceIndexByName(const QString &name) const
{
    const QString wanted = name.trimmed();
    for (int i = 0; i < resources.size(); ++i) {
        if (resources.at(i).name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

int Project::calendarIndex(int uid) const
{
    for (int i = 0; i < calendars.size(); ++i) {
        if (calendars.at(i).uid == uid) {
            return i;
        }
    }
    return -1;
}

int Project::assignmentIndex(int taskUid, int resourceUid) const
{
    for (int i = 0; i < assignments.size(); ++i) {
        if (assignments.at(i).taskUid == taskUid && assignments.at(i).resourceUid == resourceUid) {
            return i;
        }
    }
    return -1;
}

QList<int> Project::assignmentsOfTask(int taskUid) const
{
    QList<int> out;
    for (int i = 0; i < assignments.size(); ++i) {
        if (assignments.at(i).taskUid == taskUid) {
            out.append(i);
        }
    }
    return out;
}

namespace {
template<typename List>
int nextUid(const List &list)
{
    int top = 0;
    for (const auto &item : list) {
        top = std::max(top, item.uid);
    }
    return top + 1;
}
} // namespace

int Project::nextTaskUid() const { return nextUid(tasks); }
int Project::nextResourceUid() const { return nextUid(resources); }
int Project::nextAssignmentUid() const { return nextUid(assignments); }
int Project::nextCalendarUid() const { return nextUid(calendars); }

Calendar standardCalendar(int uid)
{
    Calendar cal;
    cal.uid = uid;
    cal.name = QStringLiteral("Standard");
    const DayHours day{{8 * 60, 12 * 60}, {13 * 60, 17 * 60}};
    for (int i = 0; i < 7; ++i) {
        cal.week[size_t(i)] = i < 5 ? day : DayHours{};
    }
    return cal;
}

Project newProject(const QDate &start, const QString &title)
{
    Project p;
    p.info.title = title;
    p.calendars.append(standardCalendar(1));
    p.info.calendarUid = 1;
    p.info.start = toMinutes(start, p.info.defaultStartMinute);
    return p;
}

QString linkTypeCode(LinkType type)
{
    switch (type) {
    case LinkType::FinishToStart:
        return QStringLiteral("FS");
    case LinkType::StartToStart:
        return QStringLiteral("SS");
    case LinkType::FinishToFinish:
        return QStringLiteral("FF");
    case LinkType::StartToFinish:
        return QStringLiteral("SF");
    }
    return QStringLiteral("FS");
}

std::optional<LinkType> linkTypeFromCode(const QString &code)
{
    const QString c = code.trimmed().toUpper();
    if (c == QLatin1String("FS")) {
        return LinkType::FinishToStart;
    }
    if (c == QLatin1String("SS")) {
        return LinkType::StartToStart;
    }
    if (c == QLatin1String("FF")) {
        return LinkType::FinishToFinish;
    }
    if (c == QLatin1String("SF")) {
        return LinkType::StartToFinish;
    }
    return std::nullopt;
}

namespace {
constexpr const char *kConstraintCodes[] = {"ASAP", "ALAP", "MSO", "MFO", "SNET", "SNLT", "FNET", "FNLT"};
}

QString constraintCode(ConstraintType type)
{
    return QString::fromLatin1(kConstraintCodes[int(type)]);
}

std::optional<ConstraintType> constraintFromCode(const QString &code)
{
    const QString c = code.trimmed().toUpper();
    for (int i = 0; i < 8; ++i) {
        if (c == QLatin1String(kConstraintCodes[i])) {
            return ConstraintType(i);
        }
    }
    return std::nullopt;
}

bool constraintNeedsDate(ConstraintType type)
{
    return type != ConstraintType::AsSoonAsPossible && type != ConstraintType::AsLateAsPossible;
}

} // namespace QindaOffice::Plan
