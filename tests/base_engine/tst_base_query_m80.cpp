// SPDX-License-Identifier: GPL-3.0-or-later
// M80 query kinds (D-089 §3) against hand-worked results: crosstabs
// (Access's TRANSFORM … PIVOT) with row summaries, fixed headings and
// parameters; make-table queries as one undoable schema step that replaces
// its table with a backup; declared parameters asked first with their own
// types; Top n percent.
//
// The seed: Ada (1) gave 25.00 on 2026-01-05 and 10.00 on 2026-02-10;
// Grace (2) 123.45 on 2026-02-11 and 50.00 on 2025-12-24; Alan (3) 1.00
// on 2026-03-01.

#include "base_fixture.h"

#include "base_query.h"

using namespace BaseTest;

namespace {

struct Seeded : Db {
    Seeded()
    {
        donorsAndDonations(RefAction::Restrict);
        exec(str("INSERT INTO donors(name, email) VALUES('Ada Lovelace', 'ada@x.org'), ('Grace Hopper', 'g@x.org'), "
                 "('Alan Turing', NULL)"));
        exec(str("INSERT INTO donations(donor_id, amount, given) VALUES(1, 2500, '2026-01-05'), (1, 1000, '2026-02-10'), "
                 "(2, 12345, '2026-02-11'), (3, 100, '2026-03-01'), (2, 5000, '2025-12-24')"));
    }
    QueryResult run(const QueryDesign &design, const QVariantMap &params = {})
    {
        const CompiledQuery c = compileQuery(*file, design, params);
        if (!c.ok) {
            QueryResult r;
            r.error = c.error;
            return r;
        }
        return runSelect(*file, c.sql, c.parameters, params, 100000, c.columns);
    }
};

QueryColumn col(const char *table, const char *field, const char *criteria = "", int sort = 0)
{
    QueryColumn c;
    c.table = str(table);
    c.field = str(field);
    c.criteria = str(criteria);
    c.sort = sort;
    return c;
}

QueryDesign twoTables(QList<QueryColumn> columns)
{
    QueryDesign d;
    d.tables = {{str("donations"), QString()}, {str("donors"), QString()}};
    d.joins.append({str("donations"), str("donor_id"), str("donors"), str("id"), str("inner")});
    d.columns = std::move(columns);
    return d;
}

// Donors down, years across, the sum of their gifts in the cells.
QueryDesign giftsByYear()
{
    QueryColumn who = col("donors", "name");
    who.crosstab = str("row");
    QueryColumn year;
    year.expression = str("Year([given])");
    year.crosstab = str("column");
    QueryColumn amount = col("donations", "amount");
    amount.total = TotalKind::Sum;
    amount.crosstab = str("value");
    QueryDesign d = twoTables({who, year, amount});
    d.kind = QueryKind::Crosstab;
    return d;
}

bool isNull(const QVariant &v)
{
    return !v.isValid() || v.isNull();
}

} // namespace

class BaseQueryM80Test final : public QObject {
    Q_OBJECT
private slots:
    void crosstabPivotsTheTotals()
    {
        Seeded db;
        QueryDesign d = giftsByYear();
        const CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QVERIFY(c.crosstab);
        QCOMPARE(c.headings, (QStringList{str("2025"), str("2026")}));
        QueryResult r = db.run(d);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.columns.size(), 3);
        QCOMPARE(r.columns.at(1).name, str("2025"));
        QCOMPARE(r.columns.at(2).type, FieldType::Currency); // a money sum stays minor units
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.rows.at(0).at(0).toString(), str("Ada Lovelace"));
        QVERIFY(isNull(r.rows.at(0).at(1)));                 // no gift in 2025: an empty cell
        QCOMPARE(r.rows.at(0).at(2).toLongLong(), qint64(3500));
        QCOMPARE(r.rows.at(1).at(0).toString(), str("Alan Turing"));
        QCOMPARE(r.rows.at(1).at(2).toLongLong(), qint64(100));
        QCOMPARE(r.rows.at(2).at(1).toLongLong(), qint64(5000)); // Grace, 2025
        QCOMPARE(r.rows.at(2).at(2).toLongLong(), qint64(12345));
        // A row summary: Sum of everything a donor gave, beside the years.
        QueryColumn total = col("donations", "amount");
        total.total = TotalKind::Sum;
        total.alias = str("Total");
        total.crosstab = str("row");
        d.columns.insert(1, total);
        r = db.run(d);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.columns.at(1).name, str("Total"));
        QCOMPARE(r.rows.at(2).at(1).toLongLong(), qint64(17345));
        // Count leaves empty cells empty, as Access shows them.
        d = giftsByYear();
        d.columns[2].total = TotalKind::Count;
        r = db.run(d);
        QCOMPARE(r.rows.at(2).at(1).toLongLong(), qint64(1));
        QVERIFY(isNull(r.rows.at(0).at(1)));
        // Fixed column headings, in their order, even with no records.
        d = giftsByYear();
        d.columnHeadings = {str("2026"), str("2024")};
        r = db.run(d);
        QCOMPARE(r.columns.at(1).name, str("2026"));
        QCOMPARE(r.columns.at(2).name, str("2024"));
        QCOMPARE(r.rows.at(0).at(1).toLongLong(), qint64(3500));
        QVERIFY(isNull(r.rows.at(2).at(2)));
        // Criteria on the column heading filter the rows before the pivot.
        d = giftsByYear();
        d.columns[1].criteria = str("2026");
        r = db.run(d);
        QCOMPARE(r.columns.size(), 2);
        QCOMPARE(r.rows.at(2).at(1).toLongLong(), qint64(12345));
    }

    void crosstabHeadingsFollowTheParameters()
    {
        Seeded db;
        QueryDesign d = giftsByYear();
        QueryColumn since = col("donations", "given", ">= [From]");
        since.total = TotalKind::Where;
        d.columns.append(since);
        // Without values (a preview) the headings are every year there is.
        CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QCOMPARE(c.headings, (QStringList{str("2025"), str("2026")}));
        QCOMPARE(c.parameters.first().type, FieldType::Date);
        const QVariantMap from{{str("p_from"), str("1/1/2026")}};
        c = compileQuery(*db.file, d, from);
        QCOMPARE(c.headings, QStringList{str("2026")});
        const QueryResult r = db.run(d, from);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.rows.at(2).at(1).toLongLong(), qint64(12345));
        // A crosstab without its parts says what is missing.
        d = giftsByYear();
        d.columns[0].crosstab.clear();
        QVERIFY(compileQuery(*db.file, d).error.contains(str("Row Heading")));
        d = giftsByYear();
        d.columns[2].total = TotalKind::GroupBy;
        QVERIFY(compileQuery(*db.file, d).error.contains(str("Value is a total")));
    }

    void makeTableIsOneUndoableStep()
    {
        Seeded db;
        QueryColumn donor = col("donors", "name");
        donor.alias = str("Donor");
        QueryDesign d = twoTables({donor, col("donations", "amount", ">= [Minimum]"), col("donations", "given")});
        d.kind = QueryKind::MakeTable;
        d.makeTable = str("big_gifts");
        const CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QVERIFY(c.action);
        QCOMPARE(c.makeTable, str("big_gifts"));
        qint64 rows = 0;
        SchemaPlan plan = planMakeTable(*db.file, c, {{str("p_minimum"), str("25")}}, &rows);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QCOMPARE(rows, qint64(3)); // 25.00, 123.45 and 50.00
        QVERIFY(!plan.destructive);
        Change change;
        QVERIFY(db.apply(plan, &change).ok);
        const std::optional<TableDef> made = readTable(*db.file, str("big_gifts"));
        QVERIFY(made);
        QCOMPARE(made->fields.size(), 3);
        QCOMPARE(made->fields.at(0).name, str("Donor"));
        QCOMPARE(made->fields.at(1).type, FieldType::Currency); // money stays money
        QCOMPARE(made->fields.at(2).type, FieldType::Date);
        QCOMPARE(db.scalar(str("SELECT sum(amount) FROM big_gifts")).toLongLong(), qint64(19845));
        QVERIFY(db.file->applyChange(change, true).ok); // undo
        QVERIFY(!db.file->tableExists(str("big_gifts")));
        QVERIFY(db.file->applyChange(change, false).ok); // redo
        // Running it again replaces the table, after a backup.
        plan = planMakeTable(*db.file, c, {{str("p_minimum"), str("100")}}, &rows);
        QCOMPARE(rows, qint64(1));
        QVERIFY(plan.destructive);
        QVERIFY(plan.notes.join(QLatin1Char(' ')).contains(str("Deletes the table big_gifts and its 3 record(s)")));
        QVERIFY(db.apply(plan, &change).ok);
        QVERIFY(!change.backupPath.isEmpty());
        QCOMPARE(db.scalar(str("SELECT count(*) FROM big_gifts")).toInt(), 1);
        // It may not replace a table it reads, nor a system table.
        d.makeTable = str("donations");
        QVERIFY(compileQuery(*db.file, d).error.contains(str("cannot replace it")));
        d.makeTable = str("qb_objects");
        QVERIFY(!compileQuery(*db.file, d).ok);
        // A table on the one side of a relationship is not replaced.
        d.makeTable = str("donors");
        d.tables = {{str("donations"), QString()}};
        d.joins.clear();
        d.columns = {col("donations", "amount")};
        const CompiledQuery onto = compileQuery(*db.file, d);
        QVERIFY2(onto.ok, qPrintable(onto.error));
        QVERIFY(planMakeTable(*db.file, onto, {}, &rows).error.contains(str("relationship")));
    }

    void declaredParametersComeFirst()
    {
        Seeded db;
        QueryDesign d;
        d.tables = {{str("donations"), QString()}};
        d.columns = {col("donations", "id"), col("donations", "given", ">= [Start]"), col("donations", "amount", ">= [Minimum]")};
        QueryParameter minimum;
        minimum.prompt = str("Minimum");
        minimum.type = FieldType::Currency;
        minimum.declared = true;
        QueryParameter start;
        start.prompt = str("Start");
        start.type = FieldType::Text; // declared as text: not re-derived from the date it meets
        start.declared = true;
        d.parameters = {minimum, start};
        CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QCOMPARE(c.parameters.size(), 2);
        QCOMPARE(c.parameters.at(0).prompt, str("Minimum"));
        QCOMPARE(c.parameters.at(0).type, FieldType::Currency);
        QCOMPARE(c.parameters.at(1).type, FieldType::Text);
        const QueryResult r = db.run(d, {{str("p_minimum"), str("20")}, {str("p_start"), str("2026-01-01")}});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows.size(), 2); // 25.00 on Jan 5 and 123.45 on Feb 11
        // Saved and read back, the declarations keep their order and types.
        const QueryDesign back = QueryDesign::fromMap(d.toMap());
        QCOMPARE(back.parameters.size(), 2);
        QCOMPARE(back.parameters.at(0).type, FieldType::Currency);
        QVERIFY(back.parameters.at(1).declared);
    }

    void topPercentRoundsUp()
    {
        Seeded db;
        QueryDesign d;
        d.tables = {{str("donations"), QString()}};
        d.columns = {col("donations", "amount", "", -1)};
        d.top = 40;
        d.topPercent = true;
        QueryResult r = db.run(d);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows.size(), 2); // 40 % of 5
        QCOMPARE(r.rows.at(1).at(0).toLongLong(), qint64(5000));
        d.top = 50;
        QCOMPARE(db.run(d).rows.size(), 3); // 2.5 rounds up
        d.topPercent = false;
        d.top = 1;
        QCOMPARE(db.run(d).rows.first().first().toLongLong(), qint64(12345));
        // Unique values: Ada's two gifts give one row.
        QueryDesign who;
        who.tables = {{str("donations"), QString()}};
        who.columns = {col("donations", "donor_id")};
        who.distinct = true;
        QCOMPARE(db.run(who).rows.size(), 3);
    }
};

BASE_TEST_MAIN(BaseQueryM80Test)

#include "tst_base_query_m80.moc"
