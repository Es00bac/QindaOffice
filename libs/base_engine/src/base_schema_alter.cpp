// SPDX-License-Identifier: GPL-3.0-or-later
// planAlterTable: renames first (plain ALTER, which rewrites references),
// then either simple ADD COLUMN / CREATE INDEX steps or the 12-step table
// rebuild. See base_schema.h.
#include "base_rules.h"
#include "base_schema_p.h"

#include <QSet>

namespace QindaOffice::Base {

using namespace detail;

namespace {

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

bool sameRelationships(const QList<Relationship> &a, const QList<Relationship> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (const Relationship &r : a) {
        const auto match = std::find_if(b.cbegin(), b.cend(), [&](const Relationship &o) {
            return o.sameLink(r) && o.onDelete == r.onDelete && o.onUpdate == r.onUpdate;
        });
        if (match == b.cend()) {
            return false;
        }
    }
    return true;
}

// A rename that only changes letter case must pass through a temporary
// name: SQLite compares names without case.
void appendRename(QStringList *out, const QString &from, const QString &to, const QString &tableForColumn)
{
    if (tableForColumn.isEmpty()) {
        if (from.compare(to, Qt::CaseInsensitive) == 0) {
            const QString temp = QStringLiteral("qb_rename_") + to;
            out->append(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(q(from), q(temp)));
            out->append(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(q(temp), q(to)));
        } else {
            out->append(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(q(from), q(to)));
        }
        return;
    }
    const QString table = q(tableForColumn);
    if (from.compare(to, Qt::CaseInsensitive) == 0) {
        const QString temp = QStringLiteral("qb_rename_") + to;
        out->append(QStringLiteral("ALTER TABLE %1 RENAME COLUMN %2 TO %3").arg(table, q(from), q(temp)));
        out->append(QStringLiteral("ALTER TABLE %1 RENAME COLUMN %2 TO %3").arg(table, q(temp), q(to)));
    } else {
        out->append(QStringLiteral("ALTER TABLE %1 RENAME COLUMN %2 TO %3").arg(table, q(from), q(to)));
    }
}

} // namespace

SchemaPlan planAlterTable(const BaseFile &file, const TableDef &before, const TableDef &input)
{
    SchemaPlan plan;
    TableDef after = input;
    plan.label = before.name == after.name ? QStringLiteral("Change table %1").arg(after.name)
                                           : QStringLiteral("Rename table %1 to %2").arg(before.name, after.name);
    for (Relationship &r : after.relationships) {
        if (r.childTable.compare(before.name, Qt::CaseInsensitive) == 0) {
            r.childTable = after.name;
        }
        if (r.parentTable.compare(before.name, Qt::CaseInsensitive) == 0) {
            r.parentTable = after.name;
        }
    }
    if (QString e = validateTable(file, after, before.name); !e.isEmpty()) {
        plan.error = e;
        return plan;
    }
    const CurrencyFormat currency = file.currency();
    if (QString e = validateRules(after, currency); !e.isEmpty()) {
        plan.error = e;
        return plan;
    }
    const Connection &db = file.db();

    // Which old field each new one comes from.
    QHash<QString, const FieldDef *> oldByName;
    for (const FieldDef &f : before.fields) {
        oldByName.insert(f.name.toLower(), &f);
    }
    QSet<QString> kept;
    for (const FieldDef &f : after.fields) {
        if (!f.originalName.isEmpty()) {
            if (!oldByName.contains(f.originalName.toLower())) {
                plan.error = QStringLiteral("%1 names a field %2 the table does not have.").arg(f.name, f.originalName);
                return plan;
            }
            if (kept.contains(f.originalName.toLower())) {
                plan.error = QStringLiteral("Two fields come from %1.").arg(f.originalName);
                return plan;
            }
            kept.insert(f.originalName.toLower());
        }
    }
    QStringList dropped;
    for (const FieldDef &f : before.fields) {
        if (!kept.contains(f.name.toLower())) {
            dropped.append(f.name);
        }
    }
    // A field another table's relationship points at must stay.
    for (const Relationship &r : incoming(file, before.name)) {
        if (dropped.contains(r.parentField, Qt::CaseInsensitive)) {
            plan.error = QStringLiteral("%1 is on the one side of the relationship with %2; delete that relationship "
                                        "first.")
                             .arg(r.parentField, r.childTable);
            return plan;
        }
    }

    const QString table = q(before.name);
    bool rebuild = !dropped.isEmpty() || !sameRelationships(before.relationships, after.relationships);
    QStringList survivingOrder;
    for (const FieldDef &f : after.fields) {
        if (!f.originalName.isEmpty()) {
            survivingOrder.append(f.originalName.toLower());
        }
    }
    QStringList beforeOrder;
    for (const FieldDef &f : before.fields) {
        if (kept.contains(f.name.toLower())) {
            beforeOrder.append(f.name.toLower());
        }
    }
    rebuild = rebuild || survivingOrder != beforeOrder;
    bool seenNew = false;
    QList<const FieldDef *> added;
    for (const FieldDef &f : after.fields) {
        if (f.originalName.isEmpty()) {
            seenNew = true;
            added.append(&f);
            const QString dflt = f.defaultValue.trimmed();
            const bool expressionDefault = dflt.startsWith(QLatin1Char('=')) || dflt.endsWith(QStringLiteral("()"));
            rebuild = rebuild || f.primaryKey || f.required || expressionDefault;
            continue;
        }
        rebuild = rebuild || seenNew; // a new field in the middle
        const FieldDef &old = *oldByName.value(f.originalName.toLower());
        if (f.type != old.type || f.required != old.required || f.primaryKey != old.primaryKey
            || f.defaultValue.trimmed() != old.defaultValue.trimmed() || (old.unique && !f.unique)
            || (old.indexed && !f.indexed && !f.unique)) {
            rebuild = true;
        }
        // A foreign BLOB column (Other) may become a file field; nothing else
        // converts to or from one (bytes are not text).
        if (f.type != old.type && (f.type == FieldType::File || old.type == FieldType::File)
            && !(old.type == FieldType::Other && f.type == FieldType::File)) {
            plan.error = QStringLiteral("%1 cannot change between a file field and another type; add a new field "
                                        "instead.")
                             .arg(f.name);
            return plan;
        }
        if (f.required && !old.required) {
            const qint64 blanks =
                db.scalar(QStringLiteral("SELECT count(*) FROM %1 WHERE %2 IS NULL").arg(table, q(old.name))).toLongLong();
            if (blanks > 0) {
                plan.error = QStringLiteral("%1 record(s) have no %2 yet. Fill them in before making it required.")
                                 .arg(blanks)
                                 .arg(f.name);
                return plan;
            }
        }
        if ((f.unique || (f.primaryKey && !old.primaryKey)) && !old.unique && !old.primaryKey) {
            const qint64 dupes = db.scalar(QStringLiteral("SELECT count(*) FROM (SELECT %1 FROM %2 WHERE %1 IS NOT NULL "
                                                          "GROUP BY %1 HAVING count(*) > 1)")
                                               .arg(q(old.name), table))
                                     .toLongLong();
            if (dupes > 0) {
                plan.error = QStringLiteral("%1 has %2 value(s) used more than once, so it cannot be unique.")
                                 .arg(f.name)
                                 .arg(dupes);
                return plan;
            }
        }
        if (lossy(old.type, f.type)) {
            plan.destructive = true;
            plan.notes.append(QStringLiteral("%1 changes from %2 to %3 and may lose detail.")
                                  .arg(f.name, typeLabel(old.type), typeLabel(f.type)));
        } else if (f.type != old.type) {
            plan.notes.append(QStringLiteral("%1 changes from %2 to %3.").arg(f.name, typeLabel(old.type), typeLabel(f.type)));
        }
    }
    for (const QString &name : dropped) {
        const qint64 filled =
            db.scalar(QStringLiteral("SELECT count(*) FROM %1 WHERE %2 IS NOT NULL").arg(table, q(name))).toLongLong();
        plan.destructive = true;
        plan.notes.append(QStringLiteral("Deletes the field %1 (%2 value(s)).").arg(name).arg(filled));
    }

    // Phase A: renames (plain ALTER rewrites every reference to them).
    bool renamedColumns = false;
    if (before.name != after.name) {
        appendRename(&plan.statements, before.name, after.name, QString());
        plan.notes.append(QStringLiteral("Renames the table to %1.").arg(after.name));
    }
    for (const FieldDef &f : after.fields) {
        if (!f.originalName.isEmpty() && f.originalName != f.name) {
            appendRename(&plan.statements, f.originalName, f.name, after.name);
            plan.notes.append(QStringLiteral("Renames %1 to %2.").arg(f.originalName, f.name));
            renamedColumns = true;
        }
    }
    const QString target = q(after.name);
    if (rebuild && renamedColumns && (!before.otherIndexSql.isEmpty() || !before.triggerSql.isEmpty())) {
        plan.error = QStringLiteral("%1 has its own indexes or triggers: rename fields in a separate step from other "
                                    "changes.")
                         .arg(after.name);
        return plan;
    }

    if (!rebuild) {
        // Phase B (simple): new columns at the end, new indexes.
        for (const FieldDef *f : added) {
            QString error;
            const QString column = columnSql(*f, false, false, currency, &error);
            if (!error.isEmpty()) {
                plan.error = error;
                return plan;
            }
            plan.statements.append(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(target, column));
            plan.notes.append(QStringLiteral("Adds the field %1 (%2).").arg(f->name, typeLabel(f->type)));
        }
        for (const FieldDef &f : after.fields) {
            const FieldDef *old = f.originalName.isEmpty() ? nullptr : oldByName.value(f.originalName.toLower());
            const QString index = q(QStringLiteral("qbx_%1_%2").arg(after.name, f.name));
            if (f.unique && (old == nullptr || !old->unique) && !f.primaryKey) {
                plan.statements.append(
                    QStringLiteral("CREATE UNIQUE INDEX %1 ON %2(%3)").arg(index, target, q(f.name)));
            } else if (f.indexed && !f.unique && (old == nullptr || (!old->indexed && !old->unique)) && !f.primaryKey) {
                plan.statements.append(QStringLiteral("CREATE INDEX %1 ON %2(%3)").arg(index, target, q(f.name)));
            }
        }
    } else {
        // Phase B (rebuild): sqlite.org/lang_altertable.html §7.
        plan.legacyAlter = true;
        plan.statements.append(QStringLiteral("PRAGMA legacy_alter_table=ON"));
        const QString temp = QStringLiteral("qb_rebuild_") + after.name;
        QString error;
        const QString create = createTableSql(after, temp, currency, &error);
        if (!error.isEmpty()) {
            plan.error = error;
            return plan;
        }
        plan.statements.append(create);
        QStringList into;
        QStringList from;
        for (const FieldDef &f : after.fields) {
            if (f.originalName.isEmpty()) {
                continue;
            }
            const FieldDef &old = *oldByName.value(f.originalName.toLower());
            into.append(q(f.name));
            if (old.type == f.type) {
                from.append(q(f.name));
            } else {
                from.append(QStringLiteral("qb_convert(%1, %2, %3)")
                                .arg(q(f.name), quoteLiteral(typeId(old.type)), quoteLiteral(typeId(f.type))));
                QString check = QStringLiteral("SELECT count(*) FROM %1 WHERE %2 IS NOT NULL AND typeof(%2) NOT IN (%3)")
                                    .arg(target, q(f.name), expectedStorage(f.type));
                if (f.type == FieldType::Date) {
                    check += QStringLiteral(" OR (typeof(%1)='text' AND %1 NOT GLOB "
                                            "'[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]')")
                                 .arg(q(f.name));
                }
                plan.checks.append({check, QStringLiteral("%1 value(s) in %2 don't fit %3 and are kept as they were.")
                                               .arg(QStringLiteral("%1"), f.name, typeLabel(f.type))});
            }
        }
        if (!into.isEmpty()) {
            plan.statements.append(QStringLiteral("INSERT INTO %1(%2) SELECT %3 FROM %4")
                                       .arg(q(temp), into.join(QStringLiteral(", ")), from.join(QStringLiteral(", ")),
                                            target));
        }
        plan.statements.append(QStringLiteral("DROP TABLE %1").arg(target));
        plan.statements.append(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(q(temp), target));
        appendFieldIndexes(&plan.statements, after, after.name);
        plan.statements.append(after.otherIndexSql);
        plan.statements.append(after.triggerSql);
        for (const FieldDef *f : added) {
            plan.notes.append(QStringLiteral("Adds the field %1 (%2).").arg(f->name, typeLabel(f->type)));
        }
        if (!sameRelationships(before.relationships, after.relationships)) {
            plan.notes.append(QStringLiteral("Updates the relationships of %1.").arg(after.name));
        }
        plan.notes.append(QStringLiteral("Rebuilds %1 and copies its records.").arg(after.name));
    }
    if (file.hasSystemTables()) {
        plan.statements.append(systemColumnStatements(file));
        plan.statements.append(QStringLiteral("DELETE FROM qb_fields WHERE table_name IN (%1, %2)")
                                   .arg(quoteLiteral(before.name), quoteLiteral(after.name)));
        for (const FieldDef &f : after.fields) {
            plan.statements.append(metaInsert(after.name, f));
        }
        if (const QString own = metaTableInsert(after); !own.isEmpty()) {
            plan.statements.append(own);
        }
    }
    appendRuleStatements(&plan, file, before, after, rebuild);
    if (plan.statements.isEmpty()) {
        plan.notes.append(QStringLiteral("Nothing to change."));
    }
    return plan;
}

} // namespace QindaOffice::Base
