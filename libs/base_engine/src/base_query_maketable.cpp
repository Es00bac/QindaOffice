// SPDX-License-Identifier: GPL-3.0-or-later
// Make-table queries (M80, D-089 §3): Access's SELECT … INTO as a schema
// plan — previewed, backed up when it replaces a table, one undo step
// (whole-database images, like every schema change). The new table's
// fields take the query's output types, so money stays Currency and dates
// stay dates.
#include "base_query_p.h"
#include "base_schema_p.h"

#include <QSet>

namespace QindaOffice::Base {

namespace detail {

QString inlineParameters(const BaseFile &file, const QString &sql, const QList<QueryParameter> &declared,
                         const QVariantMap &values, QString *error)
{
    const CurrencyFormat currency = file.currency();
    QString out;
    const int n = int(sql.size());
    for (int i = 0; i < n; ++i) {
        const QChar c = sql.at(i);
        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            // Copy a quoted literal or identifier whole ('' and "" escape).
            int j = i + 1;
            while (j < n && !(sql.at(j) == c && (j + 1 >= n || sql.at(j + 1) != c))) {
                j += sql.at(j) == c ? 2 : 1;
            }
            out += sql.mid(i, j - i + 1);
            i = j;
            continue;
        }
        if (c == QLatin1Char(':') && i + 1 < n && (sql.at(i + 1).isLetter() || sql.at(i + 1) == QLatin1Char('_'))) {
            int j = i + 1;
            while (j < n && (sql.at(j).isLetterOrNumber() || sql.at(j) == QLatin1Char('_'))) {
                ++j;
            }
            const QString name = sql.mid(i + 1, j - i - 1);
            QueryParameter p{name, name, FieldType::Text};
            for (const QueryParameter &d : declared) {
                if (d.name == name) {
                    p = d;
                }
            }
            if (!values.contains(name)) {
                *error = QStringLiteral("Enter a value for %1.").arg(p.prompt);
                return {};
            }
            const Parsed parsed = parseValue(p.type, values.value(name), currency);
            if (!parsed.ok) {
                *error = QStringLiteral("%1 %2.").arg(p.prompt, parsed.error);
                return {};
            }
            out += sqlLiteral(parsed.value);
            i = j - 1;
            continue;
        }
        out += c;
    }
    return out;
}

} // namespace detail

qint64 countRows(const BaseFile &file, const CompiledQuery &compiled, const QVariantMap &parameters, QString *error)
{
    QString problem;
    Statement statement = file.db().prepare(QStringLiteral("SELECT count(*) FROM (%1)").arg(compiled.sql), &problem);
    if (!statement.isValid()) {
        *error = problem;
        return -1;
    }
    if (const Result r = detail::bindParameters(file, statement, compiled.parameters, parameters); !r.ok) {
        *error = r.error;
        return -1;
    }
    if (statement.step() != Statement::Step::Row) {
        *error = statement.errorText();
        return -1;
    }
    return statement.value(0).toLongLong();
}

SchemaPlan planMakeTable(const BaseFile &file, const CompiledQuery &compiled, const QVariantMap &parameters,
                         qint64 *rows)
{
    SchemaPlan plan;
    const QString target = compiled.makeTable;
    plan.label = QStringLiteral("Make table %1").arg(target);
    if (!compiled.ok || target.isEmpty()) {
        plan.error = compiled.ok ? QStringLiteral("This is not a make-table query.") : compiled.error;
        return plan;
    }
    QString error;
    const qint64 count = countRows(file, compiled, parameters, &error);
    if (count < 0) {
        plan.error = error;
        return plan;
    }
    if (rows != nullptr) {
        *rows = count;
    }
    // The output types: column origins, the compiler's hints, the first row.
    const QueryResult probe = runSelect(file, compiled.sql, compiled.parameters, parameters, 1, compiled.columns);
    if (!probe.ok) {
        plan.error = probe.error;
        return plan;
    }
    TableDef def;
    def.name = target;
    def.autoIncrement = false;
    QSet<QString> used;
    QStringList into;
    for (int i = 0; i < probe.columns.size(); ++i) {
        const OutputColumn &c = probe.columns.at(i);
        FieldDef f;
        f.name = validateName(c.name, QStringLiteral("field")).isEmpty() ? c.name : QStringLiteral("Field%1").arg(i + 1);
        for (int k = 2; used.contains(f.name.toLower()); ++k) {
            f.name = QStringLiteral("%1%2").arg(c.name).arg(k);
        }
        used.insert(f.name.toLower());
        // AutoNumbers copy as whole numbers (the new table numbers nothing);
        // a foreign column of no QindaBase type copies as text.
        f.type = c.type == FieldType::AutoNumber ? FieldType::Integer
                 : c.type == FieldType::Other    ? FieldType::Text
                                                 : c.type;
        def.fields.append(f);
        into.append(quoteIdentifier(f.name));
    }
    const bool replacing = file.tableExists(target);
    if (QString e = detail::validateTable(file, def, replacing ? target : QString()); !e.isEmpty()) {
        plan.error = e;
        return plan;
    }
    if (replacing) {
        const QList<Relationship> refs = detail::incoming(file, target);
        if (!refs.isEmpty()) {
            plan.error = QStringLiteral("%1 is on the one side of a relationship with %2, so the query cannot replace it.")
                             .arg(target, refs.first().childTable);
            return plan;
        }
        const qint64 old = file.db().scalar(QStringLiteral("SELECT count(*) FROM %1").arg(quoteIdentifier(target))).toLongLong();
        plan.statements.append(QStringLiteral("DROP TABLE %1").arg(quoteIdentifier(target)));
        if (file.hasSystemTables()) {
            plan.statements.append(QStringLiteral("DELETE FROM qb_fields WHERE table_name = %1").arg(quoteLiteral(target)));
        }
        plan.destructive = true;
        plan.notes.append(QStringLiteral("Deletes the table %1 and its %2 record(s) first. A backup is made.")
                              .arg(target)
                              .arg(old));
    }
    QString problem;
    const QString create = createTableSql(def, def.name, file.currency(), &problem);
    if (!problem.isEmpty()) {
        plan.error = problem;
        return plan;
    }
    plan.statements.append(create);
    if (file.hasSystemTables()) {
        plan.statements.append(detail::systemColumnStatements(file));
        for (const FieldDef &f : def.fields) {
            plan.statements.append(detail::metaInsert(def.name, f));
        }
    }
    const QString select = detail::inlineParameters(file, compiled.sql, compiled.parameters, parameters, &problem);
    if (!problem.isEmpty()) {
        plan.error = problem;
        return plan;
    }
    plan.statements.append(QStringLiteral("INSERT INTO %1(%2)\n%3")
                               .arg(quoteIdentifier(def.name), into.join(QStringLiteral(", ")), select));
    plan.notes.append(QStringLiteral("Creates the table %1 with %2 field(s) and the query's %3 record(s).")
                          .arg(def.name)
                          .arg(def.fields.size())
                          .arg(count));
    return plan;
}

} // namespace QindaOffice::Base
