// SPDX-License-Identifier: GPL-3.0-or-later
// M86's data macros (D-094) against hand-worked results: Before Change
// (RaiseError, SetField with IsInsert and [Old]), Before Delete, After
// Insert and After Delete (LogEvent), StopMacro; every writer meets them;
// an undo and a redo replay what the macros did without running them again;
// the checks refuse window actions and impossible steps; a removed macro
// takes its triggers along; a renamed table keeps its macros.

#include "base_fixture.h"

#include "base_data_macro.h"
#include "base_records.h"

using namespace BaseTest;

namespace {

MacroStep step(const char *action, QVariantMap arguments)
{
    return {str(action), std::move(arguments)};
}

Macro macro(QList<MacroStep> steps)
{
    Macro m;
    m.steps = std::move(steps);
    return m;
}

struct Gifts : Db {
    Gifts()
    {
        create(table("donations", {field("id", FieldType::AutoNumber), field("donor", FieldType::Text),
                                   field("amount", FieldType::Currency), field("status", FieldType::Text),
                                   field("updated", FieldType::Integer), field("receipt", FieldType::YesNo)}));
    }
    Result set(const char *event, const Macro &m)
    {
        const SchemaPlan plan = planDataMacro(*file, str("donations"), str(event), m);
        return plan.ok() ? apply(plan) : Result::failure(plan.error);
    }
    [[nodiscard]] QStringList log() const
    {
        QStringList out;
        for (const QVariantList &row : file->db().rows(str("SELECT event, description FROM qb_application_log ORDER BY id"))) {
            out.append(row.value(0).toString() + str(": ") + row.value(1).toString());
        }
        return out;
    }
    [[nodiscard]] QStringList triggers() const
    {
        QStringList out;
        for (const QVariantList &row : file->db().rows(str("SELECT name FROM sqlite_schema WHERE type = 'trigger' AND "
                                                            "name LIKE 'qbm:%' ORDER BY name"))) {
            out.append(row.value(0).toString());
        }
        return out;
    }
};

} // namespace

class BaseDataMacroTest final : public QObject {
    Q_OBJECT
private slots:
    void tableEventsRunForEveryWriter()
    {
        Gifts db;
        QVERIFY2(db.set("beforeChange",
                        macro({step("raiseError", {{str("description"), str("Amounts are positive.")},
                                                   {str("condition"), str("[amount] <= 0")}}),
                               step("setField", {{str("field"), str("status")}, {str("value"), str("\"New\"")},
                                                 {str("condition"), str("IsInsert")}}),
                               step("setField", {{str("field"), str("updated")}, {str("value"), str("Nz([updated], 0) + 1")},
                                                 {str("condition"), str("Not IsInsert")}})}))
                     .ok,
                 "beforeChange");
        QVERIFY(db.set("afterInsert", macro({step("stopMacro", {{str("condition"), str("[donor] = \"Anonymous\"")}}),
                                             step("logEvent", {{str("description"), str("=\"New gift from \" & [donor]")}})}))
                    .ok);
        QVERIFY(db.set("beforeDelete", macro({step("raiseError", {{str("description"), str("Receipted gifts stay.")},
                                                                  {str("condition"), str("[receipt]")}})}))
                    .ok);
        QVERIFY(db.set("afterDelete", macro({step("logEvent", {{str("description"), str("Gift deleted")}})})).ok);
        QCOMPARE(db.triggers(), (QStringList{str("qbm:donations:ad"), str("qbm:donations:ai"), str("qbm:donations:au"),
                                             str("qbm:donations:bd"), str("qbm:donations:bi"), str("qbm:donations:bu")}));
        // Through QindaBase: one undo step holding what the macros did.
        qint64 rowid = 0;
        Change inserted;
        QVERIFY(Records::insertRecord(*db.file, str("donations"), {{str("donor"), str("Ada")}, {str("amount"), str("25")}},
                                      &rowid, &inserted)
                    .ok);
        QCOMPARE(db.scalar(str("SELECT status FROM donations WHERE rowid = ?"), {rowid}).toString(), str("New"));
        QVERIFY(db.scalar(str("SELECT updated FROM donations WHERE rowid = ?"), {rowid}).isNull());
        QCOMPARE(db.log(), (QStringList{str("After Insert: New gift from Ada")}));
        // Undo and redo replay the macros' work without running them again:
        // the record and its log line go and come back, once.
        QVERIFY(db.file->applyChange(inserted, true).ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donations")).toLongLong(), qint64(0));
        QVERIFY(db.log().isEmpty());
        QVERIFY(db.file->applyChange(inserted, false).ok);
        QCOMPARE(db.scalar(str("SELECT status FROM donations WHERE rowid = ?"), {rowid}).toString(), str("New"));
        QCOMPARE(db.log(), (QStringList{str("After Insert: New gift from Ada")}));
        // Before Change on an update: [Old] and IsInsert.
        Change edited;
        QVERIFY(Records::updateCells(*db.file, str("donations"), {{rowid, str("amount"), str("30")}}, &edited).ok);
        QVERIFY(Records::updateCells(*db.file, str("donations"), {{rowid, str("amount"), str("35")}}, &edited).ok);
        QCOMPARE(db.scalar(str("SELECT updated FROM donations WHERE rowid = ?"), {rowid}).toLongLong(), qint64(2));
        QCOMPARE(db.scalar(str("SELECT status FROM donations WHERE rowid = ?"), {rowid}).toString(), str("New"));
        // RaiseError cancels — the window's edit and another tool's alike.
        const Result zero = Records::insertRecord(*db.file, str("donations"), {{str("donor"), str("Bo")}, {str("amount"), str("0")}},
                                                  &rowid, &edited);
        QVERIFY(!zero.ok);
        QVERIFY2(zero.error.contains(str("Amounts are positive.")), qPrintable(zero.error));
        QVERIFY(!db.exec(str("UPDATE donations SET amount = -1")).ok);
        QVERIFY(db.exec(str("INSERT INTO donations(donor, amount, receipt) VALUES('Grace', 100, 1), ('Anonymous', 50, 0)")).ok);
        QCOMPARE(db.log(), (QStringList{str("After Insert: New gift from Ada"), str("After Insert: New gift from Grace")}));
        const Result receipted = db.exec(str("DELETE FROM donations WHERE donor = 'Grace'"));
        QVERIFY(!receipted.ok);
        QVERIFY(receipted.error.contains(str("Receipted gifts stay.")));
        Change removed;
        QVERIFY(Records::deleteRecords(*db.file, str("donations"),
                                       {db.scalar(str("SELECT rowid FROM donations WHERE donor = 'Anonymous'")).toLongLong()},
                                       &removed)
                    .ok);
        QCOMPARE(db.log().size(), 3);
        QCOMPARE(db.log().last(), str("After Delete: Gift deleted"));
        // Undoing the delete brings the record back without an After Insert
        // line; redoing it does not log the delete twice.
        QVERIFY(db.file->applyChange(removed, true).ok);
        QCOMPARE(db.log().size(), 2); // the delete's line went with it, and nothing new was logged
        QCOMPARE(db.scalar(str("SELECT status FROM donations WHERE donor = 'Anonymous'")).toString(), str("New"));
        QVERIFY(db.file->applyChange(removed, false).ok);
        QCOMPARE(db.log().size(), 3);
        QCOMPARE(db.log().count(str("After Delete: Gift deleted")), 1);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM qb_meta WHERE key LIKE 'qb.%'")).toLongLong(), qint64(0));
    }

    void macrosAreChecked()
    {
        Gifts db;
        const TableDef t = readTable(*db.file, str("donations")).value();
        const CurrencyFormat money = db.file->currency();
        QVERIFY(checkDataMacro(t, str("afterInsert"), macro({step("openForm", {{str("form"), str("Donor")}})}), money)
                    .first()
                    .contains(str("window actions belong to ordinary macros")));
        QVERIFY(checkDataMacro(t, str("afterUpdate"), macro({step("setField", {{str("field"), str("nope")}, {str("value"), str("1")}})}),
                               money)
                    .first()
                    .contains(str("there is no field nope")));
        QVERIFY(checkDataMacro(t, str("afterDelete"),
                               macro({step("setField", {{str("field"), str("status")}, {str("value"), str("\"x\"")}})}), money)
                    .first()
                    .contains(str("deleted record")));
        QVERIFY(checkDataMacro(t, str("beforeChange"), macro({step("raiseError", {{str("description"), str("=[donor]")}})}), money)
                    .first()
                    .contains(str("as text")));
        QVERIFY(checkDataMacro(t, str("afterInsert"), macro({step("setField", {{str("field"), str("id")}, {str("value"), str("1")}})}),
                               money)
                    .first()
                    .contains(str("cannot be set")));
        QVERIFY(checkDataMacro(t, str("afterInsert"), macro({step("logEvent", {})}), money).first().contains(str("fill in")));
        QVERIFY(checkDataMacro(t, str("onTuesday"), macro({}), money).first().contains(str("no table event")));
        QVERIFY(planDataMacro(*db.file, str("donations"), str("afterInsert"),
                              macro({step("logEvent", {{str("description"), str("=[missing]")}})}))
                    .error.contains(str("missing")));
    }

    void removedAndRenamed()
    {
        Gifts db;
        QVERIFY(db.set("afterInsert", macro({step("logEvent", {{str("description"), str("Added")}})})).ok);
        QCOMPARE(db.triggers(), (QStringList{str("qbm:donations:ai")}));
        const TableDef before = readTable(*db.file, str("donations")).value();
        QCOMPARE(dataMacro(before, str("afterInsert")).steps.size(), 1);
        TableDef renamed = before;
        renamed.name = str("gifts");
        QVERIFY2(db.apply(planAlterTable(*db.file, before, renamed)).ok, qPrintable(planAlterTable(*db.file, before, renamed).error));
        QCOMPARE(db.triggers(), (QStringList{str("qbm:gifts:ai")}));
        QVERIFY(db.exec(str("INSERT INTO gifts(donor) VALUES('Ada')")).ok);
        QCOMPARE(db.scalar(str("SELECT source_object FROM qb_application_log")).toString(), str("gifts"));
        const SchemaPlan clear = planDataMacro(*db.file, str("gifts"), str("afterInsert"), Macro());
        QVERIFY2(clear.ok(), qPrintable(clear.error));
        QVERIFY(db.apply(clear).ok);
        QVERIFY(db.triggers().isEmpty());
        QVERIFY(readTable(*db.file, str("gifts"))->dataMacros.isEmpty());
    }
};

BASE_TEST_MAIN(BaseDataMacroTest)

#include "tst_base_data_macro.moc"
