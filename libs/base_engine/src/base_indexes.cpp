// SPDX-License-Identifier: GPL-3.0-or-later
// The Indexes dialog's indexes: reading them, their SQL, and the plan
// statements that change them. See base_indexes.h for the contract.
#include "base_indexes.h"

#include <QRegularExpression>
#include <QSet>

namespace QindaOffice::Base {

namespace {

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

bool isAutoIndex(const QString &sqlName)
{
    return sqlName.startsWith(QLatin1String("sqlite_autoindex_"), Qt::CaseInsensitive);
}

bool designedName(const QString &sqlName)
{
    return sqlName.startsWith(QLatin1String("qbi:"), Qt::CaseInsensitive);
}

bool sameIndex(const IndexDef &a, const IndexDef &b)
{
    if (a.unique != b.unique || a.ignoreNulls != b.ignoreNulls || a.fields.size() != b.fields.size()
        || a.name.compare(b.name, Qt::CaseSensitive) != 0) {
        return false;
    }
    for (int i = 0; i < a.fields.size(); ++i) {
        if (a.fields.at(i).field.compare(b.fields.at(i).field, Qt::CaseInsensitive) != 0
            || a.fields.at(i).descending != b.fields.at(i).descending) {
            return false;
        }
    }
    return true;
}

const IndexDef *byOriginal(const QList<IndexDef> &list, const QString &original)
{
    if (original.isEmpty()) {
        return nullptr;
    }
    for (const IndexDef &i : list) {
        if (i.originalName.compare(original, Qt::CaseInsensitive) == 0) {
            return &i;
        }
    }
    return nullptr;
}

// The columns a partial index's WHERE requires to be not Null, or nullopt
// when the WHERE says anything else.
std::optional<QSet<QString>> notNullColumns(const QString &sql)
{
    static const QRegularExpression where(QStringLiteral("\\)\\s*WHERE\\s+(.+)$"),
                                          QRegularExpression::CaseInsensitiveOption
                                              | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression term(QStringLiteral("^\\(*\\s*[\"`\\[]?(.+?)[\"`\\]]?\\s+IS\\s+NOT\\s+NULL\\s*\\)*$"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression conjunction(QStringLiteral("\\s+AND\\s+"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = where.match(sql);
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    QSet<QString> out;
    for (const QString &part : m.captured(1).trimmed().split(conjunction)) {
        const QRegularExpressionMatch t = term.match(part.trimmed());
        if (!t.hasMatch()) {
            return std::nullopt;
        }
        out.insert(t.captured(1).toLower());
    }
    return out;
}

} // namespace

QString indexSqlName(const QString &table, const QString &index)
{
    return QStringLiteral("qbi:%1:%2").arg(table, index);
}

QString indexDisplayName(const QString &table, const QString &sqlName)
{
    if (designedName(sqlName)) {
        const int second = int(sqlName.indexOf(QLatin1Char(':'), 4));
        return second < 0 ? sqlName.mid(4) : sqlName.mid(second + 1);
    }
    const QString imported = QStringLiteral("qbx_%1_").arg(table);
    if (sqlName.startsWith(imported, Qt::CaseInsensitive)) {
        return sqlName.mid(imported.size()); // M65's Access import named them so
    }
    return sqlName;
}

void readIndexes(const Connection &db, TableDef *table)
{
    for (const QVariantList &idx :
         db.rows(QStringLiteral("SELECT name, \"unique\", origin, partial FROM pragma_index_list(?)"), {table->name})) {
        const QString sqlName = idx.value(0).toString();
        const bool unique = idx.value(1).toInt() != 0;
        const QString origin = idx.value(2).toString();
        const bool partial = idx.value(3).toInt() != 0;
        if (origin == QLatin1String("pk")) {
            continue;
        }
        IndexDef def;
        bool plainColumns = true;
        for (const QVariantList &col : db.rows(QStringLiteral("SELECT name, \"desc\" FROM pragma_index_xinfo(?) "
                                                              "WHERE key = 1 ORDER BY seqno"),
                                               {sqlName})) {
            if (col.value(0).isNull()) {
                plainColumns = false; // an expression or the rowid
                break;
            }
            const FieldDef *f = table->field(col.value(0).toString());
            def.fields.append({f != nullptr ? f->name : col.value(0).toString(), col.value(1).toInt() != 0});
        }
        if (plainColumns && def.fields.size() == 1 && !partial && !def.fields.first().descending) {
            const int fi = table->fieldIndex(def.fields.first().field);
            if (fi >= 0) {
                FieldDef &f = table->fields[fi];
                (unique ? f.unique : f.indexed) = true;
                continue;
            }
        }
        const QString sql =
            db.scalar(QStringLiteral("SELECT sql FROM sqlite_schema WHERE type='index' AND name=?"), {sqlName}).toString();
        if (plainColumns && !def.fields.isEmpty() && (origin == QLatin1String("c") || origin == QLatin1String("u"))) {
            bool modelled = !partial;
            if (partial) {
                const std::optional<QSet<QString>> required = notNullColumns(sql);
                QSet<QString> columns;
                for (const IndexField &f : std::as_const(def.fields)) {
                    columns.insert(f.field.toLower());
                }
                modelled = required && *required == columns;
                def.ignoreNulls = modelled;
            }
            if (modelled) {
                def.originalName = sqlName;
                def.unique = unique;
                QStringList names;
                for (const IndexField &f : std::as_const(def.fields)) {
                    names.append(f.field);
                }
                def.name = isAutoIndex(sqlName) ? QStringLiteral("Unique %1").arg(names.join(QLatin1Char('+')))
                                                : indexDisplayName(table->name, sqlName);
                table->indexes.append(def);
                continue;
            }
        }
        if (origin == QLatin1String("c") && !sql.isEmpty()) {
            table->otherIndexSql.append(sql);
        }
    }
}

QString createIndexSql(const QString &tableName, const IndexDef &index)
{
    // A foreign index keeps its own name while the user keeps it.
    const bool foreign = !index.originalName.isEmpty() && !designedName(index.originalName)
                         && !isAutoIndex(index.originalName)
                         && indexDisplayName(tableName, index.originalName) == index.name;
    QStringList keys;
    QStringList notNull;
    for (const IndexField &f : index.fields) {
        keys.append(q(f.field) + (f.descending ? QStringLiteral(" DESC") : QString()));
        notNull.append(QStringLiteral("%1 IS NOT NULL").arg(q(f.field)));
    }
    QString sql = QStringLiteral("CREATE %1INDEX %2 ON %3(%4)")
                      .arg(index.unique ? QStringLiteral("UNIQUE ") : QString(),
                           q(foreign ? index.originalName : indexSqlName(tableName, index.name)), q(tableName),
                           keys.join(QStringLiteral(", ")));
    if (index.ignoreNulls) {
        sql += QStringLiteral(" WHERE ") + notNull.join(QStringLiteral(" AND "));
    }
    return sql;
}

QString validateIndexes(const TableDef &table)
{
    QSet<QString> names;
    for (const IndexDef &index : table.indexes) {
        if (QString e = validateName(index.name, QStringLiteral("index")); !e.isEmpty()) {
            return e;
        }
        if (names.contains(index.name.toLower())) {
            return QStringLiteral("Two indexes are named %1.").arg(index.name);
        }
        names.insert(index.name.toLower());
        if (index.fields.isEmpty()) {
            return QStringLiteral("The index %1 needs at least one field.").arg(index.name);
        }
        if (index.fields.size() > kIndexFieldLimit) {
            return QStringLiteral("The index %1 has more than %2 fields.").arg(index.name).arg(kIndexFieldLimit);
        }
        QSet<QString> used;
        for (const IndexField &f : index.fields) {
            const FieldDef *field = table.field(f.field);
            if (field == nullptr) {
                return QStringLiteral("The index %1 names a missing field %2.").arg(index.name, f.field);
            }
            if (field->type == FieldType::File) {
                return QStringLiteral("The index %1 cannot hold the file field %2.").arg(index.name, f.field);
            }
            if (used.contains(f.field.toLower())) {
                return QStringLiteral("The index %1 lists %2 twice.").arg(index.name, f.field);
            }
            used.insert(f.field.toLower());
        }
    }
    return {};
}

bool indexesNeedRebuild(const TableDef &before, const TableDef &after)
{
    for (const IndexDef &b : before.indexes) {
        if (!isAutoIndex(b.originalName)) {
            continue;
        }
        const IndexDef *a = byOriginal(after.indexes, b.originalName);
        if (a == nullptr || !sameIndex(*a, b)) {
            return true;
        }
    }
    return false;
}

QStringList indexStatements(const TableDef &before, const TableDef &after, bool rebuilt)
{
    QStringList out;
    if (rebuilt) {
        for (const IndexDef &a : after.indexes) {
            out.append(createIndexSql(after.name, a));
        }
        return out;
    }
    const bool renamed = before.name != after.name;
    for (const IndexDef &b : before.indexes) {
        const IndexDef *a = byOriginal(after.indexes, b.originalName);
        if (isAutoIndex(b.originalName)) {
            continue; // indexesNeedRebuild() sends any change to the rebuild
        }
        if (a == nullptr || !sameIndex(*a, b) || (renamed && designedName(b.originalName))) {
            out.append(QStringLiteral("DROP INDEX IF EXISTS %1").arg(q(b.originalName)));
        }
    }
    for (const IndexDef &a : after.indexes) {
        const IndexDef *b = byOriginal(before.indexes, a.originalName);
        if (b == nullptr || !sameIndex(a, *b) || (renamed && designedName(a.originalName))) {
            out.append(createIndexSql(after.name, a));
        }
    }
    return out;
}

QString uniqueIndexProblem(const Connection &db, const TableDef &before, const TableDef &after)
{
    if (before.name.isEmpty()) {
        return {}; // a new table has no records
    }
    for (const IndexDef &a : after.indexes) {
        const IndexDef *b = byOriginal(before.indexes, a.originalName);
        if (!a.unique || (b != nullptr && b->unique && sameIndex(a, *b))) {
            continue;
        }
        QStringList columns;
        QStringList notNull;
        bool newField = false;
        for (const IndexField &f : a.fields) {
            const FieldDef *field = after.field(f.field);
            if (field == nullptr || field->originalName.isEmpty()) {
                newField = true;
                break;
            }
            // The check reads the table as it is now, so by the old names (a
            // retyped or recalculated field is checked again by SQLite itself
            // when the plan's CREATE UNIQUE INDEX runs in the preview).
            columns.append(q(field->originalName));
            notNull.append(q(field->originalName) + QStringLiteral(" IS NOT NULL"));
        }
        if (newField || columns.isEmpty()) {
            continue; // a new field is empty everywhere, so it repeats nothing
        }
        const qint64 repeats = db.scalar(QStringLiteral("SELECT count(*) FROM (SELECT 1 FROM %1 WHERE %2 GROUP BY %3 "
                                                        "HAVING count(*) > 1)")
                                             .arg(q(before.name), notNull.join(QStringLiteral(" AND ")),
                                                  columns.join(QStringLiteral(", "))))
                                   .toLongLong();
        if (repeats > 0) {
            return QStringLiteral("%1 set(s) of values repeat in %2, so the index %3 cannot be unique.")
                .arg(repeats)
                .arg(columns.join(QStringLiteral(" + ")).remove(QLatin1Char('"')), a.name);
        }
    }
    return {};
}

} // namespace QindaOffice::Base
