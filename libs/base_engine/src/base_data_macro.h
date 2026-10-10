// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data macros (M86, D-094): Access's table events — Before Change, Before
// Delete, After Insert, After Update and After Delete — each a macro of
// SetField, RaiseError, LogEvent and (M80's) StopMacro steps, every step
// with an optional Condition (Access's If around one action).
//
// AGENT-CONTRACT (storage and enforcement — every writer of a .qbase file
// meets them, as M80's validation rules):
//  * A table's data macros live in its own qb_fields row (`data_macros`,
//    JSON {event: Macro::toMap()}, base_macro.h's shape) and are compiled
//    into SQLite triggers named "qbm:<table>:<slot>", slot bi | bu | bd
//    (BEFORE INSERT / UPDATE / DELETE: RaiseError and StopMacro of the
//    Before events) and ai | au | ad (AFTER: SetField and LogEvent of
//    Before Change and Before Delete, then every step of the After event).
//    readTable lists them in TableDef::derivedTriggers; every plan that
//    touches the table drops and makes them again from the design.
//  * Every trigger fires only WHEN NOT EXISTS a qb_meta row with key
//    "qb.quiet" or "qb.busy:<table>". BaseFile sets "qb.quiet" while it
//    replays an undo or redo (the changeset already holds what the macro
//    did; SQLite fires triggers during sqlite3changeset_apply) and while a
//    linked table refreshes; a SetField sets "qb.busy:<table>" around its
//    own UPDATE so it fires no further event of its table. Both rows are
//    written and removed inside the same change, so no changeset keeps them.
//  * Values: [field] is the record as the change writes it (the deleted
//    record in the delete events), [Old].[field] the record before an
//    update (Null for a new record), IsInsert is Yes while Before Change
//    runs for a new record. Literals convert by the field they meet and are
//    quoted (base_expression.h); an unknown [name] is an error.
//  * SetField changes the record being saved (Before Change, After Insert,
//    After Update) through an UPDATE by rowid; RaiseError cancels the change
//    with its message (in an After event too: the statement is rolled back);
//    LogEvent appends to qb_application_log (Access's USysApplicationLog);
//    StopMacro skips the macro's later steps when its condition holds.
//  * M80's window actions (OpenForm … MessageBox) are not table events: a
//    trigger runs inside SQLite for every writer, with no window to act on —
//    Access refuses them in data macros too.

#include "base_macro.h"
#include "base_schema.h"

namespace QindaOffice::Base {

inline constexpr char kApplicationLogTable[] = "qb_application_log";

// beforeChange, beforeDelete, afterInsert, afterUpdate, afterDelete.
[[nodiscard]] QStringList dataMacroEvents();
[[nodiscard]] QString dataMacroEventLabel(const QString &event); // "Before Change"
[[nodiscard]] QList<MacroActionInfo> dataMacroActions();
[[nodiscard]] bool isMacroTrigger(const QString &name);
// The data macro of `event`, as saved (an empty macro when there is none).
[[nodiscard]] Macro dataMacro(const TableDef &table, const QString &event);
// Problems with one event's macro, numbered by step ("Step 2 (SetField):
// there is no field qty."); empty when it can be saved.
[[nodiscard]] QStringList checkDataMacro(const TableDef &table, const QString &event, const Macro &macro,
                                         const CurrencyFormat &currency);
// What makes the table's data macros work: the application log (when a
// LogEvent writes to it) and one trigger per slot with steps. `error`: the
// first macro that does not compile.
[[nodiscard]] QStringList dataMacroStatements(const TableDef &table, const CurrencyFormat &currency, QString *error);
// The plan that sets one event's macro of `table` (no steps: removes it) —
// one undoable schema step.
[[nodiscard]] SchemaPlan planDataMacro(const BaseFile &file, const QString &table, const QString &event,
                                       const Macro &macro);
// The SQL of the application log table (CREATE TABLE IF NOT EXISTS).
[[nodiscard]] QString applicationLogSql();

} // namespace QindaOffice::Base
