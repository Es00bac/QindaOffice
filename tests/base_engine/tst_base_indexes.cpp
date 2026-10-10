// SPDX-License-Identifier: GPL-3.0-or-later
// M86's Indexes dialog (D-094) against hand-worked results: a multi-field
// unique index ignoring Nulls with a descending field, read back as it was
// designed; indexes added and dropped without a table rebuild; a unique
// index refused while records repeat its values (with the count); names
// kept through a table rename and a foreign index kept under its own name
// through a rebuild; the checks on names and fields.

#include "base_fixture.h"

#include "base_indexes.h"

using namespace BaseTest;

namespace {

TableDef reread(Db &db, const char *name)
{
    return readTable(*db.file, str(name)).value_or(TableDef());
}

IndexDef index(const char *name, QList<IndexField> fields, bool unique, bool ignoreNulls)
{
    IndexDef i;
    i.name = str(name);
    i.fields = std::move(fields);
    i.unique = unique;
    i.ignoreNulls = ignoreNulls;
    return i;
}

bool hasIndex(Db &db, const char *sqlName)
{
    return db.scalar(str("SELECT count(*) FROM sqlite_schema WHERE type = 'index' AND name = ?"), {str(sqlName)}).toInt() == 1;
}

} // namespace

class BaseIndexesTest final : public QObject {
    Q_OBJECT
private slots:
    void multiFieldIndexesRoundTrip()
    {
        Db db;
        TableDef visits = table("visits", {field("id", FieldType::AutoNumber), field("donor", FieldType::Integer),
                                           field("day", FieldType::Date), field("city", FieldType::Text)});
        visits.indexes = {index("ByDonorDay", {{str("donor"), false}, {str("day"), true}}, true, true)};
        QVERIFY2(db.create(visits).ok, qPrintable(planCreateTable(*db.file, visits).error));
        QVERIFY(hasIndex(db, "qbi:visits:ByDonorDay"));
        const QString sql = db.scalar(str("SELECT sql FROM sqlite_schema WHERE name = 'qbi:visits:ByDonorDay'")).toString();
        QVERIFY2(sql.contains(str("\"day\" DESC")), qPrintable(sql));
        QVERIFY2(sql.contains(str("WHERE \"donor\" IS NOT NULL AND \"day\" IS NOT NULL")), qPrintable(sql));
        const TableDef read = reread(db, "visits");
        QCOMPARE(read.indexes.size(), 1);
        const IndexDef &i = read.indexes.first();
        QCOMPARE(i.name, str("ByDonorDay"));
        QCOMPARE(i.fields.size(), 2);
        QCOMPARE(i.fields.at(1).field, str("day"));
        QVERIFY(i.fields.at(1).descending);
        QVERIFY(i.unique);
        QVERIFY(i.ignoreNulls);
        QVERIFY(read.otherIndexSql.isEmpty());
        // Unique over (donor, day); Nulls never clash.
        QVERIFY(db.exec(str("INSERT INTO visits(donor, day) VALUES(1, '2026-03-01'), (1, NULL), (1, NULL)")).ok);
        QVERIFY(!db.exec(str("INSERT INTO visits(donor, day) VALUES(1, '2026-03-01')")).ok);
        // Through the map QML edits, and back.
        const TableDef again = TableDef::fromMap(read.toMap());
        QCOMPARE(again.indexes.size(), 1);
        QCOMPARE(again.indexes.first().originalName, str("qbi:visits:ByDonorDay"));
    }

    void indexesChangeWithoutARebuild()
    {
        Db db;
        QVERIFY(db.create(table("visits", {field("id", FieldType::AutoNumber), field("donor", FieldType::Integer),
                                           field("city", FieldType::Text)}))
                    .ok);
        QVERIFY(db.exec(str("INSERT INTO visits(donor, city) VALUES(1, 'Salem'), (2, 'Salem'), (2, 'Bend'), (3, 'Bend')")).ok);
        TableDef before = reread(db, "visits");
        TableDef after = before;
        after.indexes.append(index("ByCity", {{str("city"), false}, {str("donor"), false}}, false, false));
        const SchemaPlan add = planAlterTable(*db.file, before, after);
        QVERIFY2(add.ok(), qPrintable(add.error));
        QVERIFY(!add.sqlText().contains(str("DROP TABLE"))); // no rebuild
        QVERIFY(add.sqlText().contains(str("CREATE INDEX \"qbi:visits:ByCity\"")));
        QVERIFY(db.apply(add).ok);
        // Unique over city alone: Salem and Bend repeat — two sets.
        before = reread(db, "visits");
        after = before;
        after.indexes.append(index("OneCity", {{str("city"), false}}, true, false));
        const SchemaPlan unique = planAlterTable(*db.file, before, after);
        QVERIFY(!unique.ok());
        QVERIFY2(unique.error.contains(str("2 set(s) of values repeat in city")), qPrintable(unique.error));
        // Unique over (donor, city) holds.
        after.indexes.last().fields.append({str("donor"), false});
        QVERIFY2(db.apply(planAlterTable(*db.file, before, after)).ok, qPrintable(planAlterTable(*db.file, before, after).error));
        QCOMPARE(reread(db, "visits").indexes.size(), 2);
        // Dropping one is a DROP INDEX.
        before = reread(db, "visits");
        after = before;
        after.indexes.removeIf([](const IndexDef &i) { return i.name == str("ByCity"); });
        const SchemaPlan drop = planAlterTable(*db.file, before, after);
        QVERIFY(drop.sqlText().contains(str("DROP INDEX IF EXISTS \"qbi:visits:ByCity\"")));
        QVERIFY(!drop.sqlText().contains(str("DROP TABLE")));
        QVERIFY(db.apply(drop).ok);
        QVERIFY(!hasIndex(db, "qbi:visits:ByCity"));
        QVERIFY(hasIndex(db, "qbi:visits:OneCity"));
    }

    void namesSurviveRenamesAndRebuilds()
    {
        Db db;
        TableDef visits = table("visits", {field("id", FieldType::AutoNumber), field("donor", FieldType::Integer),
                                           field("city", FieldType::Text)});
        visits.indexes = {index("ByCity", {{str("city"), false}, {str("donor"), true}}, false, false)};
        QVERIFY(db.create(visits).ok);
        TableDef before = reread(db, "visits");
        TableDef renamed = before;
        renamed.name = str("calls");
        QVERIFY2(db.apply(planAlterTable(*db.file, before, renamed)).ok, qPrintable(planAlterTable(*db.file, before, renamed).error));
        QVERIFY(hasIndex(db, "qbi:calls:ByCity"));
        QVERIFY(!hasIndex(db, "qbi:visits:ByCity"));
        QCOMPARE(reread(db, "calls").indexes.first().name, str("ByCity"));
        // A foreign index keeps its own name through a rebuild.
        QVERIFY(db.exec(str("CREATE INDEX my_idx ON calls(donor, city)")).ok);
        before = reread(db, "calls");
        QCOMPARE(before.indexes.size(), 2);
        TableDef retyped = before;
        retyped.fields[retyped.fieldIndex(str("donor"))].type = FieldType::Text; // a rebuild
        QVERIFY2(db.apply(planAlterTable(*db.file, before, retyped)).ok, qPrintable(planAlterTable(*db.file, before, retyped).error));
        QVERIFY(hasIndex(db, "my_idx"));
        QVERIFY(hasIndex(db, "qbi:calls:ByCity"));
        QCOMPARE(reread(db, "calls").indexes.size(), 2);
    }

    void badIndexesAreRefused()
    {
        Db db;
        TableDef t = table("t", {field("id", FieldType::AutoNumber), field("a", FieldType::Text),
                                 field("pic", FieldType::File)});
        t.indexes = {index("X", {{str("nope"), false}}, false, false)};
        QVERIFY(planCreateTable(*db.file, t).error.contains(str("missing field nope")));
        t.indexes = {index("X", {{str("a"), false}}, false, false), index("x", {{str("a"), true}}, false, false)};
        QVERIFY(planCreateTable(*db.file, t).error.contains(str("Two indexes are named")));
        t.indexes = {index("X", {{str("pic"), false}}, false, false)};
        QVERIFY(planCreateTable(*db.file, t).error.contains(str("file field")));
        t.indexes = {index("X", {}, false, false)};
        QVERIFY(planCreateTable(*db.file, t).error.contains(str("at least one field")));
    }
};

BASE_TEST_MAIN(BaseIndexesTest)

#include "tst_base_indexes.moc"
