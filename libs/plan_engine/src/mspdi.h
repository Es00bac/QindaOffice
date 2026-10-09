// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Microsoft Project XML (MSPDI, the "XML format" Project 2003–2021 and
// Project Online save; also what ProjectLibre, GanttProject and MPXJ read
// and write) — import and export of a Project (D-051, docs/plan.md
// "Microsoft Project XML").
//
// Covered both ways: project properties, base and resource calendars
// (week days and exceptions), the task outline with durations, milestones,
// manual and inactive tasks, constraints, priority and deadline (M63),
// % complete and actuals, notes, fixed cost, links of all four types with
// lag / lead / percentage lag, work and material resources, and
// assignments; since M79 (D-088) also baselines 0–10 of tasks, assignments
// and the project summary task (each element replayed byte for byte while
// unchanged), leveling delay and the pre-leveled dates, and the critical
// slack limit and multiple critical paths. Everything else a file holds —
// custom fields and their definitions, task type, hyperlinks, cost rate
// tables, recurring calendar exceptions and alternative work weeks (M79:
// also scheduled, derived from the kept elements), views, timephased data, elements
// of later Project versions — is KEPT verbatim and written back in
// Project's order (mspdi_keep.h, D-071); values Project derives from the
// schedule travel only while the object's dates and progress are as read.
// Native .mpp files are out of scope.
//
// AGENT-CONTRACT (units on the wire, mspdi_p.h has the converters):
//  * Durations, work and lag are working time: xs:duration "PT#H#M#S" for
//    Duration/Work, and LinkLag in TENTHS of a minute (a percentage lag,
//    LagFormat 19/20, is percent × 10).
//  * Cost, FixedCost, CostPerUse and baseline Cost are decimal CENTS
//    (MPXJ's parseCurrency divides by 100); StandardRate is currency units
//    per hour, StandardRateFormat being display only (Project normalises
//    the stored value to hours); MaxUnits and assignment Units are
//    decimals (1.0 = one full-time person; a material quantity).
//  * Dates are the project's wall clock "yyyy-MM-ddTHH:mm:ss".

#include "plan_types.h"

#include <QByteArray>
#include <QStringList>

namespace QindaOffice::Plan {

struct MspdiResult {
    bool ok = false;
    QString error;       // "line N: …" when the XML is malformed
    QStringList warnings; // what was dropped or approximated
};

// Reads an MSPDI document into *out (replacing it) and schedules it.
// Unmodelled elements are kept (see above); dangling links and assignments
// are dropped with a warning; recurring calendar exceptions and alternative
// work weeks are kept for the file and (M79) scheduled — a kind of
// recurrence QindaPlan cannot expand is reported as not scheduled.
MspdiResult readMspdi(const QByteArray &xml, Project *out);

// Writes MSPDI for a scheduled project (element order follows the MSPDI
// schema so Project itself accepts it).
[[nodiscard]] QByteArray writeMspdi(const Project &project);

// Cheap sniff of the first bytes: an XML <Project> root in the MSPDI
// namespace (http://schemas.microsoft.com/project).
[[nodiscard]] bool looksLikeMspdi(const QByteArray &head);

// M79: a .qplan saved by M63–M78 kept, as verbatim MSPDI elements, what M79
// models — baselines 3–10 and assignment baselines, leveling delays and
// pre-leveled dates, the critical-path options; this lifts them into the
// model, and rebuilds every calendar's recurring exceptions and work weeks
// from their kept elements (readPlanJson calls it on every load;
// mspdi_adopt.cpp). Returns how many elements it lifted.
int adoptKeptElements(Project &project);

} // namespace QindaOffice::Plan
