// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_snapshot.h"

#include "base_records.h"

#include <cmath>

namespace QindaOffice::Base {

namespace {

QString snapshotType(FieldType type, bool lookup)
{
    if (lookup) {
        return QStringLiteral("TEXT");
    }
    switch (type) {
    case FieldType::AutoNumber:
    case FieldType::Integer:
        return QStringLiteral("INTEGER");
    case FieldType::Number:
    case FieldType::Currency:
        return QStringLiteral("REAL");
    case FieldType::YesNo:
        return QStringLiteral("BOOLEAN");
    case FieldType::Date:
        return QStringLiteral("DATE");
    case FieldType::DateTime:
        return QStringLiteral("DATETIME");
    default:
        return QStringLiteral("TEXT");
    }
}

QString key(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double && std::floor(v.toDouble()) == v.toDouble()
               ? QString::number(qint64(v.toDouble()))
               : v.toString();
}

} // namespace

Result writeSnapshot(const QString &path, const QString &table, const QList<SnapshotColumn> &columns,
                     const QList<QVariantList> &rows, const CurrencyFormat &currency, bool *truncated)
{
    Connection db;
    QString error;
    if (!db.open(path, true, false, &error)) {
        return Result::failure(error);
    }
    QStringList defs;
    QStringList names;
    for (const SnapshotColumn &c : columns) {
        defs.append(quoteIdentifier(c.name) + QLatin1Char(' ') + snapshotType(c.type, !c.lookupDisplay.isEmpty()));
        names.append(quoteIdentifier(c.name));
    }
    Savepoint sp(db, QStringLiteral("qb_snapshot"));
    Result r = db.exec(QStringLiteral("DROP TABLE IF EXISTS %1").arg(quoteIdentifier(table)));
    if (r.ok) {
        r = db.exec(QStringLiteral("CREATE TABLE %1 (%2)").arg(quoteIdentifier(table), defs.join(QStringLiteral(", "))));
    }
    if (!r.ok) {
        return r;
    }
    Statement insert = db.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
                                      .arg(quoteIdentifier(table), names.join(QStringLiteral(", ")),
                                           QStringList(names.size(), QStringLiteral("?")).join(QStringLiteral(", "))),
                                  &error);
    if (!insert.isValid()) {
        return Result::failure(error);
    }
    double scale = 1;
    for (int i = 0; i < currency.decimals; ++i) {
        scale *= 10;
    }
    const int count = int(std::min<qsizetype>(rows.size(), kSnapshotRowLimit));
    for (int r = 0; r < count; ++r) {
        QVariantList values;
        for (int c = 0; c < columns.size(); ++c) {
            const SnapshotColumn &col = columns.at(c);
            QVariant v = rows.at(r).value(c);
            if (!col.lookupDisplay.isEmpty() && v.isValid()) {
                v = col.lookupDisplay.value(key(v), v.toString());
            } else if (col.type == FieldType::Currency && v.metaType().id() == QMetaType::LongLong) {
                v = double(v.toLongLong()) / scale;
            } else if (col.type == FieldType::File && v.isValid()) {
                v = displayText(FieldType::File, v, currency); // reports show what the cell holds
            }
            values.append(v);
        }
        insert.reset();
        insert.bindAll(values);
        if (insert.step() == Statement::Step::Error) {
            return Result::failure(insert.errorText());
        }
    }
    insert = Statement();
    if (truncated != nullptr) {
        *truncated = rows.size() > kSnapshotRowLimit;
    }
    return sp.release();
}

Result writeFormMirror(const BaseFile &file, const QString &path, const QString &table)
{
    const std::optional<TableDef> def = readTable(file, table);
    if (!def) {
        return Result::failure(QStringLiteral("There is no table named %1.").arg(table));
    }
    const QList<ColumnInfo> columns = Records::columns(file, table);
    QStringList defs;
    for (const ColumnInfo &c : columns) {
        if (c.type == FieldType::File) {
            continue; // the form shows files beside the designer (FileFieldPanel.qml), not in it
        }
        if (c.lookup.isSet() && !c.lookup.displayField.isEmpty()) {
            continue; // M80: a lookup is a combo or list box beside the designer (FormLookups.qml)
        }
        QString sql = quoteIdentifier(c.name) + QLatin1Char(' ') + declaredType(c.type);
        if (c.primaryKey && def->primaryKey().size() == 1) {
            sql += QStringLiteral(" PRIMARY KEY");
        } else if (c.required) {
            sql += QStringLiteral(" NOT NULL");
        }
        defs.append(sql);
    }
    Connection db;
    QString error;
    if (!db.open(path, true, false, &error)) {
        return Result::failure(error);
    }
    Savepoint sp(db, QStringLiteral("qb_mirror"));
    Result r = db.exec(QStringLiteral("DROP TABLE IF EXISTS %1").arg(quoteIdentifier(table)));
    if (r.ok) {
        r = db.exec(QStringLiteral("CREATE TABLE %1 (%2)").arg(quoteIdentifier(table), defs.join(QStringLiteral(", "))));
    }
    if (!r.ok) {
        return r;
    }
    return sp.release();
}

} // namespace QindaOffice::Base
