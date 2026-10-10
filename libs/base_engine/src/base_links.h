// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Linked tables (M86, D-094): Access's linked tables, to a table of another
// .qbase (or any SQLite file), a CSV / text file or an Excel sheet.
//
// AGENT-CONTRACT (with readTable, Records, the planners, BaseService and
// qml/LinkTableDialog.qml / LinkedTableManagerDialog.qml):
//  * A linked table is an ordinary table holding a COPY of its source's
//    records, so every query, form, report and SQLite tool reads it as it
//    reads any table. Its own qb_fields row carries the source
//    (LinkSource::toMap as JSON in `link_source`; TableDef::link).
//  * The copy is read-only: QindaBase never edits it (datasheets and forms
//    show it read-only, designs refuse changes, no relationship may point
//    at it, no data macro may run on it) and guard triggers
//    "qbk:<table>:i|u|d" refuse every other writer, WHEN NOT EXISTS the
//    qb_meta row "qb.quiet" — which a refresh writes and removes inside its
//    own transaction.
//  * The copy is refreshed from the source when the database opens
//    (refreshLinks: not an undo step, nothing to undo) and on demand (one
//    undo step). A source whose fields changed rebuilds the copy's design;
//    a source that cannot be read leaves the last copy and says why
//    (LinkSource::problem).
//  * The source is found by its path relative to the database's folder
//    first (a database moved together with its files keeps working), then
//    by its absolute path.
//  * Reading an .xlsx sheet needs calc_io, which base_engine does not link:
//    the app passes a LinkReader (base_xlsx_import.h); without one an Excel
//    link cannot be refreshed and says so.

#include "base_csv.h"
#include "base_schema.h"

#include <functional>
#include <optional>

namespace QindaOffice::Base {

struct LinkSource {
    QString kind;           // qbase | csv | xlsx
    QString path;           // absolute, as linked
    QString relativePath;   // from the database's folder ("" when it has none yet)
    QString table;          // qbase: the source table; xlsx: the sheet ("" = the first holding data)
    QString delimiter;      // csv: "" = detected
    bool headerRow = true;  // csv, xlsx
    QString refreshed;      // yyyy-MM-dd HH:mm:ss of the last good refresh
    qint64 rows = 0;        // records the last refresh copied
    QString problem;        // why the last refresh failed ("" = it worked)
    [[nodiscard]] QVariantMap toMap() const;
    static LinkSource fromMap(const QVariantMap &map);
    [[nodiscard]] QString description() const; // "Members (members.qbase)", "Sheet1 of stock.xlsx"
};

using LinkReader = std::function<std::optional<CsvTable>(const LinkSource &source, const QString &path, QString *error)>;

[[nodiscard]] bool isLinkTrigger(const QString &name);
// The guard triggers of a linked table (none for another table).
[[nodiscard]] QStringList linkGuardStatements(const TableDef &table);
// The file the link reads now ("" when neither path exists).
[[nodiscard]] QString linkPath(const BaseFile &file, const LinkSource &source);
// The tables of a .qbase or SQLite file a link may name.
[[nodiscard]] QStringList linkableTables(const QString &path, QString *error);

struct LinkCopy {
    QList<FieldDef> fields;   // the copy's design: names, types, captions, the key of a .qbase table
    QList<QVariantList> rows; // storage values (base_types.h)
};
[[nodiscard]] std::optional<LinkCopy> readLinkSource(const BaseFile &file, const LinkSource &source,
                                                     const LinkReader &xlsx, QString *error);

struct LinkResult {
    bool ok = false;
    QString table;
    QString error;
    qint64 rows = 0;
    bool redesigned = false;  // the source's fields had changed
    QList<Change> changes;    // undoable runs: the step(s) to push as one
};

// A new linked table `name` over `source`: its design, guard and copy (one
// undo step: the plan and the records).
LinkResult linkTable(BaseFile &file, const QString &name, const LinkSource &source, const LinkReader &xlsx);
// The copy read again from its source; `undoable` captures it as changes
// (Refresh, Linked Table Manager), otherwise it is the database opening.
LinkResult refreshLink(BaseFile &file, const QString &table, const LinkReader &xlsx, bool undoable);
// Every linked table, at open (read-only files are left as they are).
QList<LinkResult> refreshLinks(BaseFile &file, const LinkReader &xlsx);
// Convert to Local Table: the copy stays, editable; the link and guard go.
[[nodiscard]] SchemaPlan planUnlinkTable(const BaseFile &file, const QString &table);
// A new path for a link's source (Relink); the copy refreshes after it.
[[nodiscard]] SchemaPlan planRelinkTable(const BaseFile &file, const QString &table, const QString &path);
// The linked tables and their sources, by name.
[[nodiscard]] QList<QPair<QString, LinkSource>> linkedTables(const BaseFile &file);

} // namespace QindaOffice::Base
