// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Presentation databases for QindaTK's form and report engines (M49,
// D-049 §8). QindaTK's FormDesigner and LocalReportDocumentModel read a
// SQLite schema through Tk.LocalDatabaseProvider; QindaBase hands them
// scratch databases shaped for presentation instead of the live file:
//
//  * a FORM MIRROR: the table's columns with no rows, where currency keeps
//    its CURRENCY type (the form passes it as text, which the engine
//    parses exactly); file fields are left out (the app shows them beside
//    the form); lookup fields are TEXT (M89, D-097: the combo or list box
//    bound to one shows the related records' labels, base_form_layout.h —
//    M80 had left them out while the designer drew no combo boxes);
//  * a REPORT SNAPSHOT: the rows of a table or a saved select query, with
//    lookup fields as their display text, currency as REAL units and file
//    fields as their display text ("PNG image · 24 KB").
//
// AGENT-NOTE: QindaTK's report engine totals in double precision (its
// query_core: "a host requiring exact currency arithmetic must provide its
// own backend"). Since M80 (D-089 §7) the report's pages come from the
// engine's own evaluation (base_report.h), exact in minor units; the
// snapshot feeds only the toolkit's ReportDesigner and its preview.
// Snapshots hold at most 4,000 rows, the report engine's own read limit.

#include "base_file.h"
#include "base_query.h"

#include <QHash>

namespace QindaOffice::Base {

struct SnapshotColumn {
    QString name;
    FieldType type = FieldType::Text;
    QHash<QString, QString> lookupDisplay; // stored value (as text) → display text
};

constexpr int kSnapshotRowLimit = 4000;

// (Re)creates `table` in the SQLite file at `path` with `rows`.
Result writeSnapshot(const QString &path, const QString &table, const QList<SnapshotColumn> &columns,
                     const QList<QVariantList> &rows, const CurrencyFormat &currency, bool *truncated = nullptr);
// (Re)creates an empty mirror of `table` from `file` in the SQLite file at `path`.
Result writeFormMirror(const BaseFile &file, const QString &path, const QString &table);

} // namespace QindaOffice::Base
