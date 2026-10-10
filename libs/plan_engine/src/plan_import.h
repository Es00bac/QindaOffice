// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel and CSV task import (M83, D-092) — Project's Import Wizard for
// tasks: a table of text (an .xlsx sheet read by plan_output's
// readTaskTableXlsx, or CSV parsed here) whose columns are mapped one by one
// onto task fields, appended to the project as one change.
//
// AGENT-CONTRACT (mapping): one entry per source column — a task field id
// of importFieldIds() or "" to skip the column. Every cell goes through
// setTaskField (plan_edit.h), so it is read exactly as if typed into the
// sheet; a value a field refuses becomes a warning naming the row and the
// field, and the import goes on. "outlineLevel" (or "wbs", "1.2.3" = level
// 3) places the tasks in an outline; "id" is the source's task ID, which
// "predecessors" refer to (else the data row number does), and both are
// translated to the new tasks' IDs.

#include "plan_edit.h"

namespace QindaOffice::Plan {

struct ImportTable {
    QList<QStringList> rows; // as read; row 0 may be the headers
};

// RFC 4180 CSV; the separator (comma, semicolon or tab) is the one the
// first line uses most; UTF-8 (a BOM dropped), else Latin-1.
[[nodiscard]] ImportTable parseCsvTable(const QByteArray &data);

// The fields a column can map to, and their titles ("Task Name", "Outline
// Level", "Text1" …).
[[nodiscard]] QStringList importFieldIds();
[[nodiscard]] QString importFieldTitle(const QString &field);
// A first guess per header: Project's column names, QindaPlan's own sheet
// titles, the field ids and (with a project) its custom fields' new names,
// ignoring case and punctuation; "" when none.
[[nodiscard]] QStringList guessImportMapping(const QStringList &headers, const Project *project = nullptr);

struct ImportReport {
    int tasks = 0;
    QStringList warnings;
};
// Appends the data rows (rows without a name are skipped, with a warning
// when they hold anything) as new tasks at the end of the project; refuses
// (project untouched) when no column is the task name or nothing imports.
EditResult importTasks(Project &project, const ImportTable &table, const QStringList &mapping, bool firstRowHeaders,
                       ImportReport *report, const QLocale &locale = QLocale());

} // namespace QindaOffice::Plan
