// SPDX-License-Identifier: GPL-3.0-or-later
// The SQL view in Access SQL (M80, D-089 §4): a design prints as the
// statement Access shows, and that statement parses back into a design
// that runs the same query — for every kind the grid holds; hand-written
// Access SQL becomes a design; what the grid cannot hold is refused with
// the reason.

#include "base_fixture.h"

#include "base_access_sql.h"
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
        create(table("archive", {field("id", FieldType::AutoNumber), field("who", FieldType::Text),
                                 field("amount", FieldType::Currency)}));
    }
    QueryResult run(const QueryDesign &design, const QVariantMap &params = {})
    {
        const CompiledQuery c = compileQuery(*file, design, params);
        QueryResult r;
        if (!c.ok) {
            r.error = c.error;
            return r;
        }
        if (c.action) {
            qint64 affected = 0;
            const Result preview = previewAction(*file, c.sql, c.parameters, params, &affected);
            r.ok = preview.ok;
            r.error = preview.error;
            r.rows = {{affected}};
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

QueryDesign joined(QList<QueryColumn> columns)
{
    QueryDesign d;
    d.tables = {{str("donations"), QString()}, {str("donors"), QString()}};
    d.joins.append({str("donations"), str("donor_id"), str("donors"), str("id"), str("inner")});
    d.columns = std::move(columns);
    return d;
}

} // namespace

class BaseAccessSqlTest final : public QObject {
    Q_OBJECT

    // A design survives print → parse: the same SQL, or (where only
    // parentheses differ) the same records under the same names.
    static void roundTrips(Seeded &db, const QueryDesign &design, const QVariantMap &params = {})
    {
        const QString text = accessSql(design);
        const AccessSqlParse back = parseAccessSql(*db.file, text);
        QVERIFY2(back.ok, qPrintable(back.error + str("\n") + text));
        const CompiledQuery a = compileQuery(*db.file, design, params);
        const CompiledQuery b = compileQuery(*db.file, back.design, params);
        QVERIFY2(a.ok && b.ok, qPrintable(a.error + b.error));
        QCOMPARE(b.action, a.action);
        QCOMPARE(b.makeTable, a.makeTable);
        if (a.sql == b.sql) {
            return;
        }
        const QueryResult x = db.run(design, params);
        const QueryResult y = db.run(back.design, params);
        QVERIFY2(x.ok && y.ok, qPrintable(x.error + y.error));
        QCOMPARE(y.rows, x.rows);
        QStringList xs, ys;
        for (const OutputColumn &c : x.columns) {
            xs.append(c.name);
        }
        for (const OutputColumn &c : y.columns) {
            ys.append(c.name);
        }
        QCOMPARE(ys, xs);
    }

private slots:
    void printsWhatAccessShows()
    {
        QueryColumn donor = col("donors", "name", "", 1);
        donor.alias = str("Donor");
        const QueryDesign d = joined({donor, col("donations", "amount", "> 20")});
        QCOMPARE(accessSql(d), str("SELECT donors.[name] AS Donor, donations.amount\n"
                                   "FROM donations INNER JOIN donors ON donations.donor_id = donors.id\n"
                                   "WHERE donations.amount > 20\n"
                                   "ORDER BY donors.[name];"));
        QueryColumn total = col("donations", "amount");
        total.total = TotalKind::Sum;
        total.alias = str("Total given");
        total.criteria = str(">= 30");
        QueryDesign totals = joined({donor, total});
        totals.totals = true;
        QCOMPARE(accessSql(totals), str("SELECT donors.[name] AS Donor, Sum(donations.amount) AS [Total given]\n"
                                        "FROM donations INNER JOIN donors ON donations.donor_id = donors.id\n"
                                        "GROUP BY donors.[name]\n"
                                        "HAVING Sum(donations.amount) >= 30\n"
                                        "ORDER BY donors.[name];"));
        QueryDesign params;
        params.tables = {{str("donations"), QString()}};
        params.columns = {col("donations", "amount", ">= [Minimum]")};
        QueryParameter minimum;
        minimum.prompt = str("Minimum");
        minimum.type = FieldType::Currency;
        params.parameters = {minimum};
        params.top = 25;
        params.topPercent = true;
        QCOMPARE(accessSql(params), str("PARAMETERS [Minimum] Currency;\n"
                                        "SELECT TOP 25 PERCENT donations.amount\n"
                                        "FROM donations\n"
                                        "WHERE donations.amount >= [Minimum];"));
    }

    void everyKindRoundTrips()
    {
        Seeded db;
        QueryColumn donor = col("donors", "name", "", 1);
        donor.alias = str("Donor");
        QueryColumn amount = col("donations", "amount", "Between 10 And 100");
        amount.orCriteria = {str("> 120")};
        roundTrips(db, joined({donor, amount, col("donations", "given", "", -1)}));
        // Totals with WHERE and HAVING.
        QueryColumn sum = col("donations", "amount", "> 30");
        sum.total = TotalKind::Sum;
        QueryColumn year;
        year.expression = str("Year([given])");
        year.total = TotalKind::Where;
        year.criteria = str("2026");
        QueryDesign totals = joined({donor, sum, year});
        totals.totals = true;
        roundTrips(db, totals);
        // A crosstab with fixed headings.
        QueryColumn row = col("donors", "name");
        row.crosstab = str("row");
        QueryColumn heading;
        heading.expression = str("Year([given])");
        heading.crosstab = str("column");
        QueryColumn value = col("donations", "amount");
        value.total = TotalKind::Sum;
        value.crosstab = str("value");
        QueryDesign crosstab = joined({row, heading, value});
        crosstab.kind = QueryKind::Crosstab;
        crosstab.columnHeadings = {str("2026"), str("2025")};
        roundTrips(db, crosstab);
        // Update, delete, append and make-table.
        QueryDesign update;
        update.kind = QueryKind::Update;
        update.tables = {{str("donations"), QString()}};
        QueryColumn raise = col("donations", "amount", "< 20");
        raise.updateTo = str("[amount] + 5");
        update.columns = {raise};
        roundTrips(db, update);
        QueryDesign remove;
        remove.kind = QueryKind::Delete;
        remove.tables = {{str("donations"), QString()}};
        remove.columns = {col("donations", "given", "< #1/1/2026#")};
        roundTrips(db, remove);
        QueryColumn who = col("donors", "name");
        who.appendTo = str("who");
        QueryColumn much = col("donations", "amount", ">= 50");
        much.appendTo = str("amount");
        QueryDesign append = joined({who, much});
        append.kind = QueryKind::Append;
        append.appendTable = str("archive");
        roundTrips(db, append);
        QueryDesign make = joined({donor, col("donations", "amount")});
        make.kind = QueryKind::MakeTable;
        make.makeTable = str("Gift List");
        roundTrips(db, make);
        // Parameters, distinct and top values.
        QueryDesign params;
        params.tables = {{str("donations"), QString()}};
        params.columns = {col("donations", "donor_id"), col("donations", "given", "Between [Start] And [End]")};
        params.distinct = true;
        roundTrips(db, params, {{str("p_start"), str("1/1/2026")}, {str("p_end"), str("2026-12-31")}});
    }

    void handWrittenAccessSqlBecomesADesign()
    {
        Seeded db;
        AccessSqlParse p = parseAccessSql(*db.file, str("SELECT * FROM donors WHERE [name] Like \"A*\" ORDER BY [name] DESC;"));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.design.columns.size(), 2); // * and the hidden column the criteria and sort sit on
        QVERIFY(!p.design.columns.at(1).show);
        QueryResult r = db.run(p.design);
        QCOMPARE(r.rows.size(), 2);
        QCOMPARE(r.rows.first().at(1).toString(), str("Alan Turing"));
        // Declared parameters keep their type.
        p = parseAccessSql(*db.file, str("PARAMETERS [Minimum] Currency; SELECT donations.amount FROM donations "
                                         "WHERE donations.amount >= [Minimum];"));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.design.parameters.size(), 1);
        QCOMPARE(compileQuery(*db.file, p.design).parameters.first().type, FieldType::Currency);
        QCOMPARE(db.run(p.design, {{str("p_minimum"), str("30")}}).rows.size(), 2);
        // TRANSFORM … PIVOT is a crosstab.
        p = parseAccessSql(*db.file, str("TRANSFORM Sum(donations.amount) AS SumOfamount\n"
                                         "SELECT donors.name FROM donations INNER JOIN donors ON donations.donor_id = donors.id\n"
                                         "GROUP BY donors.name\nPIVOT Year([given]);"));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.design.kind, QueryKind::Crosstab);
        r = db.run(p.design);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.columns.size(), 3);
        // ON naming the right table first keeps "every row of" the right side.
        p = parseAccessSql(*db.file, str("SELECT donors.name, donations.amount FROM donors LEFT JOIN donations "
                                         "ON donations.donor_id = donors.id;"));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(db.run(p.design).rows.size(), 5);
        // Rows of one OR across fields become the grid's Or rows.
        p = parseAccessSql(*db.file, str("SELECT donors.name FROM donors WHERE donors.name = \"Ada Lovelace\" "
                                         "OR donors.email Is Null;"));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(db.run(p.design).rows.size(), 2);
        // An action query in Access SQL.
        p = parseAccessSql(*db.file, str("UPDATE donations SET donations.amount = [amount] + 5 WHERE donations.amount < 20;"));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.design.kind, QueryKind::Update);
        QCOMPARE(db.run(p.design).rows.first().first().toLongLong(), qint64(2));
    }

    void whatTheGridCannotHoldIsRefused()
    {
        Seeded db;
        const char *const refused[] = {
            "SELECT name FROM donors UNION SELECT name FROM campaigns;",
            "SELECT donors.name FROM donors INNER JOIN donations ON donations.donor_id = donors.id "
            "AND donations.id = donors.id;",
            "INSERT INTO archive (who) VALUES (\"x\");",
            "SELECT * FROM donors; SELECT * FROM donations;",
            "SELECT * FROM nowhere;",
            "SELECT donors.name FROM donors WHERE (donors.name = \"A\" Or donors.name = \"B\") And donors.id > 1 "
            "And donors.email = \"x\" And (donors.id = 1 Or donors.email Is Null);",
            "DROP TABLE donors;"};
        for (const char *sql : refused) {
            const AccessSqlParse p = parseAccessSql(*db.file, str(sql));
            QVERIFY2(!p.ok, sql);
            QVERIFY2(!p.error.isEmpty(), sql);
        }
        QVERIFY(parseAccessSql(*db.file, str(refused[0])).error.contains(str("UNION")));
    }
};

BASE_TEST_MAIN(BaseAccessSqlTest)

#include "tst_base_access_sql.moc"
