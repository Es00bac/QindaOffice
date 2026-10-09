// SPDX-License-Identifier: GPL-3.0-or-later
// Datasheet records: reads with sort and filter, typed edits with
// understandable refusals, lookups by name, delete impact and cascades,
// tab-separated paste, exact totals — every edit one undoable change.

#include "base_fixture.h"

#include "base_file_field.h"
#include "base_records.h"
#include "base_snapshot.h"

using namespace BaseTest;

namespace {

struct Seeded : Db {
    Seeded()
    {
        donorsAndDonations(RefAction::Restrict);
        exec(str("INSERT INTO donors(name, email, active) VALUES('Ada Lovelace', 'ada@x.org', 1), ('Grace Hopper', "
                 "'grace@x.org', 1), ('Alan Turing', NULL, 0)"));
        exec(str("INSERT INTO donations(donor_id, amount, given) VALUES(1, 2500, '2026-01-05'), (1, 1000, '2026-02-10'), "
                 "(2, 12345, '2026-02-11'), (3, 1, '2026-03-01')"));
    }
};

} // namespace

class BaseRecordsTest final : public QObject {
    Q_OBJECT
private slots:
    void readsWithSortAndFilter()
    {
        Seeded db;
        RowSet all = Records::read(*db.file, str("donations"));
        QVERIFY2(all.error.isEmpty(), qPrintable(all.error));
        QCOMPARE(all.rowids.size(), 4);
        QCOMPARE(all.totalRows, qint64(4));
        QVERIFY(all.editable);
        // A relationship without an explicit lookup still shows the parent's name.
        const ColumnInfo &donor = all.columns.at(all.columnIndex(str("donor_id")));
        QCOMPARE(donor.lookup.table, str("donors"));
        QCOMPARE(donor.lookup.displayField, str("name"));
        RowQuery q;
        q.sort = {{str("amount"), true}};
        q.filters = {{str("amount"), str("ge"), str("10.00")}};
        const RowSet big = Records::read(*db.file, str("donations"), q);
        QCOMPARE(big.rowids.size(), 3);
        QCOMPARE(big.values.first().at(big.columnIndex(str("amount"))).toLongLong(), qint64(12345));
        q.filters = {{str("given"), str("lt"), str("2/1/2026")}};
        QCOMPARE(Records::read(*db.file, str("donations"), q).rowids.size(), 1);
        q.filters = {{str("amount"), str("gt"), str("lots")}};
        QVERIFY(Records::read(*db.file, str("donations"), q).error.contains(str("amount")));
        RowQuery emails;
        emails.filters = {{str("email"), str("isNull"), QString()}};
        QCOMPARE(Records::read(*db.file, str("donors"), emails).rowids, QList<qint64>{3});
        emails.filters = {{str("name"), str("contains"), str("HOP")}};
        QCOMPARE(Records::read(*db.file, str("donors"), emails).rowids, QList<qint64>{2});
        RowQuery limited;
        limited.limit = 2;
        const RowSet part = Records::read(*db.file, str("donations"), limited);
        QVERIFY(part.truncated);
        QCOMPARE(part.totalRows, qint64(4));
    }

    void editsAreTypedAndUndoable()
    {
        Seeded db;
        Change change;
        QVERIFY(Records::updateCells(*db.file, str("donations"), {{1, str("amount"), str("$1,234.50")}}, &change).ok);
        QCOMPARE(db.scalar(str("SELECT amount FROM donations WHERE id=1")).toLongLong(), qint64(123450));
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(db.scalar(str("SELECT amount FROM donations WHERE id=1")).toLongLong(), qint64(2500));
        Result r = Records::updateCells(*db.file, str("donations"), {{1, str("amount"), str("12.345")}}, nullptr);
        QVERIFY(r.error.contains(str("amount takes at most 2 decimal places")));
        r = Records::updateCells(*db.file, str("donations"), {{1, str("id"), str("9")}}, nullptr);
        QVERIFY(r.error.contains(str("numbered by the database")));
        r = Records::updateCells(*db.file, str("donors"), {{1, str("name"), QString()}}, nullptr);
        QCOMPARE(r.error, str("name is required."));
        r = Records::updateCells(*db.file, str("donors"), {{1, str("email"), str("grace@x.org")}}, nullptr);
        QVERIFY2(r.error.contains(str("must be unique")), qPrintable(r.error));
        // A lookup field takes the related record's name.
        QVERIFY(Records::updateCells(*db.file, str("donations"), {{4, str("donor_id"), str("grace hopper")}}, nullptr).ok);
        QCOMPARE(db.scalar(str("SELECT donor_id FROM donations WHERE id=4")).toLongLong(), qint64(2));
        r = Records::updateCells(*db.file, str("donations"), {{4, str("donor_id"), str("Nobody")}}, nullptr);
        QVERIFY(r.error.contains(str("no match in donors")));
        r = Records::updateCells(*db.file, str("donations"), {{4, str("donor_id"), str("77")}}, nullptr);
        QVERIFY(r.error.contains(str("relationship")));
        // Several cells in one step.
        QVERIFY(Records::updateCells(*db.file, str("donors"),
                                     {{1, str("active"), str("No")}, {2, str("active"), QVariant(false)}}, &change)
                    .ok);
        QCOMPARE(db.scalar(str("SELECT sum(active) FROM donors")).toInt(), 0);
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(db.scalar(str("SELECT sum(active) FROM donors")).toInt(), 2);
    }

    void newRecordsNeedTheirRequiredFields()
    {
        Seeded db;
        qint64 rowid = 0;
        Change change;
        const Result missing = Records::insertRecord(*db.file, str("donations"), {{str("given"), str("2026-03-03")}},
                                                     &rowid, nullptr);
        QCOMPARE(missing.error, str("Fill in donor_id and amount to save the record."));
        QVERIFY(Records::insertRecord(*db.file, str("donations"),
                                      {{str("donor_id"), str("Ada Lovelace")}, {str("amount"), QVariant(50.25)}}, &rowid,
                                      &change)
                    .ok);
        QCOMPARE(rowid, qint64(5));
        QCOMPARE(db.scalar(str("SELECT amount FROM donations WHERE id=5")).toLongLong(), qint64(5025));
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donations")).toInt(), 4);
    }

    void deletesExplainTheirReach()
    {
        Seeded db;
        bool blocked = false;
        QStringList impact = Records::deleteImpact(*db.file, str("donors"), {1}, &blocked);
        QVERIFY(blocked); // Restrict: Ada has donations
        QVERIFY(impact.first().contains(str("2 record(s) in donations")));
        QVERIFY(!Records::deleteRecords(*db.file, str("donors"), {1}, nullptr).ok);
        Change change;
        QVERIFY(Records::deleteRecords(*db.file, str("donations"), {1, 2}, &change).ok);
        QVERIFY(Records::deleteImpact(*db.file, str("donors"), {1}, &blocked).isEmpty());
        QVERIFY(Records::deleteRecords(*db.file, str("donors"), {1}, nullptr).ok);
        Db c;
        QVERIFY(c.donorsAndDonations(RefAction::Cascade));
        c.exec(str("INSERT INTO donors(name) VALUES('Ada')"));
        c.exec(str("INSERT INTO donations(donor_id, amount) VALUES(1, 100), (1, 200)"));
        impact = Records::deleteImpact(*c.file, str("donors"), {1}, &blocked);
        QVERIFY(!blocked);
        QCOMPARE(impact, QStringList{str("Also deletes 2 related record(s) in donations.")});
        QVERIFY(Records::deleteRecords(*c.file, str("donors"), {1}, &change).ok);
        QCOMPARE(c.scalar(str("SELECT count(*) FROM donations")).toInt(), 0);
        QVERIFY(c.file->applyChange(change, true).ok);
        QCOMPARE(c.scalar(str("SELECT count(*) FROM donations")).toInt(), 2);
    }

    void pastesTabSeparatedRows()
    {
        Seeded db;
        const QList<QStringList> parsed = Records::parseTabular(str("a\t\"b\tc\"\n\"say \"\"hi\"\"\"\tx\r\n"));
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed.at(0), (QStringList{str("a"), str("b\tc")}));
        QCOMPARE(parsed.at(1), (QStringList{str("say \"hi\""), str("x")}));
        // Two rows over existing donors (name, email), one appended.
        const QList<qint64> order{1, 2, 3};
        int updated = 0;
        int inserted = 0;
        Change change;
        const Result r = Records::pasteText(*db.file, str("donors"), order, 1, str("name"),
                                            str("Grace M. Hopper\tgmh@x.org\nA. Turing\tat@x.org\nKatherine "
                                                "Johnson\tkj@x.org\n"),
                                            &change, &updated, &inserted);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(updated, 2);
        QCOMPARE(inserted, 1);
        QCOMPARE(db.scalar(str("SELECT name FROM donors WHERE id=3")).toString(), str("A. Turing"));
        QCOMPARE(db.scalar(str("SELECT email FROM donors WHERE id=4")).toString(), str("kj@x.org"));
        QVERIFY(db.file->applyChange(change, true).ok); // one undo step for the whole paste
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donors")).toInt(), 3);
        QCOMPARE(db.scalar(str("SELECT name FROM donors WHERE id=3")).toString(), str("Alan Turing"));
        const Result bad = Records::pasteText(*db.file, str("donations"), {1, 2, 3, 4}, 0, str("amount"),
                                              str("10\n20\nlots\n"), nullptr, nullptr, nullptr);
        QVERIFY2(bad.error.startsWith(str("Row 3:")), qPrintable(bad.error));
        QCOMPARE(db.scalar(str("SELECT amount FROM donations WHERE id=1")).toLongLong(), qint64(2500)); // nothing kept
    }

    void totalsAreExact()
    {
        Seeded db;
        QString display;
        QCOMPARE(Records::total(*db.file, str("donations"), str("amount"), str("sum"), {}, &display).toLongLong(),
                 qint64(15846));
        QCOMPARE(display, str("$158.46"));
        QVERIFY(Records::total(*db.file, str("donations"), str("amount"), str("average"), {}, &display).isValid());
        QCOMPARE(display, str("$39.62"));
        QVERIFY(Records::total(*db.file, str("donations"), str("amount"), str("count"), {{str("amount"), str("gt"), str("20")}},
                               &display)
                    .isValid());
        QCOMPARE(display, str("2"));
        QVERIFY(Records::total(*db.file, str("donations"), str("given"), str("max"), {}, &display).isValid());
        QCOMPARE(display, str("2026-03-01"));
        QString error;
        QVERIFY(!Records::total(*db.file, str("donors"), str("name"), str("sum"), {}, &display, &error).isValid());
        QVERIFY(error.contains(str("not a number")));
        const auto choices = Records::lookupChoices(*db.file, {str("donors"), str("id"), str("name")});
        QCOMPARE(choices.size(), 3);
        QCOMPARE(choices.first().second, str("Ada Lovelace")); // sorted by name
    }

    void fileFieldsPersistUndoAndStayOutOfReads()
    {
        Db db;
        QVERIFY(db.create(table("members", {field("id", FieldType::AutoNumber), field("name", FieldType::Text),
                                            field("photo", FieldType::File)}))
                    .ok);
        // A file field can be no key, index, default or required field.
        QVERIFY(!planCreateTable(*db.file, table("bad", {field("id", FieldType::AutoNumber),
                                                          field("doc", FieldType::File, true)}))
                     .ok());
        QCOMPARE(db.scalar(str("SELECT type FROM pragma_table_info('members') WHERE name='photo'")).toString(), str("FILE"));
        QCOMPARE(readTable(*db.file, str("members"))->fields.at(2).type, FieldType::File);
        QVERIFY(db.exec(str("INSERT INTO members(name) VALUES('Ada')")).ok);
        const QByteArray png = QByteArray::fromHex("89504E470D0A1A0A0000000D49484452") + QByteArray(100000, 'p');
        Change change;
        QVERIFY(Records::setFile(*db.file, str("members"), 1, str("photo"), png, &change).ok);
        QCOMPARE(db.scalar(str("SELECT typeof(photo) FROM members")).toString(), str("blob"));
        QString error;
        QCOMPARE(Records::readFile(*db.file, str("members"), 1, str("photo"), &error).value(), png);
        // Reads summarize (never the bytes); the datasheet shows kind and size.
        const RowSet rows = Records::read(*db.file, str("members"));
        const QString summary = rows.values.first().at(2).toString();
        QVERIFY2(summary.startsWith(str("100016:89504e47")), qPrintable(summary));
        QCOMPARE(displayText(FieldType::File, rows.values.first().at(2)), str("PNG image · 98 KB"));
        QVERIFY(rows.columns.at(2).readOnly);
        QCOMPARE(Records::readRow(*db.file, str("members"), 1, rows.columns)->at(2).toString(), summary);
        // Typing into it is refused; a filter only asks "has a file?".
        QVERIFY(!Records::updateCells(*db.file, str("members"), {{1, str("photo"), str("x")}}, nullptr).ok);
        RowQuery having;
        having.filters = {{str("photo"), str("isNotNull"), QString()}};
        having.sort = {{str("photo"), true}};
        QCOMPARE(Records::read(*db.file, str("members"), having).rowids.size(), 1);
        having.filters = {{str("photo"), str("contains"), str("x")}};
        QVERIFY(!Records::read(*db.file, str("members"), having).error.isEmpty());
        // Undo empties the cell again.
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(db.scalar(str("SELECT photo IS NULL FROM members")).toInt(), 1);
        // Over the limit, not a file field, no such record: refused.
        QVERIFY(!Records::setFile(*db.file, str("members"), 1, str("photo"), QByteArray(kFileFieldLimit + 1, 'x'), nullptr).ok);
        QVERIFY(!Records::setFile(*db.file, str("members"), 1, str("name"), png, nullptr).ok);
        QVERIFY(!Records::setFile(*db.file, str("members"), 99, str("photo"), png, nullptr).ok);
        // A null array clears it; a rebuild keeps the bytes.
        QVERIFY(Records::setFile(*db.file, str("members"), 1, str("photo"), png, nullptr).ok);
        const TableDef before = readTable(*db.file, str("members")).value();
        TableDef after = before;
        after.fields.move(2, 1); // a reorder rebuilds the table
        QVERIFY(db.apply(planAlterTable(*db.file, before, after)).ok);
        QCOMPARE(Records::readFile(*db.file, str("members"), 1, str("photo"), &error).value(), png);
        // It never converts to or from another type.
        TableDef retyped = readTable(*db.file, str("members")).value();
        retyped.fields[1].type = FieldType::LongText;
        QVERIFY(!planAlterTable(*db.file, readTable(*db.file, str("members")).value(), retyped).ok());
        QVERIFY(Records::setFile(*db.file, str("members"), 1, str("photo"), QByteArray(), nullptr).ok);
        QCOMPARE(db.scalar(str("SELECT photo IS NULL FROM members")).toInt(), 1);
        // Forms show files beside the designer: the mirror leaves them out.
        QVERIFY(writeFormMirror(*db.file, db.path("mirror.sqlite"), str("members")).ok);
        Connection mirror;
        QVERIFY(mirror.open(db.path("mirror.sqlite"), false, true, nullptr));
        QCOMPARE(mirror.scalar(str("SELECT count(*) FROM pragma_table_info('members')")).toInt(), 2);
    }
};

BASE_TEST_MAIN(BaseRecordsTest)

#include "tst_base_records.moc"
