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
// lag / lead / percentage lag, baselines 0–2, work and material resources,
// and assignments. Everything else a file holds — custom fields and their
// definitions, task type, hyperlinks, cost rate tables, baselines 3–10,
// recurring calendar exceptions, views, timephased data, elements of later
// Project versions — is KEPT verbatim and written back in Project's order
// (mspdi_keep.h, D-071); values Project derives from the schedule travel
// only while the object's dates and progress are as read. Native .mpp
// files are out of scope.
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
// work weeks are kept for the file and reported as not scheduled.
MspdiResult readMspdi(const QByteArray &xml, Project *out);

// Writes MSPDI for a scheduled project (element order follows the MSPDI
// schema so Project itself accepts it).
[[nodiscard]] QByteArray writeMspdi(const Project &project);

// Cheap sniff of the first bytes: an XML <Project> root in the MSPDI
// namespace (http://schemas.microsoft.com/project).
[[nodiscard]] bool looksLikeMspdi(const QByteArray &head);

} // namespace QindaOffice::Plan
