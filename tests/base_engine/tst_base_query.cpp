// SPDX-License-Identifier: GPL-3.0-or-later
// Queries: the designer grid compiles to readable SQL with typed, quoted
// literals; joins, totals, Or rows, expressions, parameters; hand-written
// SQL; action queries that run only with a confirmed count.

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
    QueryResult select(const QueryDesign &design, const QVariantMap &params = {})
    {
        const CompiledQuery c = compileQuery(*file, design);
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

QueryDesign design(std::initializer_list<const char *> tables, QList<QueryColumn> columns)
{
    QueryDesign d;
    for (const char *t : tables) {
        d.tables.append({str(t), QString()});
    }
    d.columns = std::move(columns);
    if (d.tables.size() == 2) {
        d.joins.append({str("donations"), str("donor_id"), str("donors"), str("id"), str("inner")});
    }
    return d;
}

} // namespace

class BaseQueryTest final : public QObject {
    Q_OBJECT
private slots:
    void criteriaConvertByType()
    {
        Seeded db;
        // "> 20" on currency is twenty dollars: 2000 minor units.
        QueryDesign d = design({"donations"}, {col("donations", "id"), col("donations", "amount", "> 20", -1)});
        CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QVERIFY2(c.sql.contains(str("\"donations\".\"amount\" > 2000")), qPrintable(c.sql));
        QueryResult r = db.select(d);
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.columns.at(1).type, FieldType::Currency);
        QCOMPARE(r.rows.first().at(1).toLongLong(), qint64(12345));
        d.columns[1].criteria = str("Between 10 And 25");
        QCOMPARE(db.select(d).rows.size(), 2);
        d.columns[1].criteria = str("In (1, 25.00, 49)");
        QCOMPARE(db.select(d).rows.size(), 2);
        d.columns[1].criteria = str(">= 10 And < 30 Or = 50");
        QCOMPARE(db.select(d).rows.size(), 3);
        d.columns[1].criteria = str("lots");
        QVERIFY(!compileQuery(*db.file, d).ok);
        // Dates: #m/d/yyyy#, ISO text, and Year() in an expression.
        d = design({"donations"}, {col("donations", "given", ">= #1/1/2026# And < \"2026-03-01\"")});
        QCOMPARE(db.select(d).rows.size(), 3);
        // Text: bare words, Like with Access wildcards, Is Null, Not.
        d = design({"donors"}, {col("donors", "name", "Like \"A*\"", 1)});
        r = db.select(d);
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.rows.first().first().toString(), str("Ada Lovelace"));
        d = design({"donors"}, {col("donors", "name", "Grace Hopper")});
        QCOMPARE(db.select(d).rows.size(), 1);
        d = design({"donors"}, {col("donors", "email", "Is Null")});
        QCOMPARE(db.select(d).rows.size(), 1);
        d = design({"donors"}, {col("donors", "name", "Not \"Ada Lovelace\"")});
        QCOMPARE(db.select(d).rows.size(), 2);
        d = design({"donors"}, {col("donors", "name", "Like \"100%\"")}); // % is literal, not a wildcard
        QVERIFY(compileQuery(*db.file, d).sql.contains(str("'100\\%'")));
        // Quotes in user text stay inside a literal.
        d = design({"donors"}, {col("donors", "name", "\"x' OR 1=1 --\"")});
        QCOMPARE(db.select(d).rows.size(), 0);
    }

    void orRowsAndCrossFieldCriteria()
    {
        Seeded db;
        QueryColumn name = col("donors", "name", "\"Ada Lovelace\"");
        QueryColumn email = col("donors", "email");
        name.orCriteria = {str("Alan Turing")};
        email.orCriteria = {str("Is Null")};
        const QueryDesign d = design({"donors"}, {name, email});
        const CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY(c.sql.contains(str(") OR (")));
        QCOMPARE(db.select(d).rows.size(), 2);
        // A criteria can name another field.
        QueryDesign sameAsId = design({"donations"}, {col("donations", "id"), col("donations", "donor_id", "= [id]")});
        QCOMPARE(db.select(sameAsId).rows.size(), 1);
    }

    void joinsTotalsAndExpressions()
    {
        Seeded db;
        QueryColumn donor = col("donors", "name", "", 1);
        donor.alias = str("Donor");
        QueryColumn total = col("donations", "amount");
        total.total = TotalKind::Sum;
        QueryColumn gifts = col("donations", "id");
        gifts.total = TotalKind::Count;
        QueryColumn year;
        year.expression = str("Year([given])");
        year.alias = str("Year");
        year.total = TotalKind::Where;
        year.criteria = str("2026");
        QueryDesign d = design({"donations", "donors"}, {donor, total, gifts, year});
        d.totals = true;
        const CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QVERIFY(c.sql.contains(str("INNER JOIN \"donors\" ON \"donations\".\"donor_id\" = \"donors\".\"id\"")));
        QVERIFY(c.sql.contains(str("GROUP BY")));
        const QueryResult r = db.select(d);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows.size(), 3);
        QCOMPARE(r.columns.at(1).name, str("SumOfamount"));
        QCOMPARE(r.columns.at(1).type, FieldType::Currency); // exact integer sum, shown as money
        QCOMPARE(r.rows.at(0).at(1).toLongLong(), qint64(3500)); // Ada, 2026 only
        QCOMPARE(r.rows.at(1).at(1).toLongLong(), qint64(100));
        QCOMPARE(r.rows.at(2).at(1).toLongLong(), qint64(12345)); // Grace's 2025 gift excluded
        QCOMPARE(r.columns.at(2).type, FieldType::Integer);
        // HAVING on an aggregate: donors who gave more than $30 in all.
        QueryDesign having = design({"donations", "donors"}, {donor, total});
        having.totals = true;
        having.columns[1].criteria = str("> 30");
        QCOMPARE(db.select(having).rows.size(), 2);
        // A computed column keeps money in minor units.
        QueryColumn doubled;
        doubled.expression = str("[amount] * 2 + 1");
        doubled.alias = str("Doubled");
        QueryResult calc = db.select(design({"donations"}, {col("donations", "id", "1"), doubled}));
        QVERIFY2(calc.ok, qPrintable(calc.error));
        QCOMPARE(calc.rows.first().at(1).toLongLong(), qint64(5100)); // 25.00*2 + 1.00
        QCOMPARE(calc.columns.at(1).type, FieldType::Currency);
        QueryColumn label;
        label.expression = str("UCase(Left([name], 3)) & \"-\" & IIf(IsNull, 0, 1)");
        QVERIFY(!compileQuery(*db.file, design({"donors"}, {label})).ok); // unknown name, with a hint
        label.expression = str("UCase(Left([name], 3)) & \"-\" & Nz([email], \"none\")");
        calc = db.select(design({"donors"}, {col("donors", "id", "3"), label}));
        QCOMPARE(calc.rows.first().at(1).toString(), str("ALA-none"));
    }

    void parametersAreAskedFor()
    {
        Seeded db;
        QueryDesign d = design({"donations"}, {col("donations", "id"),
                                               col("donations", "given", "Between [Start date] And [End date]"),
                                               col("donations", "amount", ">= [Minimum]")});
        const CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QCOMPARE(c.parameters.size(), 3);
        QCOMPARE(c.parameters.at(0).prompt, str("Start date"));
        QCOMPARE(c.parameters.at(0).type, FieldType::Date);
        QCOMPARE(c.parameters.at(2).type, FieldType::Currency);
        QVERIFY(c.sql.contains(str(":p_start_date")));
        QueryResult r = runSelect(*db.file, c.sql, c.parameters,
                                  {{str("p_start_date"), str("1/1/2026")}, {str("p_end_date"), str("2026-02-28")},
                                   {str("p_minimum"), str("20")}});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows.size(), 2); // 25.00 and 123.45 in Jan–Feb
        r = runSelect(*db.file, c.sql, c.parameters, {{str("p_start_date"), str("soon")}});
        QVERIFY(r.error.contains(str("Start date")));
    }

    void handWrittenSql()
    {
        Seeded db;
        QueryDesign d;
        d.kind = QueryKind::Sql;
        d.sql = str("SELECT name, (SELECT sum(amount) FROM donations WHERE donor_id = donors.id) AS total FROM donors "
                    "WHERE name LIKE :pattern ORDER BY name");
        const CompiledQuery c = compileQuery(*db.file, d);
        QVERIFY2(c.ok, qPrintable(c.error));
        QVERIFY(!c.action);
        QCOMPARE(c.parameters.first().name, str("pattern"));
        const QueryResult r = runSelect(*db.file, c.sql, c.parameters, {{str("pattern"), str("A%")}});
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.columns.first().type, FieldType::Text);
        QCOMPARE(r.columns.first().sourceTable, str("donors"));
        d.sql = str("DROP TABLE donations");
        QVERIFY(compileQuery(*db.file, d).error.contains(str("Database Tools")));
        d.sql = str("SELECT 1; SELECT 2");
        QVERIFY(!compileQuery(*db.file, d).ok);
        d.sql = str("PRAGMA foreign_keys=OFF");
        QVERIFY(!compileQuery(*db.file, d).ok);
    }

    void actionQueriesNeedConfirmedCounts()
    {
        Seeded db;
        QueryColumn amount = col("donations", "amount", "< 20");
        amount.updateTo = str("[amount] + 5");
        QueryDesign update = design({"donations"}, {amount});
        update.kind = QueryKind::Update;
        const CompiledQuery c = compileQuery(*db.file, update);
        QVERIFY2(c.ok, qPrintable(c.error));
        QVERIFY(c.action);
        QVERIFY2(c.sql.startsWith(str("UPDATE \"donations\" SET \"amount\" = (\"amount\" + 500)")), qPrintable(c.sql));
        qint64 affected = -1;
        QVERIFY(previewAction(*db.file, c.sql, c.parameters, {}, &affected).ok);
        QCOMPARE(affected, qint64(2));
        QCOMPARE(db.scalar(str("SELECT sum(amount) FROM donations")).toLongLong(), qint64(20945)); // preview changed nothing
        db.exec(str("UPDATE donations SET amount = 1 WHERE id = 3")); // someone else edits in between
        Change change;
        Result r = runAction(*db.file, c.sql, c.parameters, {}, affected, &change, &affected);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(str("3 record(s) would change now, not 2")));
        QVERIFY(previewAction(*db.file, c.sql, c.parameters, {}, &affected).ok);
        r = runAction(*db.file, c.sql, c.parameters, {}, affected, &change, &affected);
        QVERIFY(r.ok);
        QVERIFY(db.file->applyChange(change, true).ok);
        QCOMPARE(db.scalar(str("SELECT amount FROM donations WHERE id=4")).toLongLong(), qint64(100));
        // Delete and append.
        QueryDesign del = design({"donations"}, {col("donations", "given", "< #1/1/2026#")});
        del.kind = QueryKind::Delete;
        const QString deleteSql = compileQuery(*db.file, del).sql;
        QVERIFY(deleteSql.startsWith(str("DELETE FROM \"donations\"")) && deleteSql.contains(str("WHERE")));
        QVERIFY(db.create(table("archive", {field("id", FieldType::AutoNumber), field("who", FieldType::Text),
                                            field("amount", FieldType::Currency)}))
                    .ok);
        QueryColumn who = col("donors", "name");
        who.appendTo = str("who");
        QueryColumn much = col("donations", "amount");
        much.appendTo = str("amount");
        QueryDesign append = design({"donations", "donors"}, {who, much});
        append.kind = QueryKind::Append;
        append.appendTable = str("archive");
        const CompiledQuery a = compileQuery(*db.file, append);
        QVERIFY2(a.ok, qPrintable(a.error));
        QVERIFY(previewAction(*db.file, a.sql, a.parameters, {}, &affected).ok);
        QCOMPARE(affected, qint64(5));
        QVERIFY(runAction(*db.file, a.sql, a.parameters, {}, 5, nullptr, &affected).ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM archive")).toInt(), 5);
    }

    void designsRoundTripUnknownMembers()
    {
        QVariantMap map = design({"donations"}, {col("donations", "amount", "> 1")}).toMap();
        map.insert(str("layout"), QVariantMap{{str("zoom"), 2}});
        const QueryDesign back = QueryDesign::fromMap(map);
        QCOMPARE(back.columns.first().criteria, str("> 1"));
        QCOMPARE(back.toMap().value(str("layout")).toMap().value(str("zoom")).toInt(), 2);
    }
};

BASE_TEST_MAIN(BaseQueryTest)

#include "tst_base_query.moc"
