// SPDX-License-Identifier: GPL-3.0-or-later
// Macros: the action catalog, macro definitions, the checks a macro passes
// before it runs, and the where conditions OpenForm and ApplyFilter carry.
// See base_macro.h for the contract.
#include "base_macro.h"
#include "base_macro_flow.h"

#include "base_expression.h"
#include "base_objects.h"
#include "base_schema.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

MacroArgument arg(const char *id, const char *label, const char *kind, bool required = false,
                  QStringList choices = {})
{
    return {s(id), s(label), s(kind), std::move(choices), required};
}

const QStringList kKnownKeys{s("version"), s("description"), s("actions")};

// Any [name] is a field: a where condition checked before its table is known.
Expr::Context syntaxOnly()
{
    Expr::Context ctx;
    ctx.resolve = [](const QString &, const QString &) -> std::optional<Expr::FieldRef> {
        return Expr::FieldRef{s("x"), FieldType::Text};
    };
    return ctx;
}

QString objectKind(const QString &argumentKind)
{
    return argumentKind == QLatin1String("form") || argumentKind == QLatin1String("report")
                   || argumentKind == QLatin1String("query") || argumentKind == QLatin1String("macro")
               ? argumentKind
               : QString();
}

} // namespace

QVariantMap MacroActionInfo::toMap() const
{
    QVariantList list;
    for (const MacroArgument &a : arguments) {
        list.append(QVariantMap{{s("id"), a.id},
                                {s("label"), a.label},
                                {s("kind"), a.kind},
                                {s("choices"), a.choices},
                                {s("required"), a.required}});
    }
    return {{s("id"), id}, {s("label"), label}, {s("description"), description}, {s("arguments"), list}};
}

QList<MacroActionInfo> macroActions()
{
    // AGENT-NOTE: Access's everyday macro actions (the ones its Command
    // Button Wizard and the "Open … / Go to … / Apply Filter" macros use).
    // M97 SetValue changes the open form's draft; saving still passes the
    // normal record validation and BeforeUpdate event. RunSQL and VBA stay
    // absent: bulk data changes use a saved query with preview and Undo.
    static const QList<MacroActionInfo> actions{
        {s("openForm"), s("OpenForm"), s("Opens a form, on the records a condition picks or on a new record."),
         {arg("form", "Form name", "form", true), arg("where", "Where condition", "where"),
          arg("dataMode", "Data mode", "choice", false, {s("edit"), s("add")})}},
        {s("openReport"), s("OpenReport"), s("Shows a report's pages, or prints them."),
         {arg("report", "Report name", "report", true),
          arg("view", "View", "choice", false, {s("preview"), s("print")})}},
        {s("openQuery"), s("OpenQuery"),
         s("Runs a saved query: a select shows its datasheet; an action query asks first, as always."),
         {arg("query", "Query name", "query", true)}},
        {s("openTable"), s("OpenTable"), s("Opens a table's datasheet."), {arg("table", "Table name", "table", true)}},
        {s("applyFilter"), s("ApplyFilter"), s("Shows only the records of the open form or table that meet the condition."),
         {arg("where", "Where condition", "where", true)}},
        {s("showAllRecords"), s("ShowAllRecords"), s("Removes the filter of the open form or table."), {}},
        {s("goToRecord"), s("GoToRecord"), s("Moves to the first, previous, next or last record, or a new one."),
         {arg("record", "Record", "choice", true, {s("first"), s("previous"), s("next"), s("last"), s("new")})}},
        {s("saveRecord"), s("SaveRecord"), s("Saves the record being edited."), {}},
        {s("requery"), s("Requery"), s("Reads the records of the open object again."), {}},
        {s("findRecord"), s("FindRecord"), s("Finds the next record holding the text."),
         {arg("text", "Find what", "text", true)}},
        {s("closeWindow"), s("CloseWindow"), s("Closes the open form, report, table or query."), {}},
        {s("messageBox"), s("MessageBox"), s("Shows a message."),
         {arg("message", "Message", "text", true), arg("title", "Title", "text")}},
        {s("runMacro"), s("RunMacro"), s("Runs another macro."), {arg("macro", "Macro name", "macro", true)}},
        {s("stopMacro"), s("StopMacro"), s("Stops the macro here."), {}},
        {s("if"), s("If"), s("Runs the following actions when the expression is true."),
         {arg("condition", "Condition", "value", true)}},
        {s("else"), s("Else"), s("Runs this branch when the matching If is false."), {}},
        {s("endIf"), s("EndIf"), s("Ends the matching If block."), {}},
        {s("setValue"), s("SetValue"), s("Changes a field in the open form's draft record."),
         {arg("field", "Field name", "text", true), arg("expression", "Value expression", "value", true)}},
        {s("setTempVar"), s("SetTempVar"), s("Keeps a temporary value in this database window."),
         {arg("name", "Name", "text", true), arg("expression", "Value expression", "value", true)}},
        {s("removeTempVar"), s("RemoveTempVar"), s("Removes a temporary window value."),
         {arg("name", "Name", "text", true)}},
        {s("cancelEvent"), s("CancelEvent"), s("Cancels the form event without saving its record."),
         {arg("message", "Reason", "text")}},
    };
    return actions;
}

const MacroActionInfo *macroAction(const QString &id)
{
    static const QList<MacroActionInfo> actions = macroActions();
    for (const MacroActionInfo &a : actions) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

QVariantMap Macro::toMap() const
{
    QVariantMap out = extra;
    QVariantList list;
    for (const MacroStep &step : steps) {
        list.append(QVariantMap{{s("action"), step.action}, {s("arguments"), step.arguments}});
    }
    out.insert(s("version"), 1);
    out.insert(s("description"), description);
    out.insert(s("actions"), list);
    return out;
}

Macro Macro::fromMap(const QVariantMap &map)
{
    Macro m;
    m.description = map.value(s("description")).toString();
    for (const QVariant &v : map.value(s("actions")).toList()) {
        const QVariantMap step = v.toMap();
        m.steps.append({step.value(s("action")).toString(), step.value(s("arguments")).toMap()});
    }
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!kKnownKeys.contains(it.key())) {
            m.extra.insert(it.key(), it.value());
        }
    }
    return m;
}

QStringList checkMacro(const BaseFile &file, const Macro &macro, const QString &self)
{
    QStringList problems = checkMacroFlow(macro);
    for (int i = 0; i < macro.steps.size(); ++i) {
        const MacroStep &step = macro.steps.at(i);
        const MacroActionInfo *info = macroAction(step.action);
        if (info == nullptr) {
            problems.append(s("Step %1: there is no macro action \"%2\".").arg(i + 1).arg(step.action));
            continue;
        }
        const QString where = s("Step %1 (%2): ").arg(i + 1).arg(info->label);
        for (const MacroArgument &a : info->arguments) {
            const QString value = step.arguments.value(a.id).toString().trimmed();
            if (value.isEmpty()) {
                if (a.required) {
                    problems.append(where + s("fill in %1.").arg(a.label));
                }
                continue;
            }
            if (a.kind == QLatin1String("choice") && !a.choices.contains(value)) {
                problems.append(where + s("%1 is one of %2.").arg(a.label, a.choices.join(s(", "))));
            } else if (a.kind == QLatin1String("table") && !file.tableExists(value)) {
                problems.append(where + s("there is no table named %1.").arg(value));
            } else if (const QString kind = objectKind(a.kind); !kind.isEmpty() && !ObjectStore::load(file, kind, value)) {
                problems.append(where + s("there is no %1 named %2.").arg(kind, value));
            } else if (a.kind == QLatin1String("macro") && !self.isEmpty()
                       && value.compare(self, Qt::CaseInsensitive) == 0) {
                problems.append(where + s("a macro cannot run itself."));
            }
        }
        const QString condition = step.arguments.value(s("where")).toString().trimmed();
        if (condition.isEmpty()) {
            continue;
        }
        // OpenForm's condition is checked against the form's table; any
        // other condition only for its syntax (the table is the one open
        // when the macro runs).
        QString table;
        if (step.action == QLatin1String("openForm")) {
            if (const auto form = ObjectStore::load(file, s("form"), step.arguments.value(s("form")).toString())) {
                table = form->definition.value(s("source")).toMap().value(s("name")).toString();
            }
        }
        if (!table.isEmpty()) {
            if (const CompiledWhere w = compileWhere(file, table, condition); !w.ok()) {
                problems.append(where + s("the where condition: %1").arg(w.error));
            }
        } else if (const Expr::Compiled c = Expr::expression(condition, syntaxOnly()); !c.ok()) {
            problems.append(where + s("the where condition: %1").arg(c.error));
        }
    }
    return problems;
}

CompiledWhere compileWhere(const BaseFile &file, const QString &table, const QString &condition)
{
    CompiledWhere out;
    const std::optional<TableDef> def = readTable(file, table);
    if (!def) {
        out.error = s("There is no table named %1.").arg(table);
        return out;
    }
    if (condition.trimmed().isEmpty()) {
        out.sql = s("1");
        return out;
    }
    Expr::Context ctx;
    ctx.currency = file.currency();
    ctx.parameters = nullptr; // a condition asks for nothing: an unknown [name] is an error
    const TableDef t = *def;
    ctx.resolve = [t](const QString &owner, const QString &name) -> std::optional<Expr::FieldRef> {
        if (!owner.isEmpty() && owner.compare(t.name, Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
        if (const FieldDef *f = t.field(name)) {
            return Expr::FieldRef{quoteIdentifier(f->name), f->type};
        }
        return std::nullopt;
    };
    const Expr::Compiled c = Expr::expression(condition, ctx);
    if (!c.ok()) {
        out.error = c.error;
        return out;
    }
    if (c.aggregate) {
        out.error = s("A where condition cannot total records.");
        return out;
    }
    out.sql = c.sql;
    return out;
}

std::optional<QList<qint64>> filteredRowIds(const BaseFile &file, const QString &table, const QString &condition,
                                            QString *error, int limit)
{
    const CompiledWhere where = compileWhere(file, table, condition);
    if (!where.ok()) {
        *error = where.error;
        return std::nullopt;
    }
    QString problem;
    const QList<QVariantList> rows = file.db().rows(
        s("SELECT rowid FROM %1 WHERE %2 ORDER BY rowid LIMIT %3").arg(quoteIdentifier(table), where.sql).arg(limit), {},
        &problem);
    if (!problem.isEmpty()) {
        *error = problem;
        return std::nullopt;
    }
    QList<qint64> out;
    out.reserve(rows.size());
    for (const QVariantList &row : rows) {
        out.append(row.value(0).toLongLong());
    }
    return out;
}

} // namespace QindaOffice::Base
