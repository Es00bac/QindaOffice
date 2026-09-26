// SPDX-License-Identifier: GPL-3.0-or-later
// Running queries: inspecting hand-written SQL, selects with typed output
// columns, and action queries that only run with a confirmed count.
#include "base_query.h"

#include <QHash>

namespace QindaOffice::Base {

namespace {

const QStringList kSchemaKeywords{QStringLiteral("CREATE"), QStringLiteral("ALTER"), QStringLiteral("DROP"),
                                  QStringLiteral("REINDEX")};
const QStringList kRefused{QStringLiteral("PRAGMA"), QStringLiteral("ATTACH"), QStringLiteral("DETACH"),
                           QStringLiteral("VACUUM"), QStringLiteral("BEGIN"), QStringLiteral("COMMIT"),
                           QStringLiteral("END"), QStringLiteral("ROLLBACK"), QStringLiteral("SAVEPOINT"),
                           QStringLiteral("RELEASE"), QStringLiteral("ANALYZE")};

QString bareName(const QString &raw, int index)
{
    if (raw.isEmpty()) {
        return QStringLiteral("param%1").arg(index);
    }
    QString name = raw.mid(1);
    return name.isEmpty() || name.at(0).isDigit() ? QStringLiteral("param%1").arg(index) : name;
}

Result bindParameters(const BaseFile &file, Statement &statement, const QList<QueryParameter> &declared,
                      const QVariantMap &values)
{
    const CurrencyFormat currency = file.currency();
    for (int i = 1; i <= statement.parameterCount(); ++i) {
        const QString name = bareName(statement.parameterName(i), i);
        QueryParameter p{name, name, FieldType::Text};
        for (const QueryParameter &d : declared) {
            if (d.name == name) {
                p = d;
            }
        }
        if (!values.contains(name)) {
            return Result::failure(QStringLiteral("Enter a value for %1.").arg(p.prompt));
        }
        const QVariant input = values.value(name);
        const Parsed parsed = parseValue(p.type, input, currency);
        if (!parsed.ok) {
            return Result::failure(QStringLiteral("%1 %2.").arg(p.prompt, parsed.error));
        }
        // Text parameters of an untyped SQL query bind as text; SQLite's
        // column affinity compares them numerically where it should.
        if (!statement.bind(i, parsed.value)) {
            return Result::failure(statement.errorText());
        }
    }
    return Result::success();
}

Statement prepareOne(const BaseFile &file, const QString &sql, QString *error)
{
    QString splitError;
    const QStringList statements = splitStatements(sql, &splitError);
    if (!splitError.isEmpty()) {
        *error = splitError;
        return {};
    }
    if (statements.size() != 1) {
        *error = statements.isEmpty() ? QStringLiteral("Write one SQL statement.")
                                      : QStringLiteral("A query runs one statement at a time (found %1).")
                                            .arg(statements.size());
        return {};
    }
    return file.db().prepare(statements.first(), error);
}

} // namespace

SqlInfo inspectSql(const BaseFile &file, const QString &sql)
{
    SqlInfo info;
    const QString keyword = firstKeyword(sql);
    info.keyword = keyword;
    if (kRefused.contains(keyword)) {
        info.error = QStringLiteral("%1 statements are not allowed in a query.").arg(keyword);
        return info;
    }
    info.schema = kSchemaKeywords.contains(keyword);
    if (info.schema) {
        info.ok = true;
        info.readOnly = false;
        return info;
    }
    QString error;
    Statement statement = prepareOne(file, sql, &error);
    if (!statement.isValid()) {
        info.error = error;
        return info;
    }
    info.readOnly = statement.isReadOnly();
    for (int i = 1; i <= statement.parameterCount(); ++i) {
        const QString name = bareName(statement.parameterName(i), i);
        if (std::none_of(info.parameters.cbegin(), info.parameters.cend(),
                         [&](const QueryParameter &p) { return p.name == name; })) {
            QString prompt = name;
            prompt.replace(QLatin1Char('_'), QLatin1Char(' '));
            info.parameters.append({name, prompt, FieldType::Text});
        }
    }
    info.ok = true;
    return info;
}

QueryResult runSelect(const BaseFile &file, const QString &sql, const QList<QueryParameter> &declared,
                      const QVariantMap &parameters, int limit, const QList<OutputColumn> &hints)
{
    QueryResult result;
    QString error;
    Statement statement = prepareOne(file, sql, &error);
    if (!statement.isValid()) {
        result.error = error;
        return result;
    }
    if (!statement.isReadOnly()) {
        result.error = QStringLiteral("This query changes data; run it as an action query.");
        return result;
    }
    const Result bound = bindParameters(file, statement, declared, parameters);
    if (!bound.ok) {
        result.error = bound.error;
        return result;
    }
    QHash<QString, QHash<QString, FieldType>> typesByTable;
    const int count = statement.columnCount();
    for (int c = 0; c < count; ++c) {
        OutputColumn col;
        col.name = statement.columnName(c);
        col.sourceTable = statement.originTable(c);
        col.sourceField = statement.originColumn(c);
        if (!col.sourceTable.isEmpty()) {
            if (!typesByTable.contains(col.sourceTable)) {
                typesByTable.insert(col.sourceTable, fieldTypes(file, col.sourceTable));
            }
            const QHash<QString, FieldType> &types = typesByTable[col.sourceTable];
            if (types.contains(col.sourceField)) {
                col.type = types.value(col.sourceField);
                col.typeKnown = true;
            }
        }
        result.columns.append(col);
    }
    Statement::Step step = statement.step();
    while (step == Statement::Step::Row) {
        if (result.rows.size() >= limit) {
            result.truncated = true;
            break;
        }
        QVariantList row;
        row.reserve(count);
        for (int c = 0; c < count; ++c) {
            row.append(statement.value(c));
        }
        result.rows.append(row);
        step = statement.step();
    }
    if (step == Statement::Step::Error) {
        result.error = statement.errorText();
        return result;
    }
    // Columns without an origin (expressions) take the type of their values.
    for (int c = 0; c < count; ++c) {
        OutputColumn &col = result.columns[c];
        if (col.typeKnown) {
            continue;
        }
        const auto hint = std::find_if(hints.cbegin(), hints.cend(),
                                       [&](const OutputColumn &h) { return h.name == col.name && h.typeKnown; });
        if (hint != hints.cend()) {
            col.type = hint->type;
            col.typeKnown = true;
            continue;
        }
        for (const QVariantList &row : std::as_const(result.rows)) {
            const QVariant v = row.at(c);
            if (!v.isValid()) {
                continue;
            }
            col.type = v.metaType().id() == QMetaType::LongLong ? FieldType::Integer
                       : v.metaType().id() == QMetaType::Double ? FieldType::Number
                                                                : FieldType::Text;
            break;
        }
    }
    result.ok = true;
    return result;
}

Result previewAction(BaseFile &file, const QString &sql, const QList<QueryParameter> &declared,
                     const QVariantMap &parameters, qint64 *affected)
{
    QString error;
    Statement statement = prepareOne(file, sql, &error);
    if (!statement.isValid()) {
        return Result::failure(error);
    }
    if (statement.isReadOnly()) {
        return Result::failure(QStringLiteral("This query does not change data."));
    }
    const Result bound = bindParameters(file, statement, declared, parameters);
    if (!bound.ok) {
        return bound;
    }
    Savepoint sp(file.db(), QStringLiteral("qb_preview"));
    if (!sp.isActive()) {
        return Result::failure(sp.error());
    }
    if (statement.step() == Statement::Step::Error) {
        const QString message = statement.errorText();
        statement = Statement(); // finalize before the rollback
        sp.rollback();
        return Result::failure(message);
    }
    if (affected != nullptr) {
        *affected = file.db().changes();
    }
    statement = Statement();
    sp.rollback();
    return Result::success();
}

Result runAction(BaseFile &file, const QString &sql, const QList<QueryParameter> &declared, const QVariantMap &parameters,
                 qint64 confirmedCount, Change *change, qint64 *affected)
{
    QString error;
    Statement statement = prepareOne(file, sql, &error);
    if (!statement.isValid()) {
        return Result::failure(error);
    }
    const Result bound = bindParameters(file, statement, declared, parameters);
    if (!bound.ok) {
        return bound;
    }
    qint64 changed = 0;
    const Result r = file.capture(QStringLiteral("Run action query"), [&]() -> Result {
        if (statement.step() == Statement::Step::Error) {
            const QString message = statement.errorText();
            statement = Statement();
            return Result::failure(message);
        }
        changed = file.db().changes();
        statement = Statement();
        if (confirmedCount >= 0 && changed != confirmedCount) {
            return Result::failure(QStringLiteral("The data changed since you confirmed: %1 record(s) would change now, "
                                                  "not %2. Nothing was changed.")
                                       .arg(changed)
                                       .arg(confirmedCount));
        }
        return Result::success();
    }, change);
    if (r.ok && affected != nullptr) {
        *affected = changed;
    }
    return r;
}

} // namespace QindaOffice::Base
