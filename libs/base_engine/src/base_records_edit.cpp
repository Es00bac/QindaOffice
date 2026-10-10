// SPDX-License-Identifier: GPL-3.0-or-later
// Records: edits (cells, new records, deletes, paste) as captured Changes.
#include "base_records.h"

#include <algorithm>

namespace QindaOffice::Base {

namespace {

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

const ColumnInfo *findColumn(const QList<ColumnInfo> &columns, const QString &name)
{
    for (const ColumnInfo &c : columns) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &c;
        }
    }
    return nullptr;
}

QString rowidList(const QList<qint64> &rowids)
{
    QStringList parts;
    for (const qint64 id : rowids) {
        parts.append(QString::number(id));
    }
    return parts.join(QLatin1Char(','));
}

// A validated value for one cell, or an error naming the field.
Result prepare(const BaseFile &file, const ColumnInfo *column, const QString &field, const QVariant &input, QVariant *out)
{
    if (column == nullptr) {
        return Result::failure(QStringLiteral("There is no field %1.").arg(field));
    }
    if (column->readOnly) {
        // M86: a calculated field is worked out; a linked table follows its source.
        return Result::failure(column->type == FieldType::AutoNumber
                                   ? QStringLiteral("%1 is numbered by the database.").arg(column->label())
                               : column->calculated ? QStringLiteral("%1 is calculated from its expression.").arg(column->label())
                               : column->linked
                                   ? QStringLiteral("%1 belongs to a linked table; change its source, then refresh the link.")
                                         .arg(column->label())
                                   : QStringLiteral("%1 cannot be edited here.").arg(column->label()));
    }
    const Parsed parsed = Records::resolveInput(file, *column, input);
    if (!parsed.ok) {
        return Result::failure(QStringLiteral("%1 %2.").arg(column->label(), parsed.error));
    }
    if (column->required && !parsed.value.isValid()) {
        return Result::failure(QStringLiteral("%1 is required.").arg(column->label()));
    }
    *out = parsed.value;
    return Result::success();
}

} // namespace

Result Records::updateCells(BaseFile &file, const QString &table, const QList<CellEdit> &edits, Change *change)
{
    const QList<ColumnInfo> cols = columns(file, table);
    QList<QPair<CellEdit, QVariant>> ready;
    for (const CellEdit &edit : edits) {
        QVariant value;
        const Result r = prepare(file, findColumn(cols, edit.field), edit.field, edit.input, &value);
        if (!r.ok) {
            return r;
        }
        ready.append({edit, value});
    }
    const QString label = edits.size() == 1 ? QStringLiteral("Edit %1").arg(edits.first().field)
                                            : QStringLiteral("Edit %1 cells").arg(edits.size());
    const Result r = file.capture(label, [&]() -> Result {
        for (const auto &[edit, value] : ready) {
            const Result run = file.db().run(
                QStringLiteral("UPDATE %1 SET %2 = ? WHERE rowid = ?").arg(q(table), q(findColumn(cols, edit.field)->name)),
                {value, edit.rowid});
            if (!run.ok) {
                return Result::failure(explain(run.error, cols, table));
            }
        }
        return Result::success();
    }, change);
    return r;
}

Result Records::insertRecord(BaseFile &file, const QString &table, const QVariantMap &values, qint64 *rowid,
                             Change *change)
{
    const QList<ColumnInfo> cols = columns(file, table);
    QStringList names;
    QVariantList bindings;
    QStringList missing;
    for (const ColumnInfo &c : cols) {
        QVariant input;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (it.key().compare(c.name, Qt::CaseInsensitive) == 0) {
                input = it.value();
            }
        }
        const bool given = input.isValid() && !input.isNull() && !input.toString().trimmed().isEmpty();
        if (!given) {
            if (c.required && c.defaultValue.isEmpty() && !c.readOnly) {
                missing.append(c.label());
            }
            continue;
        }
        QVariant value;
        const Result r = prepare(file, &c, c.name, input, &value);
        if (!r.ok) {
            return r;
        }
        names.append(q(c.name));
        bindings.append(value);
    }
    if (!missing.isEmpty()) {
        return Result::failure(QStringLiteral("Fill in %1 to save the record.")
                                   .arg(missing.size() == 1 ? missing.first()
                                                            : missing.mid(0, missing.size() - 1).join(QStringLiteral(", "))
                                                                  + QStringLiteral(" and ") + missing.last()));
    }
    return file.capture(QStringLiteral("New record in %1").arg(table), [&]() -> Result {
        QStringList marks(names.size(), QStringLiteral("?"));
        const QString sql = names.isEmpty()
                                ? QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(q(table))
                                : QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
                                      .arg(q(table), names.join(QStringLiteral(", ")), marks.join(QStringLiteral(", ")));
        const Result run = file.db().run(sql, bindings);
        if (!run.ok) {
            return Result::failure(explain(run.error, cols, table));
        }
        if (rowid != nullptr) {
            *rowid = file.db().lastInsertRowId();
        }
        return Result::success();
    }, change);
}

QStringList Records::deleteImpact(const BaseFile &file, const QString &table, const QList<qint64> &rowids,
                                  bool *blocked)
{
    QStringList out;
    if (blocked != nullptr) {
        *blocked = false;
    }
    for (const Relationship &r : readRelationships(file)) {
        if (r.parentTable.compare(table, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const qint64 count =
            file.db()
                .scalar(QStringLiteral("SELECT count(*) FROM %1 WHERE %2 IN (SELECT %3 FROM %4 WHERE rowid IN (%5))")
                            .arg(q(r.childTable), q(r.childField), q(r.parentField), q(table), rowidList(rowids)))
                .toLongLong();
        if (count == 0) {
            continue;
        }
        switch (r.onDelete) {
        case RefAction::Cascade:
            out.append(QStringLiteral("Also deletes %1 related record(s) in %2.").arg(count).arg(r.childTable));
            break;
        case RefAction::SetNull:
            out.append(QStringLiteral("%1 record(s) in %2 will no longer point to a %3 record.")
                           .arg(count)
                           .arg(r.childTable, table));
            break;
        default:
            out.append(QStringLiteral("%1 record(s) in %2 still refer to these; delete or reassign them first.")
                           .arg(count)
                           .arg(r.childTable));
            if (blocked != nullptr) {
                *blocked = true;
            }
            break;
        }
    }
    return out;
}

Result Records::deleteRecords(BaseFile &file, const QString &table, const QList<qint64> &rowids, Change *change)
{
    if (rowids.isEmpty()) {
        return Result::success();
    }
    bool blocked = false;
    const QStringList impact = deleteImpact(file, table, rowids, &blocked);
    if (blocked) {
        return Result::failure(impact.join(QLatin1Char(' ')));
    }
    const QList<ColumnInfo> cols = columns(file, table);
    const QString label = rowids.size() == 1 ? QStringLiteral("Delete record")
                                             : QStringLiteral("Delete %1 records").arg(rowids.size());
    return file.capture(label, [&]() -> Result {
        const Result run =
            file.db().run(QStringLiteral("DELETE FROM %1 WHERE rowid IN (%2)").arg(q(table), rowidList(rowids)));
        return run.ok ? run : Result::failure(explain(run.error, cols, table));
    }, change);
}

QList<QStringList> Records::parseTabular(const QString &text)
{
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;
    bool atCellStart = true;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell.append(QLatin1Char('"'));
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('"') && atCellStart) {
            quoted = true;
            atCellStart = false;
        } else if (ch == QLatin1Char('\t')) {
            row.append(cell);
            cell.clear();
            atCellStart = true;
        } else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            row.append(cell);
            rows.append(row);
            row.clear();
            cell.clear();
            atCellStart = true;
        } else {
            cell.append(ch);
            atCellStart = false;
        }
    }
    if (!cell.isEmpty() || !row.isEmpty()) {
        row.append(cell);
        rows.append(row);
    }
    return rows;
}

Result Records::pasteText(BaseFile &file, const QString &table, const QList<qint64> &rowOrder, int startRow,
                          const QString &startColumn, const QString &text, Change *change, int *updated, int *inserted)
{
    const QList<QStringList> grid = parseTabular(text);
    if (grid.isEmpty()) {
        return Result::failure(QStringLiteral("The clipboard holds no rows to paste."));
    }
    const QList<ColumnInfo> cols = columns(file, table);
    QList<const ColumnInfo *> targets;
    bool started = false;
    for (const ColumnInfo &c : cols) {
        started = started || c.name.compare(startColumn, Qt::CaseInsensitive) == 0;
        if (started && !c.readOnly) {
            targets.append(&c);
        }
    }
    if (targets.isEmpty()) {
        return Result::failure(QStringLiteral("There is no editable field to paste into."));
    }
    QList<CellEdit> edits;
    QList<QVariantMap> additions;
    for (int r = 0; r < grid.size(); ++r) {
        const QStringList &cells = grid.at(r);
        const int at = startRow + r;
        QVariantMap record;
        for (int c = 0; c < cells.size() && c < targets.size(); ++c) {
            if (at < rowOrder.size()) {
                edits.append({rowOrder.at(at), targets.at(c)->name, cells.at(c)});
            } else {
                record.insert(targets.at(c)->name, cells.at(c));
            }
        }
        if (at >= rowOrder.size()) {
            additions.append(record);
        }
    }
    // Validate everything first so a bad cell names its row.
    for (const CellEdit &e : edits) {
        QVariant value;
        const Result r = prepare(file, findColumn(cols, e.field), e.field, e.input, &value);
        if (!r.ok) {
            return Result::failure(QStringLiteral("Row %1: %2").arg(rowOrder.indexOf(e.rowid) - startRow + 1).arg(r.error));
        }
    }
    const Result r = file.capture(QStringLiteral("Paste %1 row(s)").arg(grid.size()), [&]() -> Result {
        for (const CellEdit &e : edits) {
            QVariant value;
            Result ok = prepare(file, findColumn(cols, e.field), e.field, e.input, &value);
            if (ok.ok) {
                ok = file.db().run(QStringLiteral("UPDATE %1 SET %2 = ? WHERE rowid = ?").arg(q(table), q(e.field)),
                                   {value, e.rowid});
            }
            if (!ok.ok) {
                return Result::failure(explain(ok.error, cols, table));
            }
        }
        for (int i = 0; i < additions.size(); ++i) {
            // A nested capture would open a second session; insert directly.
            QStringList names;
            QVariantList bindings;
            for (auto it = additions.at(i).cbegin(); it != additions.at(i).cend(); ++it) {
                QVariant value;
                const Result ok = prepare(file, findColumn(cols, it.key()), it.key(), it.value(), &value);
                if (!ok.ok) {
                    return Result::failure(QStringLiteral("New row %1: %2").arg(i + 1).arg(ok.error));
                }
                if (value.isValid()) {
                    names.append(q(it.key()));
                    bindings.append(value);
                }
            }
            QStringList marks(names.size(), QStringLiteral("?"));
            const Result run = file.db().run(
                names.isEmpty() ? QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(q(table))
                                : QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
                                      .arg(q(table), names.join(QStringLiteral(", ")), marks.join(QStringLiteral(", "))),
                bindings);
            if (!run.ok) {
                return Result::failure(QStringLiteral("New row %1: %2").arg(i + 1).arg(explain(run.error, cols, table)));
            }
        }
        return Result::success();
    }, change);
    if (r.ok) {
        if (updated != nullptr) {
            *updated = int(std::min<qsizetype>(grid.size(), std::max<qsizetype>(0, rowOrder.size() - startRow)));
        }
        if (inserted != nullptr) {
            *inserted = int(additions.size());
        }
    }
    return r;
}

} // namespace QindaOffice::Base
