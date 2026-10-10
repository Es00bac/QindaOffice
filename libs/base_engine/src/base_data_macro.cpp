// SPDX-License-Identifier: GPL-3.0-or-later
// Data macros: the catalog, the checks, and the triggers that run them.
// See base_data_macro.h for the contract.
#include "base_data_macro.h"

#include "base_calculated.h"
#include "base_expression.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

const QString kPrefix = s("qbm:");

MacroArgument arg(const char *id, const char *label, const char *kind, bool required = false)
{
    return {s(id), s(label), s(kind), {}, required};
}

bool deleteEvent(const QString &event)
{
    return event == QLatin1String("beforeDelete") || event == QLatin1String("afterDelete");
}

bool beforeEvent(const QString &event)
{
    return event == QLatin1String("beforeChange") || event == QLatin1String("beforeDelete");
}

// One SQLite trigger slot a macro's steps can land in.
struct Slot {
    const char *id;     // bi | bu | bd | ai | au | ad
    const char *timing; // BEFORE | AFTER
    const char *op;     // INSERT | UPDATE | DELETE
};

constexpr Slot kSlots[] = {{"bi", "BEFORE", "INSERT"}, {"bu", "BEFORE", "UPDATE"}, {"bd", "BEFORE", "DELETE"},
                           {"ai", "AFTER", "INSERT"},  {"au", "AFTER", "UPDATE"},  {"ad", "AFTER", "DELETE"}};

// The events whose steps a slot runs, in order (the Before event's
// after-phase steps first, then the After event).
QStringList eventsOf(const Slot &slot)
{
    const QString id = QString::fromLatin1(slot.id);
    if (id == QLatin1String("bi") || id == QLatin1String("bu")) {
        return {s("beforeChange")};
    }
    if (id == QLatin1String("bd")) {
        return {s("beforeDelete")};
    }
    if (id == QLatin1String("ai")) {
        return {s("beforeChange"), s("afterInsert")};
    }
    if (id == QLatin1String("au")) {
        return {s("beforeChange"), s("afterUpdate")};
    }
    return {s("beforeDelete"), s("afterDelete")};
}

// Whether a step of `event` runs before the row is written (RaiseError and
// StopMacro of the Before events) or after it.
bool beforePhase(const QString &event, const QString &action)
{
    return beforeEvent(event) && (action == QLatin1String("raiseError") || action == QLatin1String("stopMacro"));
}

// Field references inside a data macro for one SQLite operation.
Expr::Context macroContext(const TableDef &table, const QString &op, const CurrencyFormat &currency)
{
    const bool insert = op == QLatin1String("INSERT");
    const bool remove = op == QLatin1String("DELETE");
    Expr::Context ctx;
    ctx.currency = currency;
    ctx.parameters = nullptr; // a macro asks for nothing: an unknown [name] is an error
    ctx.resolve = [&table, insert, remove](const QString &owner, const QString &name) -> std::optional<Expr::FieldRef> {
        if (owner.isEmpty() && name.compare(QLatin1String("IsInsert"), Qt::CaseInsensitive) == 0) {
            return Expr::FieldRef{insert ? s("1") : s("0"), FieldType::YesNo};
        }
        const bool old = owner.compare(QLatin1String("Old"), Qt::CaseInsensitive) == 0;
        if (!owner.isEmpty() && !old && owner.compare(table.name, Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
        const FieldDef *f = table.field(name);
        if (f == nullptr) {
            return std::nullopt;
        }
        if (old && insert) {
            return Expr::FieldRef{s("NULL"), f->type}; // a new record has no old values
        }
        return Expr::FieldRef{QStringLiteral("%1.%2").arg(old || remove ? s("OLD") : s("NEW"), q(f->name)), f->type};
    };
    return ctx;
}

struct StepSql {
    QString head;      // the statement up to its guard ("… WHERE "); "" for StopMacro
    QString condition; // the step's own condition ("1" when none)
    QString error;
};

// One step as SQL for one operation.
StepSql compileStep(const TableDef &table, const QString &event, const MacroStep &step, const QString &op,
                    const CurrencyFormat &currency)
{
    StepSql out;
    const Expr::Context ctx = macroContext(table, op, currency);
    const QString condition = step.arguments.value(s("condition")).toString().trimmed();
    out.condition = s("1");
    if (!condition.isEmpty()) {
        const Expr::Compiled c = Expr::expression(condition, ctx);
        if (!c.ok() || c.aggregate) {
            out.error = c.ok() ? s("the condition cannot total records.") : s("the condition: %1").arg(c.error);
            return out;
        }
        out.condition = c.sql;
    }
    const QString label = dataMacroEventLabel(event);
    if (step.action == QLatin1String("raiseError")) {
        const QString message = step.arguments.value(s("description")).toString().trimmed();
        if (message.isEmpty() || message.startsWith(QLatin1Char('='))) {
            out.error = s("give the error's message as text.");
            return out;
        }
        out.head = QStringLiteral("SELECT RAISE(ABORT, %1) WHERE ").arg(quoteLiteral(message));
    } else if (step.action == QLatin1String("logEvent")) {
        const QString text = step.arguments.value(s("description")).toString().trimmed();
        QString description = quoteLiteral(text);
        if (text.startsWith(QLatin1Char('='))) {
            const Expr::Compiled c = Expr::expression(text.mid(1), ctx);
            if (!c.ok() || c.aggregate) {
                out.error = c.ok() ? s("the description cannot total records.") : s("the description: %1").arg(c.error);
                return out;
            }
            description = QStringLiteral("CAST(%1 AS TEXT)").arg(c.sql);
        } else if (text.isEmpty()) {
            out.error = s("fill in the description to log.");
            return out;
        }
        out.head = QStringLiteral("INSERT INTO %1(created, category, object_type, source_object, event, description) "
                                  "SELECT datetime('now','localtime'), 'Execution', 'Table', %2, %3, %4 WHERE ")
                       .arg(q(s(kApplicationLogTable)), quoteLiteral(table.name), quoteLiteral(label), description);
    } else if (step.action == QLatin1String("setField")) {
        const FieldDef *f = table.field(step.arguments.value(s("field")).toString().trimmed());
        if (deleteEvent(event)) {
            out.error = s("a deleted record has no field to set.");
            return out;
        }
        if (f == nullptr) {
            out.error = s("there is no field %1.").arg(step.arguments.value(s("field")).toString());
            return out;
        }
        if (f->calculated || !isTypable(f->type)) {
            out.error = s("%1 cannot be set (it is worked out or numbered by the database).").arg(f->name);
            return out;
        }
        const QString text = step.arguments.value(s("value")).toString().trimmed();
        const Expr::Compiled v = Expr::valueFor(text.isEmpty() ? s("Null") : text, f->type, ctx);
        if (!v.ok() || v.aggregate) {
            out.error = v.ok() ? s("the value cannot total records.") : s("the value: %1").arg(v.error);
            return out;
        }
        const QString value = v.literal || text.isEmpty() ? v.sql : convertValueSql(v.sql, v.type, f->type, currency);
        out.head = QStringLiteral("UPDATE %1 SET %2 = %3 WHERE rowid = NEW.rowid AND ").arg(q(table.name), q(f->name), value);
    } else if (step.action != QLatin1String("stopMacro")) {
        out.error = s("\"%1\" is not a data macro action.").arg(step.action);
    }
    return out;
}

// The body of one slot: every step that lands in it, with the StopMacro
// conditions met so far guarding the later ones.
QStringList slotBody(const TableDef &table, const Slot &slot, const CurrencyFormat &currency, bool *setsFields,
                     QString *error)
{
    QStringList out;
    const QString op = QString::fromLatin1(slot.op);
    const bool before = QLatin1String(slot.timing) == QLatin1String("BEFORE");
    for (const QString &event : eventsOf(slot)) {
        const Macro macro = dataMacro(table, event);
        QStringList stops;
        for (int i = 0; i < macro.steps.size(); ++i) {
            const MacroStep &step = macro.steps.at(i);
            const StepSql sql = compileStep(table, event, step, op, currency);
            if (!sql.error.isEmpty()) {
                if (error->isEmpty()) {
                    *error = s("%1 of %2, step %3: %4").arg(dataMacroEventLabel(event), table.name).arg(i + 1).arg(sql.error);
                }
                return {};
            }
            QString guard = QLatin1Char('(') + sql.condition + QLatin1Char(')');
            for (const QString &stop : std::as_const(stops)) {
                guard += QStringLiteral(" AND NOT (%1)").arg(stop);
            }
            if (step.action == QLatin1String("stopMacro")) {
                stops.append(sql.condition);
                continue;
            }
            if (beforePhase(event, step.action) != before) {
                continue;
            }
            *setsFields = *setsFields || step.action == QLatin1String("setField");
            out.append(sql.head + guard + QLatin1Char(';'));
        }
    }
    return out;
}

} // namespace

QStringList dataMacroEvents()
{
    return {s("beforeChange"), s("beforeDelete"), s("afterInsert"), s("afterUpdate"), s("afterDelete")};
}

QString dataMacroEventLabel(const QString &event)
{
    static const QHash<QString, QString> labels{{s("beforeChange"), s("Before Change")},
                                                {s("beforeDelete"), s("Before Delete")},
                                                {s("afterInsert"), s("After Insert")},
                                                {s("afterUpdate"), s("After Update")},
                                                {s("afterDelete"), s("After Delete")}};
    return labels.value(event, event);
}

QList<MacroActionInfo> dataMacroActions()
{
    static const QList<MacroActionInfo> actions{
        {s("setField"), s("SetField"), s("Sets a field of the record being saved to a value or an expression."),
         {arg("field", "Name", "field", true), arg("value", "Value", "value", false), arg("condition", "If", "where")}},
        {s("raiseError"), s("RaiseError"), s("Cancels the change and shows the message."),
         {arg("description", "Error description", "text", true), arg("number", "Error number", "text"),
          arg("condition", "If", "where")}},
        {s("logEvent"), s("LogEvent"), s("Writes a line to the application log (=expression for a worked-out text)."),
         {arg("description", "Description", "text", true), arg("condition", "If", "where")}},
        {s("stopMacro"), s("StopMacro"), s("Skips the rest of this macro."), {arg("condition", "If", "where")}},
    };
    return actions;
}

bool isMacroTrigger(const QString &name)
{
    return name.startsWith(kPrefix, Qt::CaseInsensitive);
}

Macro dataMacro(const TableDef &table, const QString &event)
{
    return Macro::fromMap(table.dataMacros.value(event).toMap());
}

QStringList checkDataMacro(const TableDef &table, const QString &event, const Macro &macro, const CurrencyFormat &currency)
{
    QStringList problems;
    if (!dataMacroEvents().contains(event)) {
        return {s("There is no table event \"%1\".").arg(event)};
    }
    if (table.withoutRowid) {
        return {s("%1 is a WITHOUT ROWID table; data macros need a rowid.").arg(table.name)};
    }
    if (!table.link.isEmpty()) {
        return {s("%1 is linked to another file, so it takes no data macros (change the source instead).").arg(table.name)};
    }
    const QList<MacroActionInfo> catalog = dataMacroActions();
    for (int i = 0; i < macro.steps.size(); ++i) {
        const MacroStep &step = macro.steps.at(i);
        const auto info = std::find_if(catalog.cbegin(), catalog.cend(), [&](const MacroActionInfo &a) { return a.id == step.action; });
        if (info == catalog.cend()) {
            problems.append(s("Step %1: %2 is not a table event action; table events set fields, raise errors and "
                              "log (OpenForm and the other window actions belong to ordinary macros).")
                                .arg(i + 1)
                                .arg(step.action));
            continue;
        }
        const QString where = s("Step %1 (%2): ").arg(i + 1).arg(info->label);
        bool missing = false;
        for (const MacroArgument &a : info->arguments) {
            if (a.required && step.arguments.value(a.id).toString().trimmed().isEmpty()) {
                problems.append(where + s("fill in %1.").arg(a.label));
                missing = true;
            }
        }
        if (missing) {
            continue;
        }
        const QString op = deleteEvent(event) ? s("DELETE") : event == QLatin1String("afterInsert") ? s("INSERT") : s("UPDATE");
        if (const StepSql sql = compileStep(table, event, step, op, currency); !sql.error.isEmpty()) {
            problems.append(where + sql.error);
        }
    }
    return problems;
}

QString applicationLogSql()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS %1(id INTEGER PRIMARY KEY AUTOINCREMENT, created TEXT NOT NULL, "
                          "category TEXT, object_type TEXT, source_object TEXT, event TEXT, description TEXT)")
        .arg(q(s(kApplicationLogTable)));
}

QStringList dataMacroStatements(const TableDef &table, const CurrencyFormat &currency, QString *error)
{
    QStringList out;
    bool logs = false;
    for (const QString &event : dataMacroEvents()) {
        for (const MacroStep &step : dataMacro(table, event).steps) {
            logs = logs || step.action == QLatin1String("logEvent");
        }
    }
    if (logs) {
        out.append(applicationLogSql());
    }
    const QString busy = quoteLiteral(QStringLiteral("qb.busy:%1").arg(table.name));
    for (const Slot &slot : kSlots) {
        bool setsFields = false;
        QStringList body = slotBody(table, slot, currency, &setsFields, error);
        if (!error->isEmpty()) {
            return {};
        }
        if (body.isEmpty()) {
            continue;
        }
        if (setsFields) {
            // AGENT-GUARD: SetField's own UPDATE must fire no event of this
            // table (a Before Change would run twice) — base_data_macro.h.
            body.prepend(QStringLiteral("INSERT OR REPLACE INTO qb_meta(key, value) VALUES(%1, '1');").arg(busy));
            body.append(QStringLiteral("DELETE FROM qb_meta WHERE key = %1;").arg(busy));
        }
        out.append(QStringLiteral("CREATE TRIGGER %1 %2 %3 ON %4 FOR EACH ROW WHEN NOT EXISTS (SELECT 1 FROM qb_meta "
                                  "WHERE key IN ('qb.quiet', %5)) BEGIN %6 END")
                       .arg(q(kPrefix + table.name + QLatin1Char(':') + QString::fromLatin1(slot.id)),
                            QString::fromLatin1(slot.timing), QString::fromLatin1(slot.op), q(table.name), busy,
                            body.join(QLatin1Char(' '))));
    }
    return out;
}

SchemaPlan planDataMacro(const BaseFile &file, const QString &table, const QString &event, const Macro &macro)
{
    SchemaPlan plan;
    const std::optional<TableDef> before = readTable(file, table);
    if (!before) {
        plan.error = s("There is no table named %1.").arg(table);
        return plan;
    }
    if (!file.hasSystemTables()) {
        plan.error = s("Data macros are kept in QindaBase's system tables; add them first.");
        return plan;
    }
    if (const QStringList problems = checkDataMacro(*before, event, macro, file.currency()); !problems.isEmpty()) {
        plan.error = problems.join(QLatin1Char('\n'));
        return plan;
    }
    TableDef after = *before;
    if (macro.steps.isEmpty()) {
        after.dataMacros.remove(event);
    } else {
        after.dataMacros.insert(event, macro.toMap());
    }
    plan = planAlterTable(file, *before, after);
    plan.label = s("%1 data macro of %2").arg(dataMacroEventLabel(event), before->name);
    return plan;
}

} // namespace QindaOffice::Base
