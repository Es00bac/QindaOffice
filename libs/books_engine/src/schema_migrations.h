// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The .qbooks schema and its forward migrations (docs/books.md "Schema").
//
// AGENT-CONTRACT (file format): a company file is a SQLite database with
//   PRAGMA application_id = kApplicationId   ('QBKS')
//   PRAGMA user_version   = schema version (kCurrentVersion when written)
// CompanyFile::open refuses a different application_id and a newer
// user_version, and runs migrate() for an older one. A migration step is
// append-only: never edit a released step, add the next one.

#include <QSqlDatabase>
#include <QString>

namespace QindaOffice::Books::Schema {

inline constexpr int kApplicationId = 0x51424B53; // "QBKS"
inline constexpr int kCurrentVersion = 4;

// Creates the schema at exactly `version` on an empty database (version
// < kCurrentVersion exists for the migration tests). Runs inside the
// caller's transaction.
bool create(const QSqlDatabase &db, int version, QString *error);
// Upgrades from `fromVersion` to kCurrentVersion, one step at a time.
bool migrate(const QSqlDatabase &db, int fromVersion, QString *error);

[[nodiscard]] int readUserVersion(const QSqlDatabase &db);
[[nodiscard]] int readApplicationId(const QSqlDatabase &db);

} // namespace QindaOffice::Books::Schema
