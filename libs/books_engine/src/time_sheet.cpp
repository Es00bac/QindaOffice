// SPDX-License-Identifier: GPL-3.0-or-later
// The weekly timesheet (time_tracking.h): one person's week as rows of
// seven day cells, read from and written back to time entries.
#include "time_tracking.h"

#include "company_file.h"

#include <algorithm>

namespace QindaOffice::Books {

namespace {

bool sameRow(const TimesheetRow &row, const TimeEntry &e)
{
    return row.customerId == e.customerId && row.itemId == e.itemId && row.billable == e.billable
        && row.notes == e.notes && row.classId == e.classId;
}

bool sameEntry(const TimeEntry &a, const TimeEntry &b)
{
    return a.date == b.date && a.worker == b.worker && a.customerId == b.customerId && a.itemId == b.itemId
        && a.minutes == b.minutes && a.billable == b.billable && a.notes == b.notes && a.classId == b.classId;
}

} // namespace

QList<TimesheetRow> TimeTracking::week(const Worker &worker, const QDate &weekStart) const
{
    QList<TimesheetRow> rows;
    if (!worker.isValid() || !weekStart.isValid()) {
        return rows;
    }
    TimeFilter filter;
    filter.worker = worker;
    filter.from = weekStart;
    filter.to = weekStart.addDays(6);
    for (const TimeEntry &e : entries(filter)) {
        const int day = int(weekStart.daysTo(e.date));
        // AGENT-GUARD: the first row with the same purpose and a free cell
        // that day; never two entries in one cell (saving would merge them).
        auto row = std::find_if(rows.begin(), rows.end(), [&e, day](const TimesheetRow &r) {
            return sameRow(r, e) && r.days.at(day).entryId == 0;
        });
        if (row == rows.end()) {
            TimesheetRow fresh;
            fresh.customerId = e.customerId;
            fresh.itemId = e.itemId;
            fresh.billable = e.billable;
            fresh.notes = e.notes;
            fresh.classId = e.classId;
            for (int i = 0; i < 7; ++i) {
                fresh.days << TimesheetCell{};
            }
            rows << fresh;
            row = rows.end() - 1;
        }
        row->days[day] = TimesheetCell{e.id, e.minutes, e.invoiceId != 0};
    }
    return rows;
}

Result TimeTracking::saveWeek(const Worker &worker, const QDate &weekStart, const QList<TimesheetRow> &rows)
{
    if (!weekStart.isValid() || !worker.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("choose the week and who worked it"));
    }
    SqlTransaction txn(m_file);
    if (!txn.begun()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    int rowNo = 0;
    for (const TimesheetRow &row : rows) {
        ++rowNo;
        if (row.days.size() != 7) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("row %1 does not have seven days").arg(rowNo));
        }
        for (int day = 0; day < 7; ++day) {
            const TimesheetCell &cell = row.days.at(day);
            const QDate date = weekStart.addDays(day);
            TimeEntry wanted;
            wanted.id = cell.entryId;
            wanted.date = date;
            wanted.worker = worker;
            wanted.customerId = row.customerId;
            wanted.itemId = row.itemId;
            wanted.minutes = cell.minutes;
            wanted.billable = row.billable;
            wanted.notes = row.notes.trimmed();
            wanted.classId = row.classId;
            const QString where = QStringLiteral("row %1, %2: ").arg(rowNo).arg(date.toString(Qt::ISODate));
            if (cell.entryId == 0) {
                if (cell.minutes == 0) {
                    continue;
                }
                if (const Result r = create(wanted); !r.ok) {
                    return Result::failure(r.code, where + r.error);
                }
                continue;
            }
            const std::optional<TimeEntry> before = entry(cell.entryId);
            if (!before || !(before->worker == worker) || before->date < weekStart
                || before->date > weekStart.addDays(6)) {
                return Result::failure(ErrorCode::Invalid, where + QStringLiteral("that entry is not in this week"));
            }
            if (cell.minutes == 0) {
                if (const Result r = remove(cell.entryId); !r.ok) {
                    return Result::failure(r.code, where + r.error);
                }
            } else if (!sameEntry(*before, wanted)) {
                if (const Result r = update(wanted); !r.ok) {
                    return Result::failure(r.code, where + r.error);
                }
            }
        }
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success();
}

} // namespace QindaOffice::Books
