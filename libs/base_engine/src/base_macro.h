// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Macros without VBA (M80, D-089 §5): Access's named macros — an ordered
// list of actions with arguments ("open the Donor form on the donors of
// Portland, then go to a new record") — and the where conditions the
// actions carry.
//
// AGENT-CONTRACT (with apps/qindabase BaseCommands.qml's macro runner, the
// form controls in FormView.qml and tst_base_macro_report):
//  * A macro is saved as qb_objects kind "macro" with the definition
//    {"version":1, "description":…, "actions":[{"action": id,
//    "arguments": {argument id: text}}]} (Macro::toMap). Unknown members
//    survive (the service merges into the saved map). A form's command
//    button carries the same "actions" list inline (an embedded macro).
//  * The action ids are Access's macro actions, lower camel case:
//    openForm, openReport, openQuery, openTable, applyFilter,
//    showAllRecords, goToRecord, saveRecord, requery, findRecord,
//    closeWindow, messageBox, runMacro, stopMacro; M97 adds if, else,
//    endIf, setValue, setTempVar, removeTempVar and cancelEvent.
//    macroActions() lists
//    them with their arguments; the window runs them, the engine only
//    checks them (checkMacro) — a macro never runs SQL of its own.
//  * Where conditions are Access expressions over one table's fields
//    ("[city] = \"Portland\" And [amount] > 100"), compiled by the query
//    compiler (base_expression.h): literals are converted by the field
//    they meet and quoted, an unknown [name] is an error (a macro asks for
//    nothing), so a condition is never pasted into SQL.

#include "base_file.h"
#include "base_query.h"

#include <QVariantMap>

namespace QindaOffice::Base {

struct MacroArgument {
    QString id;
    QString label;
    // form | report | query | table | macro (an object name) | where |
    // text | choice
    QString kind;
    QStringList choices; // kind "choice": the ids
    bool required = false;
};

struct MacroActionInfo {
    QString id;
    QString label;       // "OpenForm"
    QString description; // what it does, in a sentence
    QList<MacroArgument> arguments;
    [[nodiscard]] QVariantMap toMap() const;
};

// The action catalog, in the order the designer offers it.
[[nodiscard]] QList<MacroActionInfo> macroActions();
[[nodiscard]] const MacroActionInfo *macroAction(const QString &id);

struct MacroStep {
    QString action;
    QVariantMap arguments; // argument id → text
};

struct Macro {
    QList<MacroStep> steps;
    QString description;
    QVariantMap extra; // members this version does not know, kept on save
    [[nodiscard]] QVariantMap toMap() const;
    static Macro fromMap(const QVariantMap &map);
};

// Problems with a macro against this database, each naming its step
// ("Step 2 (OpenForm): there is no form named Donors."); empty when the
// macro can run. `self` is the macro's own name (RunMacro of itself is a
// loop and is refused).
[[nodiscard]] QStringList checkMacro(const BaseFile &file, const Macro &macro, const QString &self = {});

struct CompiledWhere {
    QString sql;   // a boolean SQL expression over the table's columns
    QString error; // why the condition cannot be used
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

// An Access where condition over `table` (an empty condition is "every
// record": sql "1").
[[nodiscard]] CompiledWhere compileWhere(const BaseFile &file, const QString &table, const QString &condition);

// The rowids of `table` that meet `condition`, in rowid order (a form's
// filter); nullopt with `error` when the condition does not compile.
[[nodiscard]] std::optional<QList<qint64>> filteredRowIds(const BaseFile &file, const QString &table,
                                                          const QString &condition, QString *error,
                                                          int limit = 100000);

} // namespace QindaOffice::Base
