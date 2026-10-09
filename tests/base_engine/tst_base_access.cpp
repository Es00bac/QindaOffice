// SPDX-License-Identifier: GPL-3.0-or-later
// Access import (M65, D-074) over captured mdb-tools output: the schema
// parser, the query conversion, and a whole import through fake mdb-tools
// programs with the real ones' command-line shape — tables with their
// design and records, file fields from OLE columns, relationships, select
// queries, the report of everything left out, and one-step undo.
//
// AGENT-NOTE: the owner has no Access files and mdb-tools is not installed
// on the build machines, so these captures are written by hand from
// mdbtools 1.0's backend.c (mdb_print_schema, generate_table_schema,
// mdb_print_indexes, mdb_get_relationships), mdb-export.c and
// mdb-queries.c — tabs, ", \n" separators and quoting as those print them.

#include "base_fixture.h"

#include "base_access.h"
#include "base_access_import.h"
#include "base_access_query.h"
#include "base_access_schema.h"
#include "base_objects.h"
#include "base_query.h"
#include "base_records.h"

#include <QFile>

using namespace BaseTest;

namespace {

const char kSchema[] =
    "-- ----------------------------------------------------------\n"
    "-- MDB Tools - A library for reading MS Access database files\n"
    "-- Copyright (C) 2000-2011 Brian Bruns and others.\n"
    "-- Files in libmdb are licensed under LGPL and the utilities under\n"
    "-- the GPL, see COPYING.LIB and COPYING files respectively.\n"
    "-- Check out http://mdbtools.sourceforge.net\n"
    "-- ----------------------------------------------------------\n\n"
    "SET client_encoding = 'UTF-8';\n\n"
    "CREATE TABLE IF NOT EXISTS \"members\"\n (\n"
    "\t\"memberid\"\t\t\tSERIAL, \n"
    "\t\"name\"\t\t\tVARCHAR (100) NOT NULL, \n"
    "\t\"email\"\t\t\tVARCHAR (255), \n"
    "\t\"joined\"\t\t\tDATE, \n"
    "\t\"lastvisit\"\t\t\tTIMESTAMP WITHOUT TIME ZONE, \n"
    "\t\"balance\"\t\t\tNUMERIC(15,2), \n"
    "\t\"rating\"\t\t\tDOUBLE PRECISION, \n"
    "\t\"active\"\t\t\tBOOLEAN NOT NULL, \n"
    "\t\"notes\"\t\t\tTEXT, \n"
    "\t\"photo\"\t\t\tBYTEA, \n"
    "\t\"badges\"\t\t\tUnknown_0012, \n"
    "\t\"guid\"\t\t\tUUID\n"
    ");\n"
    "COMMENT ON COLUMN \"members\".\"name\" IS 'The member''s full name';\n\n"
    "-- CREATE INDEXES ...\n"
    "ALTER TABLE \"members\" ADD CONSTRAINT \"members_pkey\" PRIMARY KEY (\"memberid\");\n"
    "CREATE UNIQUE INDEX \"members_email_idx\" ON \"members\" (\"email\");\n"
    "CREATE INDEX \"members_name_idx\" ON \"members\" (\"name\");\n\n"
    "CREATE TABLE IF NOT EXISTS \"dues\"\n (\n"
    "\t\"dueid\"\t\t\tSERIAL, \n"
    "\t\"memberid\"\t\t\tINTEGER NOT NULL, \n"
    "\t\"amount\"\t\t\tNUMERIC(15,2) NOT NULL, \n"
    "\t\"paidon\"\t\t\tTIMESTAMP WITHOUT TIME ZONE, \n"
    "\t\"period\"\t\t\tVARCHAR (20)\n"
    ");\n\n"
    "-- CREATE INDEXES ...\n"
    "ALTER TABLE \"dues\" ADD CONSTRAINT \"dues_pkey\" PRIMARY KEY (\"dueid\");\n"
    "CREATE UNIQUE INDEX \"dues_memberperiod_idx\" ON \"dues\" (\"memberid\", \"period\" DESC);\n\n"
    "CREATE TABLE IF NOT EXISTS \"events\"\n (\n"
    "\t\"eventid\"\t\t\tINTEGER, \n"
    "\t\"title\"\t\t\tVARCHAR (100), \n"
    "\t\"hostid\"\t\t\tINTEGER, \n"
    "\t\"fee\"\t\t\tNUMERIC(15,2), \n"
    "\t\"capacity\"\t\t\tNUMERIC (18, 0)\n"
    ");\n\n"
    "-- CREATE INDEXES ...\n"
    "ALTER TABLE \"events\" ADD CONSTRAINT \"events_pkey\" PRIMARY KEY (\"eventid\");\n\n\n"
    "-- CREATE Relationships ...\n"
    "ALTER TABLE \"Dues\" ADD CONSTRAINT \"dues_memberid_fk\" FOREIGN KEY (\"memberid\") REFERENCES "
    "\"Members\"(\"memberid\") ON UPDATE CASCADE ON DELETE CASCADE DEFERRABLE INITIALLY IMMEDIATE;\n"
    "-- Relationship from \"Events\" (\"hostid\") to \"Members\"(\"memberid\") does not enforce integrity.\n";

// mdb-export -b hex -D %Y-%m-%d -T "%Y-%m-%d %H:%M:%S": text, dates and
// binary quoted, NULL as nothing, money with four decimals, booleans 1/0.
const char kMembers[] =
    "MemberID,Name,Email,Joined,LastVisit,Balance,Rating,Active,Notes,Photo,Badges,GUID\n"
    "1,\"Ada Lovelace\",\"ada@example.org\",\"2026-03-14\",\"2026-03-14 18:30:00\",1250.5000,4.5,1,"
    "\"Likes \"\"engines\"\"; math\",\"89504E470D0A1A0A0000000D49484452\",7,"
    "\"7B33463235303430342D303030302D303030302D303030302D3030303030303030303030307D\"\n"
    "2,\"Grace Hopper\",,\"2026-01-02\",,0.0000,5,0,,,,\n"
    "5,\"Alan Turing\",\"alan@example.org\",,\"2026-02-01 09:00:00\",-20.2500,3.25,1,\"Line one\nline two\",,,\n";

const char kDues[] =
    "DueID,MemberID,Amount,PaidOn,Period\n"
    "1,1,25.0000,\"2026-01-05 00:00:00\",\"2026-01\"\n"
    "2,1,25.0000,,\"2026-02\"\n"
    "3,5,30.0000,\"2026-02-01 09:15:00\",\"2026-02\"\n";

const char kEvents[] =
    "EventID,Title,HostID,Fee,Capacity\n"
    "10,\"Engine night\",1,12.3456,40\n"
    "11,\"Math club\",9,0.0000,12\n";

const char kQueries[] = "Active members\nBig dues\nDoubled balances\nDues by member\nRaise dues\n~sq_cMembersForm\n"
                        "Overdue\nAsk domain\nPercent\nFrom form\n";

struct Query {
    const char *name;
    const char *sql;
};

const Query kQuerySql[] = {
    {"Active members", "SELECT Members.Name, Members.Email FROM [Members] WHERE (((Members.Active)=True)) ORDER BY "
                       "Members.Name\n"},
    {"Big dues", "SELECT TOP 5 Dues.MemberID,Dues.Amount FROM [Dues] WHERE (((Dues.Amount)>20) AND ((Dues.Period) Like "
                 "\"2026*\")) ORDER BY Dues.Amount DESCENDING\n"},
    {"Doubled balances", "SELECT Members.Name,[Balance]*2 FROM [Members] ORDER BY [Balance]*2 DESCENDING\n"},
    {"Dues by member", "SELECT Members.Name,Sum(Dues.Amount) FROM [Members],[Dues] \n"},
    {"Overdue", "SELECT Members.Name,Dues.Period FROM [Members],[Dues] WHERE (((Dues.PaidOn) Is Null)) \n"},
    {"Ask domain", "SELECT Members.Name FROM [Members] WHERE (((Members.Email) Like [Which domain?])) \n"},
    {"Percent", "SELECT TOP 10 PERCENT Members.Name FROM [Members] \n"},
    {"From form", "SELECT Members.Name FROM [Members] WHERE (((Members.MemberID)=[Forms]![Main]![Who])) \n"},
};

// MSysObjects through mdb-export -b strip (binary columns empty).
const char kObjects[] = "Connect,Database,DateCreate,DateUpdate,Flags,ForeignName,Id,Lv,LvExtra,LvModule,LvProp,Name,"
                        "Owner,ParentId,RmtInfoLong,RmtInfoShort,Type\n"
                        ",,\"2026-01-02 10:00:00\",\"2026-01-02 10:00:00\",0,,-2147483623,\"\",\"\",\"\",\"\","
                        "\"Members\",\"\",251658241,\"\",\"\",1\n"
                        ",,\"2026-01-03 10:00:00\",\"2026-01-03 10:00:00\",0,,-2147483622,\"\",\"\",\"\",\"\","
                        "\"Active members\",\"\",251658241,\"\",\"\",5\n"
                        ",,\"2026-01-03 10:00:00\",\"2026-01-03 10:00:00\",48,,-2147483621,\"\",\"\",\"\",\"\","
                        "\"Raise dues\",\"\",251658241,\"\",\"\",5\n";

// Fake mdb-tools in a directory that is all of PATH (so no real mdb-tools
// interferes; hence /bin/cat); they fail unless called
// the way the real programs must be.
struct FakeTools {
    QTemporaryDir dir;
    QByteArray oldPath = qgetenv("PATH");

    void file(const QString &name, const QByteArray &bytes)
    {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }
    void script(const char *name, const QByteArray &body)
    {
        const QString path = dir.filePath(str(name));
        file(str(name), "#!/bin/sh\nD='" + dir.path().toUtf8() + "'\n" + body);
        QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner);
    }
    explicit FakeTools(bool withSchema)
    {
        file(str("schema.sql"), kSchema);
        file(str("export-Members.csv"), kMembers);
        file(str("export-Dues.csv"), kDues);
        file(str("export-Events.csv"), kEvents);
        file(str("export-MSysObjects.csv"), kObjects);
        file(str("queries.txt"), kQueries);
        for (const Query &q : kQuerySql) {
            file(str("query-%1.sql").arg(str(q.name)), q.sql);
        }
        script("mdb-tables", "[ \"$1\" = -1 ] || exit 2\nprintf 'MSysObjects\\nMembers\\nDues\\nEvents\\n'\n");
        script("mdb-export", "[ \"$1\" = -b ] && [ \"$3\" = -D ] && [ \"$5\" = -T ] || exit 2\n"
                             "case \"$8\" in MSysObjects) [ \"$2\" = strip ] || exit 2;; *) [ \"$2\" = hex ] || exit 2;; esac\n"
                             "/bin/cat \"$D/export-$8.csv\"\n");
        if (withSchema) {
            script("mdb-schema", "[ \"$2\" = postgres ] || exit 2\n/bin/cat \"$D/schema.sql\"\n");
        }
        script("mdb-queries", "if [ \"$1\" = -L ]; then /bin/cat \"$D/queries.txt\"; else /bin/cat \"$D/query-$2.sql\"; fi\n");
        qputenv("PATH", dir.path().toUtf8());
    }
    ~FakeTools() { qputenv("PATH", oldPath); }
};

QHash<QString, QString> imported()
{
    return {{str("members"), str("Members")}, {str("dues"), str("Dues")}};
}

} // namespace

class BaseAccessTest final : public QObject {
    Q_OBJECT
private slots:
    void schemaParses()
    {
        const AccessSchema schema = parseAccessSchema(str(kSchema));
        QCOMPARE(schema.tables.size(), 3);
        const AccessTable *members = schema.table(str("Members"));
        QVERIFY(members);
        QCOMPARE(members->columns.size(), 12);
        QCOMPARE(members->column(str("Name"))->type, str("VARCHAR (100)"));
        QVERIFY(members->column(str("name"))->required);
        QCOMPARE(members->column(str("name"))->description, str("The member's full name"));
        QCOMPARE(members->primaryKey(), QStringList{str("memberid")});
        QCOMPARE(members->indexes.size(), 3);
        QVERIFY(members->indexes.at(1).unique);
        const AccessTable *dues = schema.table(str("dues"));
        QCOMPARE(dues->indexes.at(1).columns, (QStringList{str("memberid"), str("period")}));
        QCOMPARE(dues->indexes.at(1).descending, (QList<bool>{false, true}));
        QCOMPARE(schema.relationships.size(), 2);
        QCOMPARE(schema.relationships.first().childTable, str("Dues"));
        QVERIFY(schema.relationships.first().cascadeUpdate && schema.relationships.first().cascadeDelete);
        QVERIFY(!schema.relationships.at(1).enforced);
        QCOMPARE(schema.relationships.at(1).parentColumn, str("memberid"));
        const QList<std::optional<FieldType>> expected{FieldType::AutoNumber, FieldType::Text, FieldType::Text,
                                                       FieldType::Date, FieldType::DateTime, FieldType::Currency,
                                                       FieldType::Number, FieldType::YesNo, FieldType::LongText,
                                                       FieldType::File, std::nullopt, FieldType::Text};
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(accessFieldType(members->columns.at(i)), expected.at(i));
        }
        QCOMPARE(accessFieldType(schema.table(str("events"))->columns.last()), std::optional(FieldType::Number));
    }

    void queriesConvertOrSayWhyNot()
    {
        AccessQueryConversion c = convertAccessQuery(str(kQuerySql[1].sql), imported());
        QVERIFY2(c.design, qPrintable(c.reason));
        QCOMPARE(c.design->top, 5);
        QCOMPARE(c.design->tables.first().name, str("Dues"));
        QCOMPARE(c.design->columns.size(), 3); // MemberID, Amount, hidden Period
        QCOMPARE(c.design->columns.at(1).criteria, str(">20"));
        QCOMPARE(c.design->columns.at(1).sort, -1);
        QCOMPARE(c.design->columns.at(2).criteria, str("Like \"2026*\""));
        QVERIFY(!c.design->columns.at(2).show);
        c = convertAccessQuery(str(kQuerySql[2].sql), imported());
        QVERIFY(c.design);
        QCOMPARE(c.design->columns.at(1).expression, str("[Balance]*2"));
        QCOMPARE(c.design->columns.at(2).sort, -1);
        QVERIFY(!c.design->columns.at(2).show);
        // A test that is not a list of per-field conditions stays whole.
        c = convertAccessQuery(str("SELECT Members.Name FROM [Members] WHERE ((Members.Rating>4) OR (Members.Active=True))"),
                               imported());
        QVERIFY(c.design);
        QCOMPARE(c.design->columns.last().expression, str("(Members.Rating>4) OR (Members.Active=True)"));
        QCOMPARE(c.design->columns.last().criteria, str("<> False"));
        // Between keeps its And.
        c = convertAccessQuery(str("SELECT * FROM [Dues] WHERE (((Dues.Amount) Between 10 And 30))"), imported());
        QVERIFY(c.design);
        QCOMPARE(c.design->columns.first().field, str("*"));
        QCOMPARE(c.design->columns.last().criteria, str("Between 10 And 30"));
        const QHash<QString, QString> renamed{{str("members"), str("Members 2")}};
        c = convertAccessQuery(str(kQuerySql[0].sql), renamed);
        QCOMPARE(c.design->tables.first().alias, str("Members")); // expressions keep their Access name
        QVERIFY(convertAccessQuery(str(kQuerySql[3].sql), imported()).reason.contains(str("Group By")));
        QVERIFY(convertAccessQuery(str(kQuerySql[4].sql), imported()).reason.contains(str("joins 2 tables")));
        QVERIFY(convertAccessQuery(str(kQuerySql[6].sql), imported()).reason.contains(str("PERCENT")));
        QVERIFY(convertAccessQuery(str(kQuerySql[7].sql), imported()).reason.contains(str("form")));
        QVERIFY(convertAccessQuery(str("SELECT  FROM  "), imported()).reason.contains(str("could not rebuild")));
        QVERIFY(convertAccessQuery(str("SELECT Events.Title FROM [Events] "), imported()).reason.contains(str("Events")));
        QCOMPARE(accessQueryKind(48), str("update"));
        QCOMPARE(accessQueryKind(0), str("select"));
        QCOMPARE(accessQueryKind(128), str("union"));
    }

    void wholeImportKeepsTheDesign()
    {
        FakeTools tools(true);
        Db db;
        const AccessTools found = findAccessTools();
        QVERIFY(found.available && !found.schemaProgram.isEmpty() && !found.queriesProgram.isEmpty());
        MdbToolsReader reader(found, str("club.accdb"));
        QString error;
        QCOMPARE(reader.tables(&error), (QStringList{str("Members"), str("Dues"), str("Events")}));
        QList<Change> changes;
        const AccessImportReport report =
            importAccessDatabase(*db.file, reader, {{str("Members"), str("Dues"), str("Events")}, true}, &changes);
        QVERIFY2(report.ok, qPrintable(report.error + report.skipped.join(str("\n"))));
        QCOMPARE(report.tables.size(), 3);
        QCOMPARE(report.tables.first().rows, 3);
        // The design: AutoNumber key with Access's own values, types, required, descriptions, indexes.
        const TableDef members = readTable(*db.file, str("Members")).value();
        QCOMPARE(members.fields.size(), 11); // Badges (an attachment field) left out
        const QList<FieldType> types{FieldType::AutoNumber, FieldType::Text,   FieldType::Text,  FieldType::Date,
                                     FieldType::DateTime,   FieldType::Currency, FieldType::Number, FieldType::YesNo,
                                     FieldType::LongText,   FieldType::File,     FieldType::Text};
        for (int i = 0; i < types.size(); ++i) {
            QCOMPARE(members.fields.at(i).type, types.at(i));
        }
        QVERIFY(members.fields.at(1).required);
        QCOMPARE(members.fields.at(1).description, str("The member's full name"));
        QVERIFY(members.fields.at(1).indexed);
        QVERIFY(members.fields.at(2).unique);
        QCOMPARE(db.scalar(str("SELECT Name FROM Members WHERE MemberID = 5")).toString(), str("Alan Turing"));
        QCOMPARE(db.scalar(str("SELECT Balance FROM Members WHERE MemberID = 1")).toLongLong(), qint64(125050));
        QCOMPARE(db.scalar(str("SELECT Notes FROM Members WHERE MemberID = 5")).toString(), str("Line one\nline two"));
        QCOMPARE(db.scalar(str("SELECT GUID FROM Members WHERE MemberID = 1")).toString(),
                 str("{3F250404-0000-0000-0000-000000000000}"));
        QCOMPARE(Records::readFile(*db.file, str("Members"), 1, str("Photo"), &error).value(),
                 QByteArray::fromHex("89504E470D0A1A0A0000000D49484452"));
        const TableDef dues = readTable(*db.file, str("Dues")).value();
        QCOMPARE(dues.fields.at(3).type, FieldType::DateTime);
        QCOMPARE(dues.otherIndexSql.size(), 1);
        QVERIFY(dues.otherIndexSql.first().contains(str("\"Period\" DESC")));
        QCOMPARE(readTable(*db.file, str("Events"))->fields.at(3).type, FieldType::Number); // fractions of a cent
        QVERIFY(report.notes.join(str(" ")).contains(str("Events.Fee")));
        QVERIFY(report.skipped.join(str(" ")).contains(str("Members.Badges")));
        // Relationships: the enforced one with its cascades; the other is reported.
        QCOMPARE(report.relationships, QStringList{str("Dues.MemberID → Members.MemberID")});
        const Relationship rel = readTable(*db.file, str("Dues"))->relationships.value(0);
        QCOMPARE(rel.onDelete, RefAction::Cascade);
        QCOMPARE(rel.onUpdate, RefAction::Cascade);
        QVERIFY(report.skipped.join(str(" ")).contains(str("does not enforce it")));
        // Queries: the convertible selects run; the rest are named with a reason.
        QCOMPARE(report.queries, (QStringList{str("Active members"), str("Big dues"), str("Doubled balances"),
                                              str("Ask domain")}));
        const QString skipped = report.skipped.join(str("\n"));
        for (const char *name : {"Dues by member", "Raise dues", "Overdue", "Percent", "From form"}) {
            QVERIFY2(skipped.contains(str("Query %1:").arg(str(name))), name);
        }
        QVERIFY(!skipped.contains(str("~sq_")));
        const SavedObject active = ObjectStore::load(*db.file, str("query"), str("Active members")).value();
        const CompiledQuery compiled = compileQuery(*db.file, QueryDesign::fromMap(active.definition));
        QVERIFY2(compiled.ok, qPrintable(compiled.error));
        const QueryResult result = runSelect(*db.file, compiled.sql, compiled.parameters, {});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rows.size(), 2);
        QCOMPARE(result.rows.first().first().toString(), str("Ada Lovelace"));
        const CompiledQuery ask = compileQuery(
            *db.file, QueryDesign::fromMap(ObjectStore::load(*db.file, str("query"), str("Ask domain"))->definition));
        QCOMPARE(ask.parameters.size(), 1);
        QCOMPARE(ask.parameters.first().prompt, str("Which domain?"));
        // The whole import is undone in one step.
        for (auto it = changes.crbegin(); it != changes.crend(); ++it) {
            QVERIFY(db.file->applyChange(*it, true).ok);
        }
        QVERIFY(!db.file->tableExists(str("Members")));
        QVERIFY(ObjectStore::list(*db.file, str("query")).isEmpty());
    }

    void withoutTheSchemaTypesAreDetected()
    {
        FakeTools tools(false);
        Db db;
        MdbToolsReader reader(findAccessTools(), str("club.accdb"));
        QList<Change> changes;
        const AccessImportReport report = importAccessDatabase(*db.file, reader, {{str("Members")}, false}, &changes);
        QVERIFY(report.ok);
        QVERIFY(report.notes.join(str(" ")).contains(str("could not be read")));
        const TableDef members = readTable(*db.file, str("Members")).value();
        QCOMPARE(members.fields.at(members.fieldIndex(str("Photo"))).type, FieldType::File);
        QCOMPARE(members.fields.at(members.fieldIndex(str("Joined"))).type, FieldType::Date);
        QVERIFY(report.relationships.isEmpty());
    }

    void missingToolsSayWhatToInstall()
    {
        QTemporaryDir empty;
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", empty.path().toUtf8());
        const AccessTools tools = findAccessTools();
        qputenv("PATH", oldPath);
        QVERIFY(!tools.available);
        QString error;
        QVERIFY(accessTableNames(tools, str("x.accdb"), &error).isEmpty());
        QVERIFY(error.contains(str("app-office/mdbtools")));
        QVERIFY(parseQueryFlags(parseCsv(QByteArray(kObjects))).value(str("Raise dues")) == 48);
    }
};

BASE_TEST_MAIN(BaseAccessTest)

#include "tst_base_access.moc"
