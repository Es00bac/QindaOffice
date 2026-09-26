// SPDX-License-Identifier: GPL-3.0-or-later
// Table designs: create → read back, every kind of alteration through the
// rebuild or plain ALTER, previews that change nothing, refusals with
// reasons, relationships with integrity, backups before destructive
// changes, and schema undo/redo as whole-database images.

#include "base_fixture.h"

#include <QDir>

using namespace BaseTest;

namespace {

TableDef reread(Db &db, const char *name)
{
    return readTable(*db.file, str(name)).value_or(TableDef{});
}

} // namespace

class BaseSchemaTest final : public QObject {
    Q_OBJECT
private slots:
    void createAndReadBack()
    {
        Db db;
        TableDef t = table("contacts", {field("id", FieldType::AutoNumber), field("name", FieldType::Text, true),
                                        field("balance", FieldType::Currency), field("member", FieldType::YesNo),
                                        field("joined", FieldType::Date), field("notes", FieldType::LongText)});
        t.fields[1].caption = str("Full name");
        t.fields[2].defaultValue = str("10.00");
        t.fields[3].defaultValue = str("Yes");
        t.fields[4].defaultValue = str("Date()");
        t.fields[1].indexed = true;
        const SchemaPlan plan = planCreateTable(*db.file, t);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.sqlText().contains(str("\"id\" INTEGER PRIMARY KEY AUTOINCREMENT")));
        QVERIFY(plan.sqlText().contains(str("\"balance\" CURRENCY DEFAULT 1000")));
        QVERIFY(!db.file->tableExists(str("contacts")));
        const PlanPreview preview = db.file->previewPlan(plan);
        QVERIFY2(preview.ok, qPrintable(preview.error));
        QVERIFY(!db.file->tableExists(str("contacts"))); // preview rolled back
        QVERIFY(db.apply(plan).ok);
        const TableDef back = reread(db, "contacts");
        QCOMPARE(back.fields.size(), 6);
        QCOMPARE(back.fields[0].type, FieldType::AutoNumber);
        QVERIFY(back.fields[0].primaryKey);
        QVERIFY(back.fields[1].required);
        QVERIFY(back.fields[1].indexed);
        QCOMPARE(back.fields[1].caption, str("Full name"));
        QCOMPARE(back.fields[2].type, FieldType::Currency);
        QCOMPARE(back.fields[2].defaultValue, str("10.00"));
        QCOMPARE(back.fields[3].defaultValue, str("Yes"));
        QCOMPARE(back.fields[4].defaultValue, str("Date()"));
        QCOMPARE(back.fields[5].type, FieldType::LongText);
        QVERIFY(db.exec(str("INSERT INTO contacts(name) VALUES('Ada')")).ok);
        QCOMPARE(db.scalar(str("SELECT balance FROM contacts")).toLongLong(), qint64(1000));
        QCOMPARE(db.scalar(str("SELECT joined FROM contacts")).toString().size(), 10);
    }

    void refusesBadDesigns()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVERIFY(!planCreateTable(*db.file, table("donors", {field("id", FieldType::AutoNumber)})).ok());
        QVERIFY(!planCreateTable(*db.file, table("qb_mine", {field("id", FieldType::AutoNumber)})).ok());
        QVERIFY(!planCreateTable(*db.file, table("t", {field("a", FieldType::Text), field("A", FieldType::Text)})).ok());
        QVERIFY(!planCreateTable(*db.file, table("t", {field("bad\"name", FieldType::Text)})).ok());
        TableDef twoAuto = table("t", {field("a", FieldType::AutoNumber), field("b", FieldType::AutoNumber)});
        QVERIFY(!planCreateTable(*db.file, twoAuto).ok());
        TableDef badDefault = table("t", {field("n", FieldType::Integer)});
        badDefault.fields[0].defaultValue = str("lots");
        QVERIFY(planCreateTable(*db.file, badDefault).error.contains(str("whole number")));
        QVERIFY(!planDropTable(*db.file, str("donors")).ok()); // referenced by donations
    }

    void renameTableKeepsRelationships()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        TableDef after = reread(db, "donors");
        after.name = str("supporters");
        const SchemaPlan plan = planAlterTable(*db.file, reread(db, "donors"), after);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(!plan.legacyAlter); // a plain rename
        QVERIFY(db.apply(plan).ok);
        QCOMPARE(reread(db, "donations").relationships.first().parentTable, str("supporters"));
        QVERIFY(db.exec(str("INSERT INTO supporters(name) VALUES('Ada')")).ok);
        QVERIFY(db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(1, 100)")).ok);
        QVERIFY(!db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(7, 100)")).ok);
    }

    void renameAndAddFieldsSimply()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        const TableDef before = reread(db, "donors");
        TableDef after = before;
        after.fields[1].name = str("full_name");
        after.fields[1].caption = str("Name");
        after.fields.append(field("phone", FieldType::Text));
        const SchemaPlan plan = planAlterTable(*db.file, before, after);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(!plan.legacyAlter);
        QVERIFY(plan.sqlText().contains(str("RENAME COLUMN \"name\" TO \"full_name\"")));
        QVERIFY(plan.sqlText().contains(str("ADD COLUMN \"phone\" TEXT")));
        QVERIFY(db.apply(plan).ok);
        const TableDef back = reread(db, "donors");
        QCOMPARE(back.fields[1].name, str("full_name"));
        QCOMPARE(back.fields[1].caption, str("Name"));
        QCOMPARE(back.fields.last().name, str("phone"));
    }

    void typeChangesConvertExactly()
    {
        Db db;
        QVERIFY(db.create(table("items", {field("id", FieldType::AutoNumber), field("price", FieldType::Number),
                                          field("code", FieldType::Text), field("when", FieldType::DateTime)}))
                    .ok);
        QVERIFY(db.exec(str("INSERT INTO items(price, code, \"when\") VALUES(19.99, '42', '2026-03-01 10:00:00'), "
                            "(0.1, 'A7', '2026-03-02 11:00:00')"))
                    .ok);
        const TableDef before = reread(db, "items");
        TableDef after = before;
        after.fields[1].type = FieldType::Currency;
        after.fields[2].type = FieldType::Integer;
        after.fields[3].type = FieldType::Date;
        const SchemaPlan plan = planAlterTable(*db.file, before, after);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.legacyAlter);
        QVERIFY(plan.destructive); // date-time → date drops the time
        const PlanPreview preview = db.file->previewPlan(plan);
        QVERIFY2(preview.ok, qPrintable(preview.error));
        QCOMPARE(preview.warnings.size(), 1);
        QVERIFY(preview.warnings.first().startsWith(str("1 value(s) in code")));
        QCOMPARE(reread(db, "items").fields[1].type, FieldType::Number); // unchanged by the preview
        Change change;
        QVERIFY(db.apply(plan, &change).ok);
        QVERIFY(!change.backupPath.isEmpty());
        QVERIFY(QFile::exists(change.backupPath));
        QCOMPARE(db.scalar(str("SELECT price FROM items WHERE id=1")).toLongLong(), qint64(1999));
        QCOMPARE(db.scalar(str("SELECT price FROM items WHERE id=2")).toLongLong(), qint64(10));
        QCOMPARE(db.scalar(str("SELECT code FROM items WHERE id=1")).toLongLong(), qint64(42));
        QCOMPARE(db.scalar(str("SELECT code FROM items WHERE id=2")).toString(), str("A7")); // kept, not lost
        QCOMPARE(db.scalar(str("SELECT \"when\" FROM items WHERE id=1")).toString(), str("2026-03-01"));
        // Undo swaps the whole database back; redo forward again.
        QVERIFY(change.undoable());
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(reread(db, "items").fields[1].type, FieldType::Number);
        QCOMPARE(db.scalar(str("SELECT price FROM items WHERE id=1")).toDouble(), 19.99);
        QVERIFY(db.file->applyChange(change, false).ok);
        QCOMPARE(db.scalar(str("SELECT price FROM items WHERE id=1")).toLongLong(), qint64(1999));
        // And back from currency to a number.
        TableDef again = reread(db, "items");
        TableDef number = again;
        number.fields[1].type = FieldType::Number;
        QVERIFY(db.apply(planAlterTable(*db.file, again, number)).ok);
        QCOMPARE(db.scalar(str("SELECT price FROM items WHERE id=1")).toDouble(), 19.99);
    }

    void requiredAndUniqueCheckTheData()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVERIFY(db.exec(str("INSERT INTO donors(name, email) VALUES('Ada', NULL), ('Grace', 'g@x.org')")).ok);
        TableDef before = reread(db, "donors");
        TableDef after = before;
        after.fields[2].required = true;
        QVERIFY(planAlterTable(*db.file, before, after).error.contains(str("1 record(s) have no email")));
        QVERIFY(db.exec(str("UPDATE donors SET email='g@x.org' WHERE name='Ada'")).ok == false); // unique holds
        after = before;
        after.fields[1].unique = true;
        QVERIFY(db.exec(str("INSERT INTO donors(name) VALUES('Ada')")).ok);
        QVERIFY(planAlterTable(*db.file, before, after).error.contains(str("more than once")));
    }

    void droppingAFieldBacksUp()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVERIFY(db.exec(str("INSERT INTO donors(name, active) VALUES('Ada', 1)")).ok);
        const TableDef before = reread(db, "donors");
        TableDef after = before;
        after.fields.removeAt(3); // active
        const SchemaPlan plan = planAlterTable(*db.file, before, after);
        QVERIFY(plan.destructive);
        QVERIFY(plan.notes.join(str(" ")).contains(str("Deletes the field active (1 value(s))")));
        Change change;
        QVERIFY(db.apply(plan, &change).ok);
        QVERIFY(QFile::exists(change.backupPath));
        QCOMPARE(reread(db, "donors").fields.size(), 3);
        // The parent key other tables use cannot be dropped.
        TableDef noId = reread(db, "donors");
        noId.fields.removeAt(0);
        QVERIFY(planAlterTable(*db.file, reread(db, "donors"), noId).error.contains(str("relationship")));
        // Donations still enforce the relationship after the rebuild.
        QVERIFY(db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(1, 100)")).ok);
        QVERIFY(!db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(5, 100)")).ok);
    }

    void relationshipsEnforceIntegrity()
    {
        Db db;
        QVERIFY(db.create(table("clients", {field("id", FieldType::AutoNumber), field("name", FieldType::Text)})).ok);
        QVERIFY(db.create(table("projects", {field("id", FieldType::AutoNumber), field("client_id", FieldType::Integer),
                                             field("title", FieldType::Text)}))
                    .ok);
        QVERIFY(db.exec(str("INSERT INTO clients(name) VALUES('Harbor')")).ok);
        QVERIFY(db.exec(str("INSERT INTO projects(client_id, title) VALUES(1, 'Logo'), (4, 'Orphan')")).ok);
        Relationship r;
        r.childTable = str("projects");
        r.childField = str("client_id");
        r.parentTable = str("clients");
        r.parentField = str("id");
        r.onDelete = RefAction::SetNull;
        const SchemaPlan plan = planAddRelationship(*db.file, r);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        const PlanPreview refused = db.file->previewPlan(plan);
        QVERIFY(!refused.ok);
        QVERIFY(refused.error.contains(str("1 record(s) in projects → clients")));
        QVERIFY(db.exec(str("UPDATE projects SET client_id=NULL WHERE title='Orphan'")).ok);
        QVERIFY(db.apply(plan).ok);
        const TableDef projects = reread(db, "projects");
        QCOMPARE(projects.relationships.size(), 1);
        QCOMPARE(projects.relationships.first().onDelete, RefAction::SetNull);
        QVERIFY(projects.fields[1].indexed);
        QVERIFY(db.exec(str("DELETE FROM clients WHERE id=1")).ok);
        QVERIFY(db.scalar(str("SELECT client_id FROM projects WHERE title='Logo'")).isNull());
        QCOMPARE(readRelationships(*db.file).size(), 1);
        QVERIFY(db.apply(planRemoveRelationship(*db.file, r)).ok);
        QVERIFY(reread(db, "projects").relationships.isEmpty());
        // Mismatched types and a non-unique parent key are refused.
        Relationship wrong = r;
        wrong.parentField = str("name");
        QVERIFY(planAddRelationship(*db.file, wrong).error.contains(str("primary key or unique")));
    }

    void foreignDeclaredTypesSurviveARebuild()
    {
        QTemporaryDir dir;
        qputenv("XDG_STATE_HOME", dir.filePath(str("state")).toUtf8());
        const QString path = dir.filePath(str("legacy.db"));
        {
            Connection c;
            QVERIFY(c.open(path, true, false, nullptr));
            QVERIFY(c.exec(str("CREATE TABLE people(id INTEGER PRIMARY KEY, name VARCHAR(40), score DECIMAL(5,1))")).ok);
        }
        QString error;
        std::unique_ptr<BaseFile> file = BaseFile::open(path, &error);
        const TableDef before = readTable(*file, str("people")).value();
        TableDef after = before;
        after.fields[1].required = true;
        QVERIFY(file->applyPlan(planAlterTable(*file, before, after), nullptr).ok);
        const QString sql = file->db().scalar(str("SELECT sql FROM sqlite_schema WHERE name='people'")).toString();
        QVERIFY2(sql.contains(str("VARCHAR(40) NOT NULL")), qPrintable(sql));
        QVERIFY(sql.contains(str("DECIMAL(5,1)")));
        QVERIFY(!file->hasSystemTables()); // no metadata without consent
    }
};

BASE_TEST_MAIN(BaseSchemaTest)

#include "tst_base_schema.moc"
