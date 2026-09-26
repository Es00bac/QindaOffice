// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Resource load: how much of each work resource's time the assignments use,
// per day, against what its calendar and maximum units allow.
//
// AGENT-CONTRACT (overallocation): a work resource is overallocated on a
// day when the sum over its assignments of (the task's working minutes that
// day × units) exceeds (the resource calendar's working minutes that day ×
// max units). Day granularity is Project's "Day by Day" leveling basis; a
// resource double-booked for one hour of one day counts. Material
// resources are never overallocated.

#include "plan_types.h"

#include <QMap>

namespace QindaOffice::Plan {

// Per resource uid: day (days since 1970-01-01) → load in unit-minutes
// (working minutes × units percent, so 100 = one minute of one person).
using DailyLoad = QHash<int, QMap<qint64, qint64>>;

[[nodiscard]] DailyLoad dailyLoad(const Project &project);
// Sets Resource::overallocated and Task::overallocated (a task working on
// a day its resource is overallocated). Called by schedule().
void computeOverallocation(Project &project);
// The first overallocated day of `resourceUid` on or after `from` (a date),
// invalid when none.
[[nodiscard]] QDate firstOverallocatedDay(const Project &project, int resourceUid, const QDate &from = {});

// The usage table (Resource Usage view, ResourceTable): one row per work
// resource followed by one row per assignment, one bucket per day or week.
struct UsageRow {
    QString id;       // "r:<uid>" or "a:<uid>"
    int resourceUid = 0;
    int taskUid = 0;  // 0 on a resource row
    QString name;
    bool assignment = false;
    Minutes capacityPerBucket = 0; // resource rows: its most common bucket capacity
    QList<Minutes> work;           // working minutes per bucket
    QList<Minutes> capacity;       // per bucket (resource rows)
    QList<bool> over;              // per bucket: a day inside it is overallocated
    Minutes total = 0;
};
struct UsageTable {
    QList<QDate> bucketStarts;
    int bucketDays = 1;
    QList<UsageRow> rows;
};
// Buckets from `from` (snapped to a Monday for weeks) covering `count`
// buckets (at most 256, the ResourceTable limit).
[[nodiscard]] UsageTable usageTable(const Project &project, const QDate &from, int count, bool weeks);

} // namespace QindaOffice::Plan
