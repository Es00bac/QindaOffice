// SPDX-License-Identifier: GPL-3.0-or-later
// Records: reading and writing File cells (base_file_field.h, D-074).
#include "base_file_field.h"
#include "base_records.h"

namespace QindaOffice::Base {

namespace {

const ColumnInfo *fileColumn(const QList<ColumnInfo> &columns, const QString &field, QString *error)
{
    for (const ColumnInfo &c : columns) {
        if (c.name.compare(field, Qt::CaseInsensitive) == 0) {
            if (c.type != FieldType::File) {
                *error = QStringLiteral("%1 is not a file field.").arg(c.label());
                return nullptr;
            }
            return &c;
        }
    }
    *error = QStringLiteral("There is no field %1.").arg(field);
    return nullptr;
}

} // namespace

std::optional<QByteArray> Records::readFile(const BaseFile &file, const QString &table, qint64 rowid,
                                            const QString &field, QString *error)
{
    const QList<ColumnInfo> cols = columns(file, table);
    const ColumnInfo *column = fileColumn(cols, field, error);
    if (column == nullptr) {
        return std::nullopt;
    }
    const QList<QVariantList> rows = file.db().rows(
        QStringLiteral("SELECT %1 FROM %2 WHERE rowid = ?").arg(quoteIdentifier(column->name), quoteIdentifier(table)),
        {rowid}, error);
    if (rows.isEmpty()) {
        if (error->isEmpty()) {
            *error = QStringLiteral("That record no longer exists.");
        }
        return std::nullopt;
    }
    const QVariant v = rows.first().value(0);
    if (!v.isValid() || v.isNull()) {
        return QByteArray();
    }
    // A foreign file may hold text in the column; its UTF-8 is the file.
    return v.metaType().id() == QMetaType::QByteArray ? v.toByteArray() : v.toString().toUtf8();
}

Result Records::setFile(BaseFile &file, const QString &table, qint64 rowid, const QString &field,
                        const QByteArray &bytes, Change *change)
{
    if (file.readOnly()) {
        return Result::failure(QStringLiteral("The database is read-only."));
    }
    const QList<ColumnInfo> cols = columns(file, table);
    QString error;
    const ColumnInfo *column = fileColumn(cols, field, &error);
    if (column == nullptr) {
        return Result::failure(error);
    }
    if (bytes.size() > kFileFieldLimit) {
        return Result::failure(QStringLiteral("%1 holds files up to %2; this one is %3.")
                                   .arg(column->label(), formatByteSize(kFileFieldLimit), formatByteSize(bytes.size())));
    }
    const qint64 exists =
        file.db().scalar(QStringLiteral("SELECT count(*) FROM %1 WHERE rowid = ?").arg(quoteIdentifier(table)), {rowid})
            .toLongLong();
    if (exists == 0) {
        return Result::failure(QStringLiteral("Save the record first, then add its file."));
    }
    const QString label = bytes.isNull() ? QStringLiteral("Remove file from %1").arg(column->label())
                                         : QStringLiteral("Insert file into %1").arg(column->label());
    // AGENT-NOTE: an empty (but non-null) array stores an empty BLOB — a
    // zero-byte file is still a file; only a null array clears the cell.
    const QVariant value = bytes.isNull() ? QVariant() : QVariant(bytes);
    return file.capture(label, [&]() -> Result {
        const Result run = file.db().run(
            QStringLiteral("UPDATE %1 SET %2 = ? WHERE rowid = ?").arg(quoteIdentifier(table), quoteIdentifier(column->name)),
            {value, rowid});
        return run.ok ? run : Result::failure(explain(run.error, cols, table));
    }, change);
}

} // namespace QindaOffice::Base
