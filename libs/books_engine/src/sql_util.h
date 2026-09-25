// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Small helpers over QSqlQuery used by every engine module: prepared
// statements with positional binds, scalar reads and ISO date columns.
// Internal to books_engine (not part of the app-facing API).
//
// AGENT-GUARD: a QSqlQuery that is still active blocks RELEASE of a
// SAVEPOINT ("SQL statements in progress"). Keep queries in the narrowest
// scope, or call finish(), before a SqlTransaction commits.

#include <QDate>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantList>

namespace QindaOffice::Books::Sql {

// Dates are stored as TEXT 'YYYY-MM-DD' so string order is date order.
[[nodiscard]] QString isoDate(const QDate &date);
[[nodiscard]] QDate fromIso(const QVariant &value);
// NULL for 0 (optional foreign keys).
[[nodiscard]] QVariant idOrNull(qint64 id);

// Prepares, binds and executes. On failure returns false and fills
// `error` with the driver text plus the statement.
bool run(QSqlQuery &query, const QString &sql, const QVariantList &binds = {},
         QString *error = nullptr);
// First column of the first row, or `fallback` when there is no row.
[[nodiscard]] QVariant scalar(const QSqlDatabase &db, const QString &sql,
                              const QVariantList &binds = {}, const QVariant &fallback = {},
                              QString *error = nullptr);
[[nodiscard]] qint64 scalarInt(const QSqlDatabase &db, const QString &sql,
                               const QVariantList &binds = {}, qint64 fallback = 0);
// Executes a statement with no result rows (DDL, UPDATE, DELETE).
bool exec(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {},
          QString *error = nullptr);
// INSERT and return lastInsertId (0 on failure).
qint64 insert(const QSqlDatabase &db, const QString &sql, const QVariantList &binds,
              QString *error = nullptr);

} // namespace QindaOffice::Books::Sql
