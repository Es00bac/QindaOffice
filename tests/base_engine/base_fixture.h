// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the base_engine tests: a fresh .qbase in a temp dir,
// XDG_STATE_HOME pointed into it (automatic backups land there), and small
// helpers to build table designs.

#include "base_file.h"
#include "base_schema.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace BaseTest {

using namespace QindaOffice::Base;

inline QString str(const char *text)
{
    return QString::fromUtf8(text);
}

inline FieldDef field(const char *name, FieldType type, bool required = false)
{
    FieldDef f;
    f.name = str(name);
    f.type = type;
    f.required = required;
    if (type == FieldType::AutoNumber) {
        f.primaryKey = true;
    }
    return f;
}

inline TableDef table(const char *name, QList<FieldDef> fields)
{
    TableDef t;
    t.name = str(name);
    t.fields = std::move(fields);
    t.autoIncrement = true;
    return t;
}

struct Db {
    QTemporaryDir dir;
    std::unique_ptr<BaseFile> file;
    QString error;

    Db()
    {
        qputenv("XDG_STATE_HOME", dir.filePath(str("state")).toUtf8());
        file = BaseFile::create(dir.filePath(str("test.qbase")), &error);
    }
    [[nodiscard]] QString path(const char *name) const { return dir.filePath(str(name)); }
    Result apply(const SchemaPlan &plan, Change *change = nullptr, QStringList *warnings = nullptr)
    {
        return file->applyPlan(plan, change, warnings);
    }
    Result create(const TableDef &t, Change *change = nullptr)
    {
        return apply(planCreateTable(*file, t), change);
    }
    Result exec(const QString &sql, const QVariantList &bindings = {}) { return file->db().run(sql, bindings); }
    [[nodiscard]] QVariant scalar(const QString &sql, const QVariantList &bindings = {}) const
    {
        return file->db().scalar(sql, bindings);
    }
    // Donors (AutoNumber id, name required, email unique) and donations
    // (amount currency, a cascade relationship to donors).
    bool donorsAndDonations(RefAction onDelete = RefAction::Cascade)
    {
        TableDef donors = table("donors", {field("id", FieldType::AutoNumber), field("name", FieldType::Text, true),
                                           field("email", FieldType::Text), field("active", FieldType::YesNo)});
        donors.fields[2].unique = true;
        TableDef donations = table("donations", {field("id", FieldType::AutoNumber), field("donor_id", FieldType::Integer, true),
                                                 field("amount", FieldType::Currency, true), field("given", FieldType::Date)});
        Relationship r;
        r.childTable = str("donations");
        r.childField = str("donor_id");
        r.parentTable = str("donors");
        r.parentField = str("id");
        r.onDelete = onDelete;
        donations.relationships.append(r);
        return create(donors).ok && create(donations).ok;
    }
};

} // namespace BaseTest

#define BASE_TEST_MAIN(TestClass)                                                                                  \
    int main(int argc, char *argv[])                                                                               \
    {                                                                                                              \
        QTemporaryDir home;                                                                                        \
        qputenv("XDG_STATE_HOME", home.filePath(QStringLiteral("state")).toUtf8());                                \
        QCoreApplication app(argc, argv);                                                                          \
        TestClass test;                                                                                            \
        return QTest::qExec(&test, argc, argv);                                                                    \
    }
