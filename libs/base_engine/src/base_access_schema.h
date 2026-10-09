// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The schema of an Access file as `mdb-schema FILE postgres` prints it
// (base_access.h), parsed into what the import needs (M65, D-074).
//
// AGENT-CONTRACT (with mdb-tools' backend.c, mdbtools 1.0 — the format is
// pinned by the hand-written captures in tests/base_engine/
// tst_base_access.cpp):
//  * CREATE TABLE IF NOT EXISTS "t"\n (\n\t"c"\t\t\tTYPE[ (n)|(p, s)][ NOT
//    NULL], \n…\n); — types from the postgres table (BOOLEAN, SMALLINT,
//    INTEGER, NUMERIC(15,2) = Currency, REAL, DOUBLE PRECISION, TIMESTAMP
//    WITHOUT TIME ZONE, DATE = a short-date field, BYTEA = Binary/OLE,
//    VARCHAR, TEXT = Memo, UUID = Replication ID, NUMERIC (p, s), SERIAL =
//    AutoNumber) or "Unknown_00xx" (attachments, multi-valued fields).
//  * COMMENT ON COLUMN "t"."c" IS '…'; / COMMENT ON TABLE "t" IS '…';
//  * ALTER TABLE "t" ADD CONSTRAINT "t_pkey" PRIMARY KEY ("a", …);
//  * CREATE [UNIQUE] INDEX "t_i_idx" ON "t" ("a"[ DESC], …);
//  * ALTER TABLE "C" ADD CONSTRAINT "c_a_fk" FOREIGN KEY ("a") REFERENCES
//    "P"("b")[ ON UPDATE CASCADE][ ON DELETE CASCADE] DEFERRABLE
//    INITIALLY IMMEDIATE; and, for one Access does not enforce,
//    -- Relationship from "C" ("a") to "P"("b") does not enforce integrity.
// Names arrive lower-cased (table names in relationships keep their case);
// every lookup here is case-blind.

#include "base_types.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Base {

struct AccessColumn {
    QString name;
    QString type; // as printed, without NOT NULL: "VARCHAR (100)", "NUMERIC(15,2)"
    bool required = false;
    QString description;
};

struct AccessIndex {
    QString name;
    bool primary = false;
    bool unique = false;
    QStringList columns;
    QList<bool> descending;
};

struct AccessTable {
    QString name;
    QList<AccessColumn> columns;
    QList<AccessIndex> indexes;
    QString description;
    [[nodiscard]] const AccessColumn *column(const QString &name) const;
    [[nodiscard]] QStringList primaryKey() const;
};

struct AccessRelationship {
    QString childTable;
    QString childColumn;
    QString parentTable;
    QString parentColumn;
    bool enforced = true;
    bool cascadeUpdate = false;
    bool cascadeDelete = false;
};

struct AccessSchema {
    QList<AccessTable> tables;
    QList<AccessRelationship> relationships;
    [[nodiscard]] const AccessTable *table(const QString &name) const;
};

[[nodiscard]] AccessSchema parseAccessSchema(const QString &ddl);
// The QindaBase type for a column; nullopt when QindaBase cannot hold it
// (an attachment or multi-valued field mdb-tools prints as Unknown_00xx).
[[nodiscard]] std::optional<FieldType> accessFieldType(const AccessColumn &column);

} // namespace QindaOffice::Base
