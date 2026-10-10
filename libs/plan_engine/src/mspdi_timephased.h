// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to mspdi_*.cpp (M83, D-092): split tasks in Microsoft Project XML.
// Project has no split element: a split is a stretch of an assignment's
// TimephasedData with no work between stretches with work (the same holds
// for the Stop/Resume of rescheduled uncompleted work, actual then
// remaining). A task without resources carries its timephased data on a
// placeholder assignment (ResourceUID -65535).
//
// AGENT-CONTRACT (reading): the splits of a task are the zero-work stretches
// that hold working time on its calendar between the stretches with work of
// its longest assignment (actual and remaining work, Types 2 and 1) — the
// placeholder's when it has no resource. Daily periods over a weekend hold
// no working time, so an unsplit task reads with no split.
// AGENT-CONTRACT (writing): Project's own timephased data is replayed while
// the assignment's stamp holds (mspdi_keep.h) and its periods still split
// the assignment's work where the task does; once a split task's schedule
// is QindaPlan's, each assignment gets one Type 1 period per working segment
// with its share of the work and one empty period per gap (Unit 2), and a
// split task without resources a placeholder assignment carrying them.

#include "mspdi_keep.h"
#include "mspdi_p.h"

#include <QXmlStreamWriter>

namespace QindaOffice::Plan {
class WorkCalendar;
}

namespace QindaOffice::Plan::Mspdi {

// The splits that TimephasedData `fragments` describe for `task` (scheduled
// or not: only the periods and its calendar are read).
[[nodiscard]] QList<TaskSplit> splitsFromTimephased(const QStringList &fragments, const Task &task,
                                                    const WorkCalendar &calendar);
// Sets Task::splits of each task from the kept TimephasedData of its
// assignments, or of its placeholder (`placeholders`: task uid → the
// placeholder's kept TimephasedData fragments).
void deriveSplits(Project &project, const QHash<int, QStringList> &placeholders);
// The kept TimephasedData fragments of one object's extra.
[[nodiscard]] QStringList keptTimephased(const QJsonObject &extra);

// An assignment's own finish: its task's, or where its shorter span ends.
[[nodiscard]] Minutes assignmentFinish(const Project &project, const Assignment &assignment);

// Writer hooks (mspdi_writer.cpp).
void writeSplitTimephased(Children &c, QXmlStreamWriter &xml, const Project &project, const Assignment &assignment);
void writeSplitPlaceholders(QXmlStreamWriter &xml, const Project &project);

} // namespace QindaOffice::Plan::Mspdi
