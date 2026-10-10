// SPDX-License-Identifier: GPL-3.0-or-later
// Validation rules and input masks (M80, D-089 §1–2): field and record
// rules enforced by the database for every writer, refused while records
// break them, kept through table rebuilds and renames and undone with the
// design; Access's input masks shaping typed text and showing it back.

#include "base_fixture.h"

#include "base_input_mask.h"
#include "base_records.h"
#include "base_rules.h"

#include <QDate>

using namespace BaseTest;

namespace {

TableDef gifts()
{
    TableDef t = table("gifts", {field("id", FieldType::AutoNumber), field("amount", FieldType::Currency),
                                 field("given", FieldType::Date), field("rating", FieldType::Integer)});
    t.fields[1].validationRule = str("> 0");
    t.fields[1].validationText = str("A gift is more than nothing.");
    t.fields[2].validationRule = str("<= Date()");
    t.fields[3].validationRule = str("Between 1 And 5");
    return t;
}

TableDef redesign(const TableDef &def)
{
    TableDef after = def;
    after.originalName = def.name;
    for (FieldDef &f : after.fields) {
        f.originalName = f.name;
    }
    return after;
}

Result insert(Db &db, const char *table, const QVariantMap &values, qint64 *rowid = nullptr)
{
    qint64 id = 0;
    Change change;
    return Records::insertRecord(*db.file, str(table), values, rowid != nullptr ? rowid : &id, &change);
}

} // namespace

class BaseRulesTest final : public QObject {
    Q_OBJECT
private slots:
    void fieldRulesBindEveryWriter()
    {
        Db db;
        QVERIFY(db.create(gifts()).ok);
        const std::optional<TableDef> def = readTable(*db.file, str("gifts"));
        QVERIFY(def);
        QCOMPARE(def->fields.at(1).validationRule, str("> 0"));
        QCOMPARE(def->fields.at(1).validationText, str("A gift is more than nothing."));
        QCOMPARE(def->derivedTriggers.size(), 6); // insert + update for three rules
        QVERIFY(def->triggerSql.isEmpty());   // derived from the design, never kept verbatim
        Result r = insert(db, "gifts", {{str("amount"), str("0")}});
        QVERIFY(!r.ok);
        QCOMPARE(r.error, str("A gift is more than nothing."));
        qint64 rowid = 0;
        QVERIFY(insert(db, "gifts", {{str("amount"), str("12.50")}, {str("rating"), str("3")}}, &rowid).ok);
        // Null passes, as in Access: Required stops an empty field, not a rule.
        QVERIFY(insert(db, "gifts", {{str("rating"), str("2")}}).ok);
        Change change;
        r = Records::updateCells(*db.file, str("gifts"), {{rowid, str("amount"), str("-1")}}, &change);
        QVERIFY(!r.ok);
        QCOMPARE(r.error, str("A gift is more than nothing."));
        // Any other SQLite writer meets the same rule.
        QVERIFY(!db.exec(str("INSERT INTO gifts(amount) VALUES(-5)")).ok);
        // "<= Date()" uses today; without validation text the rule is named.
        const QString later = QDate::currentDate().addDays(10).toString(Qt::ISODate);
        r = insert(db, "gifts", {{str("amount"), str("1")}, {str("given"), later}});
        QVERIFY(!r.ok);
        QCOMPARE(r.error, str("given must meet its validation rule: <= Date()"));
        r = insert(db, "gifts", {{str("amount"), str("1")}, {str("rating"), str("9")}});
        QVERIFY(r.error.contains(str("Between 1 And 5")));
        // A currency rule converts its literal: "> 0" on money is 0 minor units.
        QVERIFY(insert(db, "gifts", {{str("amount"), str("0.01")}}).ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM gifts")).toInt(), 3);
    }

    void recordRuleComparesFields()
    {
        Db db;
        TableDef bookings = table("bookings", {field("id", FieldType::AutoNumber), field("starts", FieldType::Date),
                                               field("ends", FieldType::Date)});
        bookings.validationRule = str("[ends] >= [starts]");
        bookings.validationText = str("A booking ends on or after its start.");
        QVERIFY(db.create(bookings).ok);
        QCOMPARE(readTable(*db.file, str("bookings"))->validationRule, str("[ends] >= [starts]"));
        Result r = insert(db, "bookings", {{str("starts"), str("2026-03-10")}, {str("ends"), str("3/9/2026")}});
        QVERIFY(!r.ok);
        QCOMPARE(r.error, str("A booking ends on or after its start."));
        qint64 rowid = 0;
        QVERIFY(insert(db, "bookings", {{str("starts"), str("2026-03-10")}, {str("ends"), str("2026-03-12")}}, &rowid).ok);
        Change change;
        QVERIFY(!Records::updateCells(*db.file, str("bookings"), {{rowid, str("ends"), str("2026-03-01")}}, &change).ok);
        // A field's rule tests only its own value; comparing fields is the record rule.
        TableDef bad = table("bad", {field("id", FieldType::AutoNumber), field("starts", FieldType::Date),
                                     field("ends", FieldType::Date)});
        bad.fields[2].validationRule = str(">= [starts]");
        const SchemaPlan plan = planCreateTable(*db.file, bad);
        QVERIFY(!plan.ok());
        QVERIFY2(plan.error.contains(str("tests only its own value")), qPrintable(plan.error));
        bad.fields[2].validationRule = str("> [Ask me]");
        QVERIFY(!planCreateTable(*db.file, bad).ok()); // a rule asks for nothing
        bad.fields[2].validationRule = str("> Sum([starts])");
        QVERIFY(!planCreateTable(*db.file, bad).ok()); // nor totals
    }

    void newRulesWaitForTheRecords()
    {
        Db db;
        QVERIFY(db.create(table("gifts", {field("id", FieldType::AutoNumber), field("amount", FieldType::Currency)})).ok);
        QVERIFY(db.exec(str("INSERT INTO gifts(amount) VALUES(-100), (500), (-1)")).ok);
        TableDef after = redesign(*readTable(*db.file, str("gifts")));
        after.fields[1].validationRule = str("> 0");
        SchemaPlan plan = planAlterTable(*db.file, *readTable(*db.file, str("gifts")), after);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.notes.join(QLatin1Char(' ')).contains(str("validation rule (> 0)")));
        const PlanPreview preview = db.file->previewPlan(plan);
        QVERIFY(!preview.ok);
        QVERIFY2(preview.error.contains(str("2 record(s) break the validation rule of amount (> 0)")), qPrintable(preview.error));
        QVERIFY(!db.apply(plan).ok);
        QVERIFY(readTable(*db.file, str("gifts"))->derivedTriggers.isEmpty()); // nothing changed
        QVERIFY(db.exec(str("UPDATE gifts SET amount = 100 WHERE amount < 0")).ok);
        plan = planAlterTable(*db.file, *readTable(*db.file, str("gifts")), after);
        QVERIFY(db.apply(plan).ok);
        QVERIFY(!db.exec(str("UPDATE gifts SET amount = 0")).ok);
    }

    void rulesSurviveRebuildRenameAndUndo()
    {
        Db db;
        QVERIFY(db.create(gifts()).ok);
        QVERIFY(insert(db, "gifts", {{str("amount"), str("5")}, {str("rating"), str("4")}}).ok);
        // A type change elsewhere rebuilds the table, which drops its triggers:
        // the plan makes them again from the design.
        TableDef after = redesign(*readTable(*db.file, str("gifts")));
        after.fields[3].type = FieldType::Number;
        SchemaPlan plan = planAlterTable(*db.file, *readTable(*db.file, str("gifts")), after);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QVERIFY(plan.sqlText().contains(str("CREATE TRIGGER")));
        QVERIFY(db.apply(plan).ok);
        QCOMPARE(readTable(*db.file, str("gifts"))->derivedTriggers.size(), 6);
        QVERIFY(!insert(db, "gifts", {{str("amount"), str("-2")}}).ok);
        // A renamed table keeps its rules (under triggers named for it).
        after = redesign(*readTable(*db.file, str("gifts")));
        after.name = str("presents");
        QVERIFY(db.apply(planAlterTable(*db.file, *readTable(*db.file, str("gifts")), after)).ok);
        const std::optional<TableDef> renamed = readTable(*db.file, str("presents"));
        QCOMPARE(renamed->fields.at(1).validationRule, str("> 0"));
        QVERIFY(renamed->derivedTriggers.first().contains(str("presents")));
        QVERIFY(!insert(db, "presents", {{str("amount"), str("-2")}}).ok);
        // Removing a rule is one undoable design step.
        after = redesign(*renamed);
        after.fields[3].validationRule.clear();
        Change change;
        QVERIFY(db.apply(planAlterTable(*db.file, *renamed, after), &change).ok);
        QVERIFY(change.undoable());
        QVERIFY(insert(db, "presents", {{str("amount"), str("1")}, {str("rating"), str("9")}}).ok);
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(readTable(*db.file, str("presents"))->fields.at(3).validationRule, str("Between 1 And 5"));
        QVERIFY(!insert(db, "presents", {{str("amount"), str("1")}, {str("rating"), str("9")}}).ok);
    }

    void olderFilesGainTheColumns()
    {
        Db db;
        QVERIFY(db.create(table("gifts", {field("id", FieldType::AutoNumber), field("amount", FieldType::Currency)})).ok);
        // An M65 file: qb_fields without the M80 columns.
        for (const char *column : {"validation_rule", "validation_text", "input_mask"}) {
            QVERIFY(db.exec(str("ALTER TABLE qb_fields DROP COLUMN %1").arg(str(column))).ok);
        }
        const std::optional<TableDef> before = readTable(*db.file, str("gifts"));
        QVERIFY(before && before->fields.at(1).validationRule.isEmpty());
        TableDef after = redesign(*before);
        after.fields[1].validationRule = str(">= 1");
        const SchemaPlan plan = planAlterTable(*db.file, *before, after);
        QVERIFY(plan.statements.contains(str("ALTER TABLE qb_fields ADD COLUMN validation_rule TEXT")));
        QVERIFY(db.apply(plan).ok);
        QCOMPARE(readTable(*db.file, str("gifts"))->fields.at(1).validationRule, str(">= 1"));
    }

    void masksParseLikeAccess()
    {
        QString error;
        QVERIFY(!InputMask::parse(str("\"abc"), &error));
        QVERIFY(error.contains(str("not closed")));
        QVERIFY(!InputMask::parse(str("(999\\"), &error));
        QVERIFY(!InputMask::parse(str("---"), &error));
        QVERIFY(error.contains(str("at least one place")));
        const std::optional<InputMask> phone = InputMask::parse(str("!(999) 000-0000;;_"));
        QVERIFY(phone);
        QCOMPARE(phone->slotCount(), 10);
        QVERIFY(phone->rightFill);
        QVERIFY(!phone->storeLiterals);
        QCOMPARE(maskHint(*phone), str("(___) ___-____"));
        const std::optional<InputMask> date = InputMask::parse(str("99/99/0000;0;#"));
        QVERIFY(date->storeLiterals);
        QCOMPARE(maskHint(*date), str("##/##/####"));
        QVERIFY(InputMask::parse(str("Password"))->password);
        QCOMPARE(inputMaskPresets().size(), 7);
    }

    void masksShapeTypedText()
    {
        const InputMask phone = *InputMask::parse(str("(999) 000-0000"));
        for (const char *typed : {"3035551234", "(303) 555-1234", "303 555 1234", "303.555.1234"}) {
            const MaskResult r = applyInputMask(phone, str(typed));
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(r.text, str("3035551234")); // the typed characters only (";1", the default)
        }
        QVERIFY(applyInputMask(phone, str("555-1234")).error.contains(str("too short")));
        QVERIFY(applyInputMask(phone, str("30355512345")).error.contains(str("too long")));
        QVERIFY(applyInputMask(phone, str("303555123x")).error.contains(str("a digit is expected")));
        QCOMPARE(maskedDisplay(phone, str("3035551234")), str("(303) 555-1234"));
        QCOMPARE(maskedDisplay(phone, str("not a phone")), str("not a phone"));
        // "!" fills from the right: a number without its area code.
        const InputMask right = *InputMask::parse(str("!(999) 000-0000"));
        QCOMPARE(applyInputMask(right, str("555-1234")).text, str("5551234"));
        QCOMPARE(maskedDisplay(right, str("5551234")), str("(   ) 555-1234"));
        const InputMask zip = *InputMask::parse(str("00000-9999"));
        QCOMPARE(applyInputMask(zip, str("97201")).text, str("97201"));
        QCOMPARE(applyInputMask(zip, str("97201-1234")).text, str("972011234"));
        QCOMPARE(maskedDisplay(zip, str("972011234")), str("97201-1234"));
        QCOMPARE(maskedDisplay(zip, str("97201")), str("97201"));
        QCOMPARE(applyInputMask(*InputMask::parse(str(">L<??????")), str("gRACE")).text, str("Grace"));
        QCOMPARE(applyInputMask(*InputMask::parse(str(">L0L 0L0")), str("k1a 0b1")).text, str("K1A0B1"));
        const InputMask date = *InputMask::parse(str("99/99/0000;0;_"));
        QCOMPARE(applyInputMask(date, str("3/14/2026")).text, str("3/14/2026"));
        const InputMask password = *InputMask::parse(str("Password"));
        QCOMPARE(applyInputMask(password, str(" s3cret ")).text, str(" s3cret "));
        QCOMPARE(maskedDisplay(password, str("s3cret")), str("******"));
    }

    void masksApplyWherePeopleType()
    {
        Db db;
        TableDef people = table("people", {field("id", FieldType::AutoNumber), field("phone", FieldType::Text),
                                           field("zip", FieldType::Text), field("joined", FieldType::Date)});
        people.fields[1].inputMask = str("!(999) 000-0000;0;_"); // keeps the literals
        people.fields[2].inputMask = str("00000-9999");
        people.fields[3].inputMask = str("99/99/0000;0;_");
        QVERIFY(db.create(people).ok);
        qint64 rowid = 0;
        QVERIFY(insert(db, "people", {{str("phone"), str("503 555 0142")}, {str("zip"), str("97201-1234")},
                                      {str("joined"), str("3/14/2026")}}, &rowid).ok);
        const QVariantList stored = db.file->db().rows(str("SELECT phone, zip, joined FROM people")).first();
        QCOMPARE(stored.at(0).toString(), str("(503) 555-0142"));
        QCOMPARE(stored.at(1).toString(), str("972011234"));
        QCOMPARE(stored.at(2).toString(), str("2026-03-14"));
        const Result r = insert(db, "people", {{str("phone"), str("555-01")}});
        QVERIFY(!r.ok);
        QCOMPARE(r.error, str("phone is too short; it must look like (___) ___-____."));
        const QList<ColumnInfo> columns = Records::columns(*db.file, str("people"));
        QCOMPARE(columns.at(2).display(str("972011234"), {}), str("97201-1234"));
        QCOMPARE(columns.at(1).toMap().value(str("maskHint")).toString(), str("(___) ___-____"));
        // Masks shape typing, so fields that take none have none.
        TableDef bad = table("bad", {field("id", FieldType::AutoNumber), field("ok", FieldType::YesNo)});
        bad.fields[1].inputMask = str("0");
        QVERIFY(planCreateTable(*db.file, bad).error.contains(str("cannot have an input mask")));
        bad.fields[1].type = FieldType::Text;
        bad.fields[1].inputMask = str("\"open");
        QVERIFY(planCreateTable(*db.file, bad).error.contains(str("input mask of ok")));
    }
};

BASE_TEST_MAIN(BaseRulesTest)

#include "tst_base_rules.moc"
