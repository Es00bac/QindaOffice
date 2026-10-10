// SPDX-License-Identifier: GPL-3.0-or-later
// M86's queries (D-094) against hand-worked results: the Simple, Crosstab,
// Find Duplicates and Find Unmatched wizards; union queries compiled, run
// and round-tripped through the Access SQL view; subqueries in criteria;
// Format() and DatePart().
//
// The records: donors 1 Ada (Portland, ada@x), 2 Grace (Salem, grace@x),
// 3 Alan (Portland, no e-mail), 4 Ada (Bend, ada@x); donations 1–5 —
// donor 1: $25.00 2026-01-15 Card, $10.00 2026-04-02 Check, $3.01
// 2026-01-30 Card; donor 2: $50.00 2026-02-20 Card, $1.99 2025-12-31 Cash.

#include "base_fixture.h"

#include "base_access_sql.h"
#include "base_query.h"
#include "base_query_wizards.h"
#include "base_rules.h"

using namespace BaseTest;

namespace {

struct Gifts : Db {
    Gifts()
    {
        create(table("donors", {field("id", FieldType::AutoNumber), field("name", FieldType::Text),
                                field("city", FieldType::Text), field("email", FieldType::Text)}));
        TableDef donations = table("donations", {field("id", FieldType::AutoNumber), field("donor_id", FieldType::Integer),
                                                 field("amount", FieldType::Currency), field("given", FieldType::Date),
                                                 field("method", FieldType::Text)});
        Relationship r;
        r.childTable = str("donations");
        r.childField = str("donor_id");
        r.parentTable = str("donors");
        r.parentField = str("id");
        donations.relationships.append(r);
        create(donations);
        exec(str("INSERT INTO donors(name, city, email) VALUES('Ada', 'Portland', 'ada@x'), ('Grace', 'Salem', 'grace@x'),"
                 "('Alan', 'Portland', NULL), ('Ada', 'Bend', 'ada@x')"));
        exec(str("INSERT INTO donations(donor_id, amount, given, method) VALUES(1, 2500, '2026-01-15', 'Card'),"
                 "(1, 1000, '2026-04-02', 'Check'), (2, 5000, '2026-02-20', 'Card'), (2, 199, '2025-12-31', 'Cash'),"
                 "(1, 301, '2026-01-30', 'Card')"));
    }
    QueryResult run(const QueryDesign &design, const QVariantMap &parameters = {})
    {
        const CompiledQuery c = compileQuery(*file, design, parameters);
        if (!c.ok) {
            QueryResult failed;
            failed.error = c.error;
            return failed;
        }
        return runSelect(*file, c.sql, c.parameters, parameters, 1000, c.columns);
    }
    QueryResult runSql(const char *accessSql, const QVariantMap &parameters = {})
    {
        const AccessSqlParse p = parseAccessSql(*file, str(accessSql));
        if (!p.ok) {
            QueryResult failed;
            failed.error = p.error;
            return failed;
        }
        return run(p.design, parameters);
    }
};

QStringList column(const QueryResult &r, int index)
{
    QStringList out;
    for (const QVariantList &row : r.rows) {
        out.append(row.value(index).toString());
    }
    return out;
}

QStringList names(const QueryResult &r)
{
    QStringList out;
    for (const OutputColumn &c : r.columns) {
        out.append(c.name);
    }
    return out;
}

} // namespace

class BaseQueryM86Test final : public QObject {
    Q_OBJECT
private slots:
    void simpleQueryWizardTotalsByDonor()
    {
        Gifts db;
        SimpleQuerySpec spec;
        spec.fields = {{str("donors"), str("name")}, {str("donations"), str("amount")}};
        spec.summary = true;
        spec.totals = {{str("donations.amount"), str("sum")}, {str("donations.amount"), str("average")}};
        spec.countRecords = true;
        const WizardResult w = simpleQueryWizard(*db.file, spec);
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(w.design.joins.size(), 1); // from the relationship
        const QueryResult r = db.run(w.design);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(names(r), (QStringList{str("name"), str("Sum Of amount"), str("Avg Of amount"), str("Count Of donors")}));
        QCOMPARE(column(r, 0), (QStringList{str("Ada"), str("Grace")}));
        QCOMPARE(column(r, 1), (QStringList{str("3801"), str("5199")})); // minor units
        QCOMPARE(r.rows.at(0).at(2).toDouble(), 1267.0);
        QCOMPARE(r.rows.at(1).at(2).toDouble(), 2599.5);
        QCOMPARE(column(r, 3), (QStringList{str("3"), str("2")}));
        // By month, in calendar order.
        SimpleQuerySpec months;
        months.fields = {{str("donations"), str("given")}, {str("donations"), str("amount")}};
        months.summary = true;
        months.totals = {{str("donations.amount"), str("sum")}};
        months.dateGrouping = str("month");
        const QueryResult m = db.run(simpleQueryWizard(*db.file, months).design);
        QVERIFY2(m.ok, qPrintable(m.error));
        QCOMPARE(column(m, 0), (QStringList{str("December 2025"), str("January 2026"), str("February 2026"), str("April 2026")}));
        QCOMPARE(column(m, 1), (QStringList{str("199"), str("2801"), str("5000"), str("1000")}));
        // Unrelated tables are refused.
        QVERIFY(db.create(table("other", {field("id", FieldType::AutoNumber), field("x", FieldType::Text)})).ok);
        spec.fields.append({str("other"), str("x")});
        QVERIFY(simpleQueryWizard(*db.file, spec).error.contains(str("not related")));
    }

    void crosstabWizardByQuarter()
    {
        Gifts db;
        CrosstabWizardSpec spec;
        spec.table = str("donations");
        spec.rowHeadings = {str("method")};
        spec.columnHeading = str("given");
        spec.dateInterval = str("quarter");
        spec.valueField = str("amount");
        spec.function = str("sum");
        spec.rowSums = true;
        const WizardResult w = crosstabQueryWizard(*db.file, spec);
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(w.design.columnHeadings, (QStringList{str("Qtr 1"), str("Qtr 2"), str("Qtr 3"), str("Qtr 4")}));
        const QueryResult r = db.run(w.design);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(names(r), (QStringList{str("method"), str("Total Of amount"), str("Qtr 1"), str("Qtr 2"), str("Qtr 3"),
                                        str("Qtr 4")}));
        QCOMPARE(column(r, 0), (QStringList{str("Card"), str("Cash"), str("Check")}));
        QCOMPARE(column(r, 1), (QStringList{str("7801"), str("199"), str("1000")}));
        QCOMPARE(column(r, 2), (QStringList{str("7801"), QString(), QString()}));
        QCOMPARE(column(r, 3), (QStringList{QString(), QString(), str("1000")}));
        QCOMPARE(column(r, 5), (QStringList{QString(), str("199"), QString()}));
        spec.columnHeading = str("method");
        QVERIFY(crosstabQueryWizard(*db.file, spec).error.contains(str("row heading already")));
    }

    void findDuplicatesAndUnmatched()
    {
        Gifts db;
        const WizardResult counts = findDuplicatesWizard(*db.file, {str("donors"), {str("name")}, {}});
        QVERIFY2(counts.ok, qPrintable(counts.error));
        const QueryResult c = db.run(counts.design);
        QVERIFY2(c.ok, qPrintable(c.error));
        QCOMPARE(names(c), (QStringList{str("name"), str("NumberOfDups")}));
        QCOMPARE(c.rows.size(), 1);
        QCOMPARE(c.rows.first(), (QVariantList{str("Ada"), qint64(2)}));
        // With other fields: every record whose name repeats.
        const WizardResult records = findDuplicatesWizard(*db.file, {str("donors"), {str("name")}, {str("city")}});
        QVERIFY2(records.ok, qPrintable(records.error));
        QVERIFY(records.design.columns.first().criteria.startsWith(str("In (SELECT")));
        const QueryResult d = db.run(records.design);
        QVERIFY2(d.ok, qPrintable(d.error));
        QStringList cities = column(d, 1);
        cities.sort();
        QCOMPARE(cities, (QStringList{str("Bend"), str("Portland")}));
        // Two fields repeat together: name and e-mail (Alan has none).
        const QueryResult two = db.run(findDuplicatesWizard(*db.file, {str("donors"), {str("name"), str("email")}, {str("id")}}).design);
        QVERIFY2(two.ok, qPrintable(two.error));
        QStringList ids = column(two, 2);
        ids.sort();
        QCOMPARE(ids, (QStringList{str("1"), str("4")}));
        // Donors with no donation.
        UnmatchedSpec unmatched{str("donors"), str("donations"), str("id"), str("donor_id"), {str("id"), str("name")}};
        const WizardResult u = findUnmatchedWizard(*db.file, unmatched);
        QVERIFY2(u.ok, qPrintable(u.error));
        const QueryResult none = db.run(u.design);
        QVERIFY2(none.ok, qPrintable(none.error));
        QStringList missing = column(none, 0);
        missing.sort();
        QCOMPARE(missing, (QStringList{str("3"), str("4")}));
        QCOMPARE(names(none), (QStringList{str("id"), str("name")}));
    }

    void unionQueriesRunAndRoundTrip()
    {
        Gifts db;
        const QueryResult r = db.runSql("SELECT name, city FROM donors WHERE city = \"Portland\" UNION "
                                        "SELECT name, city FROM donors WHERE city = \"Bend\" ORDER BY name, city;");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(column(r, 0), (QStringList{str("Ada"), str("Ada"), str("Alan")}));
        QCOMPARE(column(r, 1), (QStringList{str("Bend"), str("Portland"), str("Portland")}));
        QCOMPARE(db.runSql("SELECT city FROM donors UNION SELECT city FROM donors;").rows.size(), 3);
        QCOMPARE(db.runSql("SELECT city FROM donors UNION ALL SELECT city FROM donors;").rows.size(), 8);
        const QueryResult numbered = db.runSql("SELECT name FROM donors WHERE city = 'Portland' UNION ALL "
                                               "SELECT name FROM donors WHERE city = 'Salem' ORDER BY 1 DESC;");
        QVERIFY2(numbered.ok, qPrintable(numbered.error));
        QCOMPARE(column(numbered, 0), (QStringList{str("Grace"), str("Alan"), str("Ada")}));
        // The design prints back to Access SQL that parses to the same query.
        const AccessSqlParse parsed = parseAccessSql(*db.file, str("PARAMETERS [Which city] Text; SELECT name FROM donors "
                                                                   "WHERE city = [Which city] UNION SELECT name FROM donors "
                                                                   "WHERE name = \"Grace\" ORDER BY name DESC;"));
        QVERIFY2(parsed.ok, qPrintable(parsed.error));
        QCOMPARE(parsed.design.kind, QueryKind::Union);
        QCOMPARE(parsed.design.parts.size(), 2);
        QCOMPARE(parsed.design.parameters.size(), 1);
        const QString text = accessSql(parsed.design);
        QVERIFY2(text.startsWith(str("PARAMETERS [Which city] Text;\nSELECT")), qPrintable(text));
        QVERIFY2(text.contains(str("\nUNION\n")), qPrintable(text));
        QVERIFY2(text.endsWith(str("ORDER BY [name] DESC;")), qPrintable(text));
        const AccessSqlParse again = parseAccessSql(*db.file, text);
        QVERIFY2(again.ok, qPrintable(again.error));
        QCOMPARE(compileQuery(*db.file, again.design).sql, compileQuery(*db.file, parsed.design).sql);
        const CompiledQuery compiled = compileQuery(*db.file, parsed.design);
        QCOMPARE(compiled.parameters.size(), 1);
        const QueryResult bend = db.run(parsed.design, {{compiled.parameters.first().name, str("Bend")}});
        QVERIFY2(bend.ok, qPrintable(bend.error));
        QCOMPARE(column(bend, 0), (QStringList{str("Grace"), str("Ada")}));
        // Kept through the saved object's map.
        QCOMPARE(QueryDesign::fromMap(parsed.design.toMap()).parts.size(), 2);
        // A part that sorts by itself is refused (Access sorts a union at its end).
        QueryDesign sorted = parsed.design;
        sorted.parts[0].columns[0].sort = 1;
        QVERIFY(compileQuery(*db.file, sorted).error.contains(str("sort the union as a whole")));
    }

    void subqueriesInCriteria()
    {
        Gifts db;
        // Donors who gave more than $20.00 at once.
        const QueryResult big = db.runSql("SELECT id, name FROM donors WHERE id In (SELECT donor_id FROM donations "
                                          "WHERE amount > 20) ORDER BY id;");
        QVERIFY2(big.ok, qPrintable(big.error));
        QCOMPARE(column(big, 0), (QStringList{str("1"), str("2")}));
        // Gifts above the average ($17.998): $25.00 and $50.00.
        const QueryResult above = db.runSql("SELECT amount FROM donations WHERE amount > (SELECT Avg(amount) FROM "
                                            "donations) ORDER BY amount;");
        QVERIFY2(above.ok, qPrintable(above.error));
        QCOMPARE(column(above, 0), (QStringList{str("2500"), str("5000")}));
        const QueryResult exists = db.runSql("SELECT name, Exists (SELECT * FROM donations WHERE amount > 40) AS anyBig "
                                             "FROM donors;");
        QVERIFY2(exists.ok, qPrintable(exists.error));
        QCOMPARE(column(exists, 1), (QStringList{str("1"), str("1"), str("1"), str("1")}));
        // A subquery asks for nothing, and has no place in a rule.
        QVERIFY(db.runSql("SELECT name FROM donors WHERE id In (SELECT donor_id FROM donations WHERE amount > [Least]);")
                    .error.contains(str("cannot ask for values")));
        TableDef rule = readTable(*db.file, str("donations")).value();
        rule.fields[2].validationRule = str("< (SELECT Max(amount) FROM donations)");
        QVERIFY(validateRules(rule, db.file->currency()).contains(str("cannot be used here")));
    }

    void formatAndDatePart()
    {
        Gifts db;
        const QueryResult r = db.runSql(
            "SELECT Format(given, \"yyyy-mm\") AS ym, Format(given, \"mmm d, yyyy\") AS md, Format(given, \"dddd\") AS wd, "
            "DatePart(\"q\", given) AS q, DatePart(\"ww\", given) AS ww, DatePart(\"w\", given) AS w, "
            "Format(given, \"Short Date\") AS sd FROM donations WHERE id = 1;");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.rows.size(), 1);
        // 2026-01-15 is a Thursday in the third week (Sunday-first, week 1 holds January 1).
        QCOMPARE(r.rows.first(), (QVariantList{str("2026-01"), str("Jan 15, 2026"), str("Thursday"), qint64(1), qint64(3),
                                               qint64(5), str("2026-01-15")}));
        QVERIFY(db.runSql("SELECT Format(given, \"0.00\") FROM donations;").error.contains(str("date pattern")));
        QVERIFY(db.runSql("SELECT DatePart(\"x\", given) FROM donations;").error.contains(str("interval")));
    }
};

BASE_TEST_MAIN(BaseQueryM86Test)

#include "tst_base_query_m86.moc"
