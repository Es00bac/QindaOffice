// SPDX-License-Identifier: GPL-3.0-or-later
// M86's field kinds (D-094) against hand-worked results: calculated fields
// as generated columns (money exact, read-only, kept through rebuilds and
// undo, Date() refused, a foreign file's own SQL kept), value lists with
// Limit To List enforced for every writer, Allow Zero Length, Hyperlink and
// Large Number fields.

#include "base_fixture.h"

#include "base_hyperlink.h"
#include "base_records.h"
#include "base_rules.h"

using namespace BaseTest;

namespace {

FieldDef calculated(const char *name, const char *expression, FieldType result = FieldType::Other)
{
    FieldDef f;
    f.name = str(name);
    f.calculated = true;
    f.expression = str(expression);
    f.type = result;
    return f;
}

TableDef reread(Db &db, const char *name)
{
    return readTable(*db.file, str(name)).value_or(TableDef());
}

} // namespace

class BaseFieldsM86Test final : public QObject {
    Q_OBJECT
private slots:
    void calculatedFieldsAreGeneratedColumns()
    {
        Db db;
        TableDef lines = table("lines", {field("id", FieldType::AutoNumber), field("item", FieldType::Text),
                                         field("qty", FieldType::Integer), field("price", FieldType::Currency),
                                         calculated("total", "[qty] * [price]"),
                                         calculated("label", "[item] & \" x\" & [qty]"),
                                         calculated("discounted", "[price] * 0.9", FieldType::Currency),
                                         calculated("units", "[price] * [qty]", FieldType::Number)});
        Change created;
        QVERIFY2(db.create(lines, &created).ok, qPrintable(planCreateTable(*db.file, lines).error));
        QVERIFY(db.exec(str("INSERT INTO lines(item, qty, price) VALUES('Pen', 3, 1999), ('Ink', NULL, 450)")).ok);
        // 3 × $19.99 = $59.97 (5997 minor units); $19.99 × 0.9 = 1799.1 → 1799.
        QCOMPARE(db.scalar(str("SELECT total FROM lines WHERE id = 1")).toLongLong(), qint64(5997));
        QCOMPARE(db.scalar(str("SELECT label FROM lines WHERE id = 1")).toString(), str("Pen x3"));
        QCOMPARE(db.scalar(str("SELECT discounted FROM lines WHERE id = 1")).toLongLong(), qint64(1799));
        QCOMPARE(db.scalar(str("SELECT units FROM lines WHERE id = 1")).toDouble(), 59.97);
        QVERIFY(db.scalar(str("SELECT total FROM lines WHERE id = 2")).isNull()); // Null qty: Null total
        const TableDef read = reread(db, "lines");
        QVERIFY(read.field(str("total"))->calculated);
        QCOMPARE(read.field(str("total"))->type, FieldType::Currency); // worked out from the expression
        QCOMPARE(read.field(str("total"))->expression, str("[qty] * [price]"));
        QCOMPARE(read.field(str("label"))->type, FieldType::Text);
        // Read-only everywhere QindaBase writes.
        const RowSet set = Records::read(*db.file, str("lines"));
        QCOMPARE(set.values.first().at(set.columnIndex(str("total"))).toLongLong(), qint64(5997));
        QVERIFY(set.columns.at(set.columnIndex(str("total"))).readOnly);
        Change change;
        const Result edited = Records::updateCells(*db.file, str("lines"), {{1, str("total"), str("1.00")}}, &change);
        QVERIFY(!edited.ok);
        QVERIFY2(edited.error.contains(str("calculated")), qPrintable(edited.error));
        QVERIFY(Records::updateCells(*db.file, str("lines"), {{1, str("qty"), str("4")}}, &change).ok);
        QCOMPARE(db.scalar(str("SELECT total FROM lines WHERE id = 1")).toLongLong(), qint64(7996));
        QVERIFY(db.file->applyChange(change, true).ok); // undo the edit: the total follows
        QCOMPARE(db.scalar(str("SELECT total FROM lines WHERE id = 1")).toLongLong(), qint64(5997));
        // A rebuild (a field moved to the middle) keeps the calculation.
        TableDef after = read;
        FieldDef sku = field("sku", FieldType::Text);
        after.fields.insert(1, sku);
        Change rebuilt;
        QVERIFY2(db.apply(planAlterTable(*db.file, read, after), &rebuilt).ok,
                 qPrintable(planAlterTable(*db.file, read, after).error));
        QCOMPARE(db.scalar(str("SELECT total FROM lines WHERE id = 1")).toLongLong(), qint64(5997));
        QVERIFY(reread(db, "lines").field(str("total"))->calculated);
        // Changing the expression recalculates every record.
        TableDef recalc = reread(db, "lines");
        recalc.fields[recalc.fieldIndex(str("total"))].expression = str("[qty] * [price] + 100");
        QVERIFY(db.apply(planAlterTable(*db.file, reread(db, "lines"), recalc)).ok);
        // A literal meets money as money: + 100 is $100.00 (10000 minor units).
        QCOMPARE(db.scalar(str("SELECT total FROM lines WHERE id = 1")).toLongLong(), qint64(15997));
        QVERIFY(db.file->applyChange(rebuilt, true).ok);
        QVERIFY(db.file->applyChange(created, true).ok);
        QVERIFY(!db.file->tableExists(str("lines")));
    }

    void calculatedFieldsRefuseWhatTheyCannotBe()
    {
        Db db;
        const auto problem = [&](FieldDef f) {
            TableDef t = table("t", {field("id", FieldType::AutoNumber), field("given", FieldType::Date),
                                     field("qty", FieldType::Integer)});
            t.fields.append(f);
            return planCreateTable(*db.file, t).error;
        };
        QVERIFY(problem(calculated("age", "Date() - [given]")).contains(str("Date()")));
        QVERIFY(problem(calculated("sum", "Sum([qty])")).contains(str("total")));
        QVERIFY(problem(calculated("self", "[self] + 1")).contains(str("itself")));
        QVERIFY(problem(calculated("ask", "[How many?] * 2")).contains(str("How many?")));
        FieldDef required = calculated("twice", "[qty] * 2");
        required.required = true;
        QVERIFY(problem(required).contains(str("calculated field")));
        QVERIFY(problem(calculated("empty", "")).contains(str("expression")));
        QVERIFY(problem(calculated("next", "[given] + 30")).isEmpty()); // a date 30 days on
    }

    void aForeignGeneratedColumnKeepsItsSql()
    {
        Db db;
        QVERIFY(db.exec(str("CREATE TABLE f(a INTEGER, b INTEGER GENERATED ALWAYS AS (a * 2) STORED, c TEXT)")).ok);
        QVERIFY(db.exec(str("INSERT INTO f(a, c) VALUES(21, 'x')")).ok);
        const TableDef before = reread(db, "f");
        QVERIFY(before.field(str("b"))->calculated);
        QVERIFY(before.field(str("b"))->expression.isEmpty());
        QCOMPARE(before.field(str("b"))->generatedSql, str("a * 2"));
        TableDef after = before;
        after.fields.move(2, 0); // c first: a rebuild
        QVERIFY2(db.apply(planAlterTable(*db.file, before, after)).ok, qPrintable(planAlterTable(*db.file, before, after).error));
        QCOMPARE(db.scalar(str("SELECT b FROM f")).toLongLong(), qint64(42));
        QCOMPARE(reread(db, "f").field(str("b"))->generatedSql, str("a * 2"));
    }

    void valueListsLimitWhatAFieldTakes()
    {
        Db db;
        TableDef gifts = table("gifts", {field("id", FieldType::AutoNumber), field("method", FieldType::Text),
                                         field("qty", FieldType::Integer)});
        gifts.fields[1].lookup.values = {str("Check"), str("Card"), str("Cash")};
        gifts.fields[1].lookup.limitToList = true;
        gifts.fields[2].lookup.values = {str("1"), str("2"), str("5")}; // a list, not limited
        QVERIFY2(db.create(gifts).ok, qPrintable(planCreateTable(*db.file, gifts).error));
        // Every writer meets the list: another SQLite tool too.
        const Result wire = db.exec(str("INSERT INTO gifts(method) VALUES('Wire')"));
        QVERIFY(!wire.ok);
        QVERIFY2(wire.error.contains(str("must be one of its list: Check, Card, Cash")), qPrintable(wire.error));
        QVERIFY(db.exec(str("INSERT INTO gifts(method, qty) VALUES(NULL, 7)")).ok); // Null passes; qty is not limited
        // Typed text takes the list's own spelling.
        qint64 rowid = 0;
        Change change;
        QVERIFY(Records::insertRecord(*db.file, str("gifts"), {{str("method"), str("card")}}, &rowid, &change).ok);
        QCOMPARE(db.scalar(str("SELECT method FROM gifts WHERE rowid = ?"), {rowid}).toString(), str("Card"));
        const Result typed = Records::insertRecord(*db.file, str("gifts"), {{str("method"), str("Wire")}}, &rowid, &change);
        QVERIFY(!typed.ok);
        QVERIFY(typed.error.contains(str("Check, Card, Cash")));
        // The choices, in the list's order, stored as the field stores them.
        const LookupDef qty = reread(db, "gifts").field(str("qty"))->lookup;
        const auto choices = Records::lookupChoices(*db.file, qty, 100, FieldType::Integer);
        QCOMPARE(choices.size(), 3);
        QCOMPARE(choices.at(2).first.toLongLong(), qint64(5));
        QCOMPARE(choices.at(2).second, str("5"));
        // Limiting qty now is refused while a record holds 7.
        TableDef before = reread(db, "gifts");
        TableDef after = before;
        after.fields[2].lookup.limitToList = true;
        const PlanPreview preview = db.file->previewPlan(planAlterTable(*db.file, before, after));
        QVERIFY(!preview.ok);
        QVERIFY2(preview.error.contains(str("1 record(s) hold a value of qty")), qPrintable(preview.error));
        // A list on a Yes/No field, or with a value the field cannot hold, is refused.
        after = before;
        after.fields[2].lookup.values.append(str("many"));
        QVERIFY(planAlterTable(*db.file, before, after).error.contains(str("\"many\"")));
    }

    void allowZeroLengthDecidesWhatEmptyTextMeans()
    {
        Db db;
        TableDef notes = table("notes", {field("id", FieldType::AutoNumber), field("strict", FieldType::Text),
                                         field("loose", FieldType::Text)});
        notes.fields[1].allowZeroLength = false;
        QVERIFY(db.create(notes).ok);
        const Result empty = db.exec(str("INSERT INTO notes(strict) VALUES('')"));
        QVERIFY(!empty.ok);
        QVERIFY2(empty.error.contains(str("Allow Zero Length is No")), qPrintable(empty.error));
        QVERIFY(db.exec(str("INSERT INTO notes(strict) VALUES(NULL)")).ok);
        // Typing "" stores an empty text where it is allowed, never where not.
        qint64 rowid = 0;
        Change change;
        QVERIFY(Records::insertRecord(*db.file, str("notes"), {{str("loose"), str("\"\"")}}, &rowid, &change).ok);
        QCOMPARE(db.scalar(str("SELECT typeof(loose) || ':' || length(loose) FROM notes WHERE rowid = ?"), {rowid}).toString(),
                 str("text:0"));
        QVERIFY(!Records::insertRecord(*db.file, str("notes"), {{str("strict"), str("\"\"")}}, &rowid, &change).ok);
        // Allowing no zero length on `loose` now is refused while one record holds ''.
        TableDef before = reread(db, "notes");
        QVERIFY(before.field(str("loose"))->allowZeroLength);
        QVERIFY(!before.field(str("strict"))->allowZeroLength);
        TableDef after = before;
        after.fields[2].allowZeroLength = false;
        const PlanPreview preview = db.file->previewPlan(planAlterTable(*db.file, before, after));
        QVERIFY(!preview.ok);
        QVERIFY2(preview.error.contains(str("1 record(s) hold an empty text in loose")), qPrintable(preview.error));
    }

    void hyperlinksAreAccessText()
    {
        const Hyperlink full = parseHyperlink(str("Docs#https://example.org/a#top#Read me"));
        QCOMPARE(full.display, str("Docs"));
        QCOMPARE(full.address, str("https://example.org/a"));
        QCOMPARE(full.subAddress, str("top"));
        QCOMPARE(full.screenTip, str("Read me"));
        QCOMPARE(full.target(), str("https://example.org/a#top"));
        QCOMPARE(full.text(), str("Docs#https://example.org/a#top#Read me"));
        const Hyperlink bare = parseHyperlink(str("https://example.org/#top")); // one '#': a bare address
        QCOMPARE(bare.address, str("https://example.org/#top"));
        QCOMPARE(bare.shown(), str("https://example.org/#top"));
        QCOMPARE(bare.accessText(), str("#https://example.org/#top")); // Access's subaddress
        QCOMPARE(parseHyperlink(bare.accessText()).target(), str("https://example.org/#top"));
        QCOMPARE(parseHyperlink(str("Mail us#mailto:ada@example.org#")).shown(), str("Mail us"));
        Db db;
        QVERIFY(db.create(table("links", {field("id", FieldType::AutoNumber), field("site", FieldType::Hyperlink),
                                          field("big", FieldType::LargeNumber)}))
                    .ok);
        const TableDef read = reread(db, "links");
        QCOMPARE(read.field(str("site"))->type, FieldType::Hyperlink);
        QCOMPARE(read.field(str("big"))->type, FieldType::LargeNumber);
        QCOMPARE(read.field(str("site"))->declared.toUpper(), str("HYPERLINK TEXT"));
        QCOMPARE(typeFromDeclared(str("BIGINT")), FieldType::LargeNumber);
        qint64 rowid = 0;
        Change change;
        QVERIFY(Records::insertRecord(*db.file, str("links"),
                                      {{str("site"), str("Docs#https://example.org#")},
                                       {str("big"), str("9,007,199,254,740,993")}},
                                      &rowid, &change)
                    .ok);
        const RowSet set = Records::read(*db.file, str("links"));
        const ColumnInfo &site = set.columns.at(set.columnIndex(str("site")));
        QCOMPARE(site.display(set.values.first().at(1), db.file->currency()), str("Docs"));
        QCOMPARE(editText(FieldType::Hyperlink, set.values.first().at(1)), str("Docs#https://example.org#"));
        QCOMPARE(set.values.first().at(2).toLongLong(), Q_INT64_C(9007199254740993)); // 2^53 + 1, exact
        QVERIFY(isNumeric(FieldType::LargeNumber));
        QVERIFY(!Records::insertRecord(*db.file, str("links"), {{str("big"), str("1.5")}}, &rowid, &change).ok);
    }
};

BASE_TEST_MAIN(BaseFieldsM86Test)

#include "tst_base_fields_m86.moc"
