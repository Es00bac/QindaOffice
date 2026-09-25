// SPDX-License-Identifier: GPL-3.0-or-later
#include "sql_util.h"

#include <QSqlError>

namespace QindaOffice::Books::Sql {

QString isoDate(const QDate &date)
{
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

QDate fromIso(const QVariant &value)
{
    if (value.isNull()) {
        return {};
    }
    return QDate::fromString(value.toString(), Qt::ISODate);
}

QVariant idOrNull(qint64 id)
{
    return id > 0 ? QVariant(id) : QVariant(QMetaType::fromType<qint64>());
}

bool run(QSqlQuery &query, const QString &sql, const QVariantList &binds, QString *error)
{
    if (!query.prepare(sql)) {
        if (error != nullptr) {
            *error = query.lastError().text() + QStringLiteral(" [") + sql + QLatin1Char(']');
        }
        return false;
    }
    for (const QVariant &value : binds) {
        // AGENT-GUARD: QSQLITE binds a NULL QString as SQL NULL, and every
        // "(COALESCE(?, '') = '' OR date <= ?)"-style optional filter — and
        // plain text columns — expect '' for "no value". Intentional NULLs
        // are passed as QVariant() or idOrNull(), never as a QString.
        if (value.typeId() == QMetaType::QString && value.toString().isNull()) {
            query.addBindValue(QString(0, QChar())); // empty, not null
            continue;
        }
        query.addBindValue(value);
    }
    if (!query.exec()) {
        if (error != nullptr) {
            *error = query.lastError().text() + QStringLiteral(" [") + sql + QLatin1Char(']');
        } else {
            // Nobody is checking this statement: say so, rather than let a
            // scalar() fallback hide a broken query.
            qWarning("books_engine: SQL failed: %s [%s]", qPrintable(query.lastError().text()), qPrintable(sql));
        }
        return false;
    }
    return true;
}

QVariant scalar(const QSqlDatabase &db, const QString &sql, const QVariantList &binds,
                const QVariant &fallback, QString *error)
{
    QSqlQuery query(db);
    if (!run(query, sql, binds, error) || !query.next()) {
        return fallback;
    }
    const QVariant value = query.value(0);
    return value.isNull() ? fallback : value;
}

qint64 scalarInt(const QSqlDatabase &db, const QString &sql, const QVariantList &binds,
                 qint64 fallback)
{
    return scalar(db, sql, binds, QVariant(fallback)).toLongLong();
}

bool exec(const QSqlDatabase &db, const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery query(db);
    return run(query, sql, binds, error);
}

qint64 insert(const QSqlDatabase &db, const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery query(db);
    if (!run(query, sql, binds, error)) {
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

} // namespace QindaOffice::Books::Sql
