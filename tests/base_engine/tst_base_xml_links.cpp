// SPDX-License-Identifier: GPL-3.0-or-later
// M86's interchange (D-094) against hand-worked results: Access's XML
// import — a round trip through M80's export keeping every type, the key
// and its numbers, required fields, indexes and a calculated field; an
// Access-shaped file with its schema embedded; a file with no schema — and
// linked tables to another .qbase and to a CSV file: read-only copies,
// refreshed (one undo step), redesigned when the source changes, kept when
// the source goes missing, relinked and converted to local tables.

#include "base_fixture.h"

#include "base_data_macro.h"
#include "base_indexes.h"
#include "base_links.h"
#include "base_records.h"
#include "base_xml_export.h"
#include "base_xml_import.h"

#include <QFile>

using namespace BaseTest;

namespace {

bool write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

FieldDef calculated(const char *name, const char *expression)
{
    FieldDef f;
    f.name = str(name);
    f.calculated = true;
    f.expression = str(expression);
    f.type = FieldType::Currency;
    return f;
}

QList<QVariantList> records(Db &db, const char *table)
{
    return db.file->db().rows(QStringLiteral("SELECT * FROM %1 ORDER BY 1").arg(quoteIdentifier(str(table))));
}

} // namespace

class BaseXmlLinksTest final : public QObject {
    Q_OBJECT
private slots:
    void xmlRoundTripKeepsTheDesign()
    {
        Db source;
        TableDef members = table("members", {field("id", FieldType::AutoNumber), field("name", FieldType::Text, true),
                                             field("joined", FieldType::Date), field("last_visit", FieldType::DateTime),
                                             field("dues", FieldType::Currency), field("active", FieldType::YesNo),
                                             field("notes", FieldType::LongText), field("site", FieldType::Hyperlink),
                                             field("big", FieldType::LargeNumber), field("photo", FieldType::File),
                                             field("ratio", FieldType::Number), calculated("twice", "[dues] * 2")});
        members.fields[1].unique = true;
        IndexDef byJoin;
        byJoin.name = str("ByJoined");
        byJoin.fields = {{str("joined"), false}, {str("active"), false}};
        members.indexes = {byJoin};
        QVERIFY2(source.create(members).ok, qPrintable(planCreateTable(*source.file, members).error));
        QVERIFY(source.exec(str("INSERT INTO members(id, name, joined, last_visit, dues, active, notes, site, big, photo, ratio) "
                                "VALUES(1, 'Ada Lovelace', '2026-03-14', '2026-03-14 09:30:00', 2500, 1, 'Line one\nline two', "
                                "'Docs#https://example.org#', 9007199254740993, X'89504E47', 0.5),"
                                "(5, 'Grace', NULL, NULL, 199, 0, NULL, NULL, NULL, NULL, NULL)"))
                    .ok);
        const TableDef design = readTable(*source.file, str("members")).value();
        const RowSet set = Records::read(*source.file, str("members"));
        QList<QVariantList> rows;
        for (const QVariantList &r : records(source, "members")) {
            rows.append(r);
        }
        const AccessXml xml = accessXml(str("members"), set.columns, rows, source.file->currency(), &design,
                                        str("members.xsd"));
        QVERIFY(write(source.path("members.xml"), xml.data));
        QVERIFY(write(source.path("members.xsd"), xml.schema));
        Db target;
        const AccessXmlDocument doc = readAccessXml(source.path("members.xml"));
        QVERIFY2(doc.ok, qPrintable(doc.error));
        QCOMPARE(doc.schemaSource, str("members.xsd"));
        QCOMPARE(doc.tables.size(), 1);
        QCOMPARE(doc.tables.first().records, qint64(2));
        QList<Change> changes;
        const AccessImportReport report = importAccessXml(*target.file, doc, {}, &changes);
        QVERIFY2(report.ok, qPrintable(report.error + report.skipped.join(str("; "))));
        QCOMPARE(report.tables.first().rows, 2);
        QCOMPARE(changes.size(), 3); // the table, its records, the calculated field
        const TableDef back = readTable(*target.file, str("members")).value();
        QCOMPARE(back.fields.size(), design.fields.size());
        for (int i = 0; i < design.fields.size(); ++i) {
            QCOMPARE(back.fields.at(i).name, design.fields.at(i).name);
            QCOMPARE(back.fields.at(i).type, design.fields.at(i).type);
            QCOMPARE(back.fields.at(i).calculated, design.fields.at(i).calculated);
        }
        QVERIFY(back.fields.at(0).primaryKey);
        QVERIFY(back.fields.at(1).required);
        QVERIFY(back.fields.at(1).unique);
        QCOMPARE(back.field(str("twice"))->expression, str("[dues] * 2"));
        QCOMPARE(back.indexes.size(), 1);
        QCOMPARE(back.indexes.first().name, str("ByJoined"));
        QCOMPARE(records(target, "members"), records(source, "members"));
        // Undoing the import's steps takes it all away.
        for (qsizetype i = changes.size() - 1; i >= 0; --i) {
            QVERIFY(target.file->applyChange(changes.at(i), true).ok);
        }
        QVERIFY(!target.file->tableExists(str("members")));
    }

    void accessShapedAndSchemalessXml()
    {
        Db db;
        // (built line by line: moc does not read raw string literals)
        const QByteArray access = QStringList{
            str("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"),
            str("<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:od=\"urn:schemas-microsoft-com:officedata\">"),
            str("<xsd:schema>"),
            str("<xsd:element name=\"dataroot\"><xsd:complexType><xsd:sequence>"),
            str("<xsd:element ref=\"Order_x0020_Lines\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xsd:sequence></xsd:complexType></xsd:element>"),
            str("<xsd:element name=\"Order_x0020_Lines\">"),
            str("<xsd:annotation><xsd:appinfo><od:index index-name=\"PrimaryKey\" index-key=\"LineID \" primary=\"yes\" unique=\"yes\" clustered=\"no\" order=\"asc\"/></xsd:appinfo></xsd:annotation>"),
            str("<xsd:complexType><xsd:sequence>"),
            str("<xsd:element name=\"LineID\" minOccurs=\"1\" od:jetType=\"autonumber\" od:sqlSType=\"int\" od:autoUnique=\"yes\" od:nonNullable=\"yes\" type=\"xsd:int\"/>"),
            str("<xsd:element name=\"Item_x0020_Name\" minOccurs=\"0\" od:jetType=\"text\" od:sqlSType=\"nvarchar\"><xsd:simpleType><xsd:restriction base=\"xsd:string\"><xsd:maxLength value=\"50\"/></xsd:restriction></xsd:simpleType></xsd:element>"),
            str("<xsd:element name=\"Price\" minOccurs=\"0\" od:jetType=\"currency\" od:sqlSType=\"money\" type=\"xsd:double\"/>"),
            str("<xsd:element name=\"Shipped\" minOccurs=\"0\" od:jetType=\"datetime\" od:sqlSType=\"datetime\" type=\"xsd:dateTime\"/>"),
            str("<xsd:element name=\"Paid\" minOccurs=\"1\" od:jetType=\"yesno\" od:sqlSType=\"bit\" od:nonNullable=\"yes\" type=\"xsd:boolean\"/>"),
            str("</xsd:sequence></xsd:complexType></xsd:element>"),
            str("</xsd:schema>"),
            str("<dataroot xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"),
            str("<Order_x0020_Lines><LineID>7</LineID><Item_x0020_Name>Blue pen</Item_x0020_Name><Price>1.5</Price><Shipped>2026-03-14T00:00:00</Shipped><Paid>1</Paid></Order_x0020_Lines>"),
            str("<Order_x0020_Lines><LineID>9</LineID><Item_x0020_Name>Ink</Item_x0020_Name><Shipped>2026-04-01T00:00:00</Shipped><Paid>0</Paid></Order_x0020_Lines>"),
            str("</dataroot>"),
            str("</root>")}.join(QLatin1Char('\n')).toUtf8();
        const AccessXmlDocument doc = readAccessXml(db.path("orders.xml"), access);
        QVERIFY2(doc.ok, qPrintable(doc.error));
        QCOMPARE(doc.schemaSource, str("embedded"));
        QList<Change> changes;
        const AccessImportReport report = importAccessXml(*db.file, doc, {}, &changes);
        QVERIFY2(report.ok, qPrintable(report.error + report.skipped.join(str("; "))));
        const TableDef t = readTable(*db.file, str("Order Lines")).value();
        QCOMPARE(t.fields.at(0).type, FieldType::AutoNumber);
        QCOMPARE(t.fields.at(1).name, str("Item Name"));
        QCOMPARE(t.fields.at(2).type, FieldType::Currency);
        QCOMPARE(t.fields.at(3).type, FieldType::Date); // every value at midnight
        QCOMPARE(t.fields.at(4).type, FieldType::YesNo);
        QCOMPARE(records(db, "Order Lines"),
                 (QList<QVariantList>{{qint64(7), str("Blue pen"), qint64(150), str("2026-03-14"), qint64(1)},
                                      {qint64(9), str("Ink"), QVariant(), str("2026-04-01"), qint64(0)}}));
        // No schema: the types come from the values.
        const QByteArray bare = "<dataroot><stock><item>Pen</item><count>12</count><seen>2026-01-02T08:15:00</seen></stock>"
                                "<stock><item>Ink</item><count>3</count><seen>2026-01-05T17:00:00</seen></stock></dataroot>";
        const AccessXmlDocument plain = readAccessXml(db.path("stock.xml"), bare);
        QVERIFY2(plain.ok, qPrintable(plain.error));
        const AccessImportReport second = importAccessXml(*db.file, plain, {}, &changes);
        QVERIFY2(second.ok, qPrintable(second.error));
        QVERIFY(second.notes.join(str(" ")).contains(str("no schema")));
        const TableDef stock = readTable(*db.file, str("stock")).value();
        QCOMPARE(stock.field(str("count"))->type, FieldType::Integer);
        QCOMPARE(stock.field(str("seen"))->type, FieldType::DateTime);
        QVERIFY(!readAccessXml(db.path("bad.xml"), "<dataroot><a>").ok);
    }

    void linkedTablesFollowTheirSource()
    {
        Db source;
        QVERIFY(source.create(table("members", {field("id", FieldType::AutoNumber), field("name", FieldType::Text),
                                                field("dues", FieldType::Currency)}))
                    .ok);
        QVERIFY(source.exec(str("INSERT INTO members(name, dues) VALUES('Ada', 2500), ('Grace', 199)")).ok);
        Db db;
        LinkSource link;
        link.kind = str("qbase");
        link.path = source.path("test.qbase");
        link.table = str("members");
        const LinkResult linked = linkTable(*db.file, str("club"), link, {});
        QVERIFY2(linked.ok, qPrintable(linked.error));
        QCOMPARE(linked.rows, qint64(2));
        QCOMPARE(linked.changes.size(), 2);
        TableDef club = readTable(*db.file, str("club")).value();
        QCOMPARE(LinkSource::fromMap(club.link).table, str("members"));
        QCOMPARE(club.field(str("dues"))->type, FieldType::Currency);
        QVERIFY(club.field(str("id"))->primaryKey);
        QCOMPARE(linkedTables(*db.file).size(), 1);
        // Read-only: QindaBase, other tools, designs, relationships, macros.
        QVERIFY(!Records::read(*db.file, str("club")).editable);
        Change change;
        QVERIFY(Records::updateCells(*db.file, str("club"), {{1, str("name"), str("Bo")}}, &change).error.contains(str("linked")));
        const Result other = db.exec(str("UPDATE club SET name = 'Bo'"));
        QVERIFY(!other.ok);
        QVERIFY2(other.error.contains(str("is linked to members in test.qbase")), qPrintable(other.error));
        TableDef changed = club;
        changed.fields.append(field("extra", FieldType::Text));
        QVERIFY(planAlterTable(*db.file, club, changed).error.contains(str("is linked")));
        QVERIFY(db.create(table("visits", {field("id", FieldType::AutoNumber), field("member", FieldType::Integer)})).ok);
        Relationship r{str("visits"), str("member"), str("club"), str("id"), RefAction::NoAction, RefAction::NoAction};
        QVERIFY(planAddRelationship(*db.file, r).error.contains(str("linked")));
        QVERIFY(planDataMacro(*db.file, str("club"), str("afterInsert"), Macro()).error.contains(str("linked")));
        // The source changes; a refresh is one undo step.
        QVERIFY(source.exec(str("INSERT INTO members(name, dues) VALUES('Alan', 3333)")).ok);
        LinkResult refreshed = refreshLink(*db.file, str("club"), {}, true);
        QVERIFY2(refreshed.ok, qPrintable(refreshed.error));
        QCOMPARE(db.scalar(str("SELECT count(*) FROM club")).toLongLong(), qint64(3));
        QCOMPARE(refreshed.changes.size(), 1);
        QVERIFY(db.file->applyChange(refreshed.changes.first(), true).ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM club")).toLongLong(), qint64(2));
        QVERIFY(db.file->applyChange(refreshed.changes.first(), false).ok);
        // The source gains a field: the copy is made again to match.
        const TableDef before = readTable(*source.file, str("members")).value();
        TableDef wider = before;
        wider.fields.append(field("city", FieldType::Text));
        QVERIFY(source.apply(planAlterTable(*source.file, before, wider)).ok);
        QVERIFY(source.exec(str("UPDATE members SET city = 'Salem' WHERE name = 'Grace'")).ok);
        refreshed = refreshLink(*db.file, str("club"), {}, false);
        QVERIFY2(refreshed.ok, qPrintable(refreshed.error));
        QVERIFY(refreshed.redesigned);
        QCOMPARE(db.scalar(str("SELECT city FROM club WHERE name = 'Grace'")).toString(), str("Salem"));
        QVERIFY(!db.exec(str("DELETE FROM club")).ok); // still guarded
        // The source goes missing: the last copy stays and says why.
        const QString moved = source.path("moved.qbase");
        source.file.reset();
        QVERIFY(QFile::rename(link.path, moved));
        const QList<LinkResult> atOpen = refreshLinks(*db.file, {});
        QCOMPARE(atOpen.size(), 1);
        QVERIFY(!atOpen.first().ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM club")).toLongLong(), qint64(3));
        QVERIFY(LinkSource::fromMap(readTable(*db.file, str("club"))->link).problem.contains(str("not found")));
        QVERIFY(db.apply(planRelinkTable(*db.file, str("club"), moved)).ok);
        QVERIFY(refreshLink(*db.file, str("club"), {}, false).ok);
        QVERIFY(LinkSource::fromMap(readTable(*db.file, str("club"))->link).problem.isEmpty());
        // Converted to a local table: editable, unguarded.
        QVERIFY(db.apply(planUnlinkTable(*db.file, str("club"))).ok);
        QVERIFY(Records::read(*db.file, str("club")).editable);
        QVERIFY(db.exec(str("UPDATE club SET name = 'Bo' WHERE name = 'Ada'")).ok);
        QVERIFY(linkedTables(*db.file).isEmpty());
    }

    void csvLinksRefresh()
    {
        Db db;
        const QString csv = db.path("members.csv");
        QVERIFY(write(csv, "name,joined,dues\nAda,2026-03-14,$25.00\nGrace,2026-01-02,$1.99\n"));
        LinkSource link;
        link.kind = str("csv");
        link.path = csv;
        const LinkResult linked = linkTable(*db.file, str("csv_members"), link, {});
        QVERIFY2(linked.ok, qPrintable(linked.error));
        const TableDef t = readTable(*db.file, str("csv_members")).value();
        QCOMPARE(t.field(str("joined"))->type, FieldType::Date);
        QCOMPARE(t.field(str("dues"))->type, FieldType::Currency);
        QCOMPARE(LinkSource::fromMap(t.link).relativePath, str("members.csv"));
        QVERIFY(write(csv, "name,joined,dues\nAda,2026-03-14,$25.00\nGrace,2026-01-02,$1.99\nAlan,2026-02-02,$33.33\n"));
        QVERIFY(refreshLink(*db.file, str("csv_members"), {}, true).ok);
        QCOMPARE(db.scalar(str("SELECT sum(dues) FROM csv_members")).toLongLong(), qint64(6032));
        LinkSource excel;
        excel.kind = str("xlsx");
        excel.path = csv; // without the window's reader an Excel link says so
        QVERIFY(linkTable(*db.file, str("sheet"), excel, {}).error.contains(str("window")));
    }
};

BASE_TEST_MAIN(BaseXmlLinksTest)

#include "tst_base_xml_links.moc"
