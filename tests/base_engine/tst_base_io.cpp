// SPDX-License-Identifier: GPL-3.0-or-later
// Saved objects, CSV import/export, starter templates, report snapshots and
// form mirrors, and Access import through (fake) mdb-tools.

#include "base_fixture.h"

#include "base_access.h"
#include "base_csv.h"
#include "base_form_controls.h"
#include "base_macro.h"
#include "base_objects.h"
#include "base_query.h"
#include "base_records.h"
#include "base_snapshot.h"
#include "base_templates.h"

#include <QFile>

using namespace BaseTest;

class BaseIoTest final : public QObject {
    Q_OBJECT
private slots:
    void objectsSaveRenameDeleteAndUndo()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVariantMap def{{str("version"), 1},
                        {str("source"), QVariantMap{{str("kind"), str("table")}, {str("name"), str("donors")}}},
                        {str("title"), str("Donor")},
                        {str("futureMember"), str("kept")}};
        Change change;
        QVERIFY(ObjectStore::save(*db.file, str("form"), str("Donor Form"), def, false, &change).ok);
        QVERIFY(!ObjectStore::save(*db.file, str("form"), str("donor form"), def, false, nullptr).ok); // case-blind clash
        QVERIFY(ObjectStore::save(*db.file, str("report"), str("Donor Form"), def, false, nullptr).ok); // other kind
        QCOMPARE(ObjectStore::uniqueName(*db.file, str("form"), str("Donor Form")), str("Donor Form 2"));
        QCOMPARE(ObjectStore::load(*db.file, str("form"), str("Donor Form"))->definition.value(str("futureMember")).toString(),
                 str("kept"));
        QVERIFY(ObjectStore::rename(*db.file, str("form"), str("Donor Form"), str("Donor Card"), nullptr).ok);
        QCOMPARE(ObjectStore::list(*db.file, str("form")).first().name, str("Donor Card"));
        QCOMPARE(ObjectStore::referencesTo(*db.file, str("donors")).size(), 2);
        // A table rename rewrites the objects in the same schema step.
        const TableDef before = readTable(*db.file, str("donors")).value();
        TableDef after = before;
        after.name = str("supporters");
        SchemaPlan plan = planAlterTable(*db.file, before, after);
        plan.statements.append(ObjectStore::tableRenameStatements(*db.file, str("donors"), str("supporters")));
        QVERIFY(db.apply(plan).ok);
        QCOMPARE(ObjectStore::load(*db.file, str("form"), str("Donor Card"))->definition.value(str("source")).toMap()
                     .value(str("name"))
                     .toString(),
                 str("supporters"));
        Change removed;
        QVERIFY(ObjectStore::remove(*db.file, str("report"), str("Donor Form"), &removed).ok);
        QVERIFY(!ObjectStore::load(*db.file, str("report"), str("Donor Form")));
        QVERIFY(db.file->applyChange(removed, true).ok);
        QVERIFY(ObjectStore::load(*db.file, str("report"), str("Donor Form")));
    }

    void csvDetectsAndImports()
    {
        Db db;
        const QByteArray bytes = "\xEF\xBB\xBFName;Zip;Amount;Joined;Member;Score;Notes\r\n"
                                 "Ada;02134;$1,250.00;3/14/2026;yes;4.5;\"likes \"\"math\"\"; engines\"\r\n"
                                 "Grace;94016;(20.00);2026-01-02;no;3;\r\n";
        const CsvTable csv = parseCsv(bytes);
        QCOMPARE(csv.delimiter, QLatin1Char(';'));
        QCOMPARE(csv.header.first(), str("Name"));
        QCOMPARE(csv.rows.size(), 2);
        QCOMPARE(csv.rows.first().last(), str("likes \"math\"; engines"));
        const CsvImportPlan plan = proposeImport(*db.file, csv, str("members"));
        QVERIFY(plan.createNew);
        const QList<FieldType> expected{FieldType::Text, FieldType::Text, FieldType::Currency, FieldType::Date,
                                        FieldType::YesNo, FieldType::Number, FieldType::Text};
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(plan.columns.at(i).type, expected.at(i));
        }
        Change schema;
        Change data;
        const ImportResult r = importCsv(*db.file, csv, plan, &schema, &data);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.imported, 2);
        QCOMPARE(db.scalar(str("SELECT Amount FROM members WHERE Name='Grace'")).toLongLong(), qint64(-2000));
        QCOMPARE(db.scalar(str("SELECT Zip FROM members WHERE Name='Ada'")).toString(), str("02134"));
        QCOMPARE(db.scalar(str("SELECT Joined FROM members WHERE Name='Ada'")).toString(), str("2026-03-14"));
        // Into the existing table: columns map by name; a bad row stops it all…
        const CsvTable more = parseCsv("Name,Amount,Unknown\nKatherine,12.50,x\nDorothy,lots,y\n");
        CsvImportPlan into = proposeImport(*db.file, more, str("members"));
        QVERIFY(!into.createNew);
        QVERIFY(!into.columns.at(2).include);
        ImportResult bad = importCsv(*db.file, more, into, nullptr, nullptr);
        QVERIFY(!bad.ok);
        QVERIFY2(bad.error.contains(str("Line 3, Amount needs an amount")), qPrintable(bad.error));
        QCOMPARE(db.scalar(str("SELECT count(*) FROM members")).toInt(), 2);
        // …unless bad rows are skipped.
        into.skipBadRows = true;
        bad = importCsv(*db.file, more, into, nullptr, nullptr);
        QVERIFY(bad.ok);
        QCOMPARE(bad.imported, 1);
        QCOMPARE(bad.skipped, 1);
        // A failed import into a NEW table leaves no table behind.
        CsvImportPlan fresh = proposeImport(*db.file, more, str("fresh"));
        fresh.columns[1].type = FieldType::Currency;
        Change s2;
        QVERIFY(!importCsv(*db.file, more, fresh, &s2, nullptr).ok);
        QVERIFY(!db.file->tableExists(str("fresh")));
        // An "id" column of unique numbers becomes the key.
        const CsvTable withIds = parseCsv("id,label\n10,a\n20,b\n");
        const CsvImportPlan keyed = proposeImport(*db.file, withIds, str("keyed"));
        QVERIFY(!keyed.addId);
        QVERIFY(keyed.columns.first().primaryKey);
        QVERIFY(importCsv(*db.file, withIds, keyed, nullptr, nullptr).ok);
        QCOMPARE(db.scalar(str("SELECT label FROM keyed WHERE id=20")).toString(), str("b"));
        // Export: quotes only where needed.
        QCOMPARE(csvBytes({str("a"), str("b")}, {{str("x,y"), str("say \"hi\"")}}),
                 QByteArray("a,b\r\n\"x,y\",\"say \"\"hi\"\"\"\r\n"));
        const QString path = db.path("out.csv");
        QVERIFY(writeFileAtomically(path, QByteArray("ok")).ok);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("ok"));
    }

    void everyTemplateBuildsAndItsQueriesRun()
    {
        const QList<TemplateInfo> list = templates();
        QCOMPARE(list.size(), 5);
        for (const TemplateInfo &t : list) {
            Db db;
            const Result r = applyTemplate(*db.file, t.id);
            QVERIFY2(r.ok, qPrintable(t.id + str(": ") + r.error));
            QVERIFY(!db.file->tableNames().isEmpty());
            QVERIFY2(!ObjectStore::list(*db.file, str("form")).isEmpty(), qPrintable(t.id));
            QVERIFY2(!ObjectStore::list(*db.file, str("report")).isEmpty(), qPrintable(t.id));
            for (const SavedObject &q : ObjectStore::list(*db.file, str("query"))) {
                const CompiledQuery c = compileQuery(*db.file, QueryDesign::fromMap(q.definition));
                QVERIFY2(c.ok, qPrintable(t.id + str("/") + q.name + str(": ") + c.error));
                QVariantMap params;
                for (const QueryParameter &p : c.parameters) {
                    params.insert(p.name, p.type == FieldType::Date ? str("2026-01-01") : str("1"));
                }
                const QueryResult run = runSelect(*db.file, c.sql, c.parameters, params);
                QVERIFY2(run.ok, qPrintable(t.id + str("/") + q.name + str(": ") + run.error));
            }
            for (const SavedObject &rep : ObjectStore::list(*db.file, str("report"))) {
                const QVariantMap source = rep.definition.value(str("source")).toMap();
                if (source.value(str("kind")).toString() == QLatin1String("query")) {
                    QVERIFY2(ObjectStore::load(*db.file, str("query"), source.value(str("name")).toString()),
                             qPrintable(rep.name));
                } else {
                    QVERIFY(db.file->tableExists(source.value(str("name")).toString()));
                }
            }
            // M80: the template's macros and form controls are ready to run.
            for (const SavedObject &m : ObjectStore::list(*db.file, str("macro"))) {
                QVERIFY2(checkMacro(*db.file, Macro::fromMap(m.definition), m.name).isEmpty(), qPrintable(m.name));
            }
            for (const SavedObject &f : ObjectStore::list(*db.file, str("form"))) {
                const QString table = f.definition.value(str("source")).toMap().value(str("name")).toString();
                QVERIFY2(checkFormControls(*db.file, table, formControls(f.definition)).isEmpty(), qPrintable(f.name));
            }
            QVERIFY(db.file->integrityProblems(true).isEmpty());
        }
        Db db;
        QVERIFY(!applyTemplate(*db.file, str("nope")).ok);
    }

    void snapshotsAndMirrorsForTheToolkit()
    {
        Db db;
        QVERIFY(applyTemplate(*db.file, str("donors")).ok);
        db.exec(str("INSERT INTO donors(name) VALUES('Ada'), ('Grace')"));
        db.exec(str("INSERT INTO donations(donor_id, amount, given) VALUES(1, 2550, '2026-01-01'), (2, 10000, "
                    "'2026-01-02')"));
        const QString scratch = db.path("presentation.sqlite");
        QVERIFY(writeFormMirror(*db.file, scratch, str("donations")).ok);
        Connection c;
        QVERIFY(c.open(scratch, false, true, nullptr));
        QCOMPARE(c.scalar(str("SELECT count(*) FROM donations")).toInt(), 0);
        // M89: positioned lookup controls bind their visible label to the
        // mirror field; the source database keeps the typed foreign key.
        QCOMPARE(c.scalar(str("SELECT type FROM pragma_table_info('donations') WHERE name='donor_id'")).toString(),
                 str("TEXT"));
        QCOMPARE(c.scalar(str("SELECT type FROM pragma_table_info('donations') WHERE name='amount'")).toString(),
                 str("CURRENCY"));
        QCOMPARE(c.scalar(str("SELECT \"notnull\" FROM pragma_table_info('donations') WHERE name='amount'")).toInt(), 1);
        c.close();
        const RowSet rows = Records::read(*db.file, str("donations"));
        QList<SnapshotColumn> cols;
        for (const ColumnInfo &ci : rows.columns) {
            SnapshotColumn sc{ci.name, ci.type, {}};
            if (ci.lookup.isSet()) {
                for (const auto &[value, text] : Records::lookupChoices(*db.file, ci.lookup)) {
                    sc.lookupDisplay.insert(value.toString(), text);
                }
            }
            cols.append(sc);
        }
        bool truncated = true;
        QVERIFY(writeSnapshot(db.path("report.sqlite"), str("donations"), cols, rows.values, db.file->currency(),
                              &truncated)
                    .ok);
        QVERIFY(!truncated);
        QVERIFY(c.open(db.path("report.sqlite"), false, true, nullptr));
        QCOMPARE(c.scalar(str("SELECT donor_id FROM donations WHERE id=2")).toString(), str("Grace"));
        QCOMPARE(c.scalar(str("SELECT amount FROM donations WHERE id=1")).toDouble(), 25.5);
        QCOMPARE(c.scalar(str("SELECT typeof(receipt_sent) FROM donations WHERE id=1")).toString(), str("integer"));
    }

    void accessImportNeedsMdbTools()
    {
        QTemporaryDir bin;
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8());
        AccessTools tools = findAccessTools();
        QVERIFY(!tools.available);
        QString error;
        QVERIFY(accessTableNames(tools, str("x.accdb"), &error).isEmpty());
        QVERIFY(error.contains(str("mdbtools")));
        // Fake programs with the real ones' command-line shape.
        const auto script = [&](const char *name, const QByteArray &body) {
            QFile f(bin.filePath(str(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n" + body);
            f.close();
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        };
        script("mdb-tables", "printf 'MSysObjects\\nMembers\\nDues\\n'\n");
        // The table is the last argument (base_access.cpp exportArguments).
        script("mdb-export", "eval t=\\${$#}\nif [ \"$t\" = Members ]; then printf 'ID,Name,Joined\\n1,\"Ada\",\"2026-03-14 00:00:00\"\\n'; "
                             "else exit 3; fi\n");
        tools = findAccessTools();
        QVERIFY(tools.available);
        QCOMPARE(accessTableNames(tools, str("club.accdb"), &error), (QStringList{str("Members"), str("Dues")}));
        const std::optional<CsvTable> members = exportAccessTable(tools, str("club.accdb"), str("Members"), &error);
        QVERIFY2(members, qPrintable(error));
        QCOMPARE(members->rows.first().at(2), str("2026-03-14")); // midnight dropped: a date
        QVERIFY(!exportAccessTable(tools, str("club.accdb"), str("Dues"), &error));
        qputenv("PATH", oldPath);
    }
};

BASE_TEST_MAIN(BaseIoTest)

#include "tst_base_io.moc"
