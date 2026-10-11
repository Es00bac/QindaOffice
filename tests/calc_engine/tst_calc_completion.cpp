// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097): the formula bar's completion (formula_completion.h) — what
// Excel's Formula AutoComplete offers for the name at the cursor: functions
// with Excel's argument names, the workbook's and the sheet's defined
// names, tables, a table's columns and special items inside a structured
// reference ("Sales[Am", "[@Am", "[@[Unit P"), nothing inside a string, a
// quoted sheet name or after a sheet qualifier.

#include "formula_completion.h"
#include "functions/functions.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

CellAddr at(qint32 row, qint32 col)
{
    return {row, col};
}

DefinedName named(const QString &name, qint32 sheet, const QString &formula)
{
    DefinedName out;
    out.name = name;
    out.sheet = sheet;
    out.formula = formula;
    return out;
}

// Sales on Sheet1!A1:D4 (Region | Amount | Unit Price | Cost) and two names.
Workbook book()
{
    Workbook b;
    b.addSheet(QStringLiteral("Sheet1"));
    b.addSheet(QStringLiteral("Report"));
    const QStringList headers{QStringLiteral("Region"), QStringLiteral("Amount"), QStringLiteral("Unit Price"),
                              QStringLiteral("Cost")};
    for (qint32 c = 0; c < 4; ++c) {
        b.setCellValue(0, at(0, c), headers.at(c));
    }
    TableSpec table;
    table.name = QStringLiteral("Sales");
    table.range = {at(0, 0), at(3, 3)};
    table.columnNames = headers;
    b.sheetAt(0).setTables({table});
    const DefinedName rate = named(QStringLiteral("SalesTax"), -1, QStringLiteral("Sheet1!$F$1"));
    const DefinedName local = named(QStringLiteral("SalesGoal"), 1, QStringLiteral("Report!$B$2"));
    DefinedName hidden = named(QStringLiteral("SalesHidden"), -1, QStringLiteral("1"));
    hidden.hidden = true;
    b.setDefinedNames({rate, local, hidden});
    return b;
}

QStringList labels(const CompletionContext &c)
{
    QStringList out;
    for (const Completion &e : c.entries) {
        out.append(e.label);
    }
    return out;
}

const Completion *entry(const CompletionContext &c, const QString &label)
{
    for (const Completion &e : c.entries) {
        if (e.label == label) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

class CalcCompletionTest final : public QObject {
    Q_OBJECT
private slots:
    void functionsWithTheirArguments()
    {
        const Workbook b = book();
        const QString text = QStringLiteral("=1+su");
        const CompletionContext c = formulaCompletions(b, 0, at(5, 0), text, int(text.size()));
        QCOMPARE(c.from, 3);
        QCOMPARE(c.to, 5);
        QVERIFY(labels(c).contains(QStringLiteral("SUM")));
        QVERIFY(labels(c).contains(QStringLiteral("SUMIFS")));
        QVERIFY(labels(c).contains(QStringLiteral("SUBSTITUTE")));
        const Completion *sum = entry(c, QStringLiteral("SUM"));
        QCOMPARE(sum->kind, QStringLiteral("function"));
        QCOMPARE(sum->detail, QStringLiteral("SUM(number1, [number2], …)"));
        QCOMPARE(sum->insert, QStringLiteral("SUM("));
        QCOMPARE(functionSignature(QStringLiteral("vlookup")),
                 QStringLiteral("VLOOKUP(lookup_value, table_array, col_index_num, [range_lookup])"));
        // Every function the engine has can show its arguments.
        for (auto it = functionRegistry().cbegin(); it != functionRegistry().cend(); ++it) {
            QVERIFY2(functionSignature(it.key()).startsWith(it.key() + QLatin1Char('(')), qPrintable(it.key()));
        }
        // Alphabetical, mixed with names and tables (Excel's one list).
        const QStringList all = labels(formulaCompletions(b, 1, at(0, 0), QStringLiteral("=sa"), 3));
        QCOMPARE(all, (QStringList{QStringLiteral("Sales"), QStringLiteral("SalesGoal"), QStringLiteral("SalesTax")}));
    }

    void namesAndTables()
    {
        const Workbook b = book();
        const CompletionContext onSheet1 = formulaCompletions(b, 0, at(5, 0), QStringLiteral("=Sales"), 6);
        // The workbook's name, not Report's own, never the hidden one.
        QCOMPARE(labels(onSheet1), (QStringList{QStringLiteral("Sales"), QStringLiteral("SalesTax")}));
        QCOMPARE(entry(onSheet1, QStringLiteral("SalesTax"))->detail, QStringLiteral("=Sheet1!$F$1"));
        QCOMPARE(entry(onSheet1, QStringLiteral("SalesTax"))->insert, QStringLiteral("SalesTax"));
        const Completion *table = entry(onSheet1, QStringLiteral("Sales"));
        QCOMPARE(table->kind, QStringLiteral("table"));
        QCOMPARE(table->detail, QStringLiteral("Table · Sheet1!A1:D4"));
        QVERIFY(labels(formulaCompletions(b, 1, at(0, 0), QStringLiteral("=Sales"), 6)).contains(QStringLiteral("SalesGoal")));
    }

    void columnsInsideStructuredReferences()
    {
        const Workbook b = book();
        QString text = QStringLiteral("=SUM(Sales[Am");
        CompletionContext c = formulaCompletions(b, 1, at(0, 0), text, int(text.size()));
        QCOMPARE(labels(c), QStringList{QStringLiteral("Amount")});
        QCOMPARE(c.from, 11);
        QCOMPARE(entry(c, QStringLiteral("Amount"))->insert, QStringLiteral("Amount]"));
        // An empty bracket lists the special items and every column.
        text = QStringLiteral("=Sales[");
        c = formulaCompletions(b, 1, at(0, 0), text, int(text.size()));
        QVERIFY(labels(c).contains(QStringLiteral("#All")));
        QVERIFY(labels(c).contains(QStringLiteral("#This Row")));
        QVERIFY(labels(c).contains(QStringLiteral("Cost")));
        QCOMPARE(entry(c, QStringLiteral("#Totals"))->insert, QStringLiteral("#Totals]"));
        // "[@" inside the table: the cell's own table, this row's value; a
        // name with a space needs its own brackets.
        text = QStringLiteral("=[@Un");
        c = formulaCompletions(b, 0, at(2, 3), text, int(text.size()));
        QCOMPARE(c.from, 3);
        QCOMPARE(entry(c, QStringLiteral("Unit Price"))->insert, QStringLiteral("[Unit Price]]"));
        text = QStringLiteral("=[@[Un");
        c = formulaCompletions(b, 0, at(2, 3), text, int(text.size()));
        QCOMPARE(entry(c, QStringLiteral("Unit Price"))->insert, QStringLiteral("Unit Price]]"));
        text = QStringLiteral("=Sales[[#This Row],[Co");
        c = formulaCompletions(b, 1, at(0, 0), text, int(text.size()));
        QCOMPARE(entry(c, QStringLiteral("Cost"))->insert, QStringLiteral("Cost]]"));
        // Outside every table a bare bracket has no columns.
        text = QStringLiteral("=[@Un");
        QVERIFY(formulaCompletions(b, 1, at(9, 9), text, int(text.size())).entries.isEmpty());
    }

    void nothingWhereNoNameIsTyped()
    {
        const Workbook b = book();
        const auto none = [&b](const QString &text) {
            return formulaCompletions(b, 0, at(5, 0), text, int(text.size())).entries.isEmpty();
        };
        QVERIFY(none(QStringLiteral("su")));               // not a formula
        QVERIFY(none(QStringLiteral("=\"su")));            // in a string
        QVERIFY(none(QStringLiteral("='Sales su")));       // in a quoted sheet name
        QVERIFY(none(QStringLiteral("=Report!su")));       // after a sheet qualifier
        QVERIFY(none(QStringLiteral("=1+")));              // no name yet
        QVERIFY(none(QStringLiteral("=1+2")));             // a number
        QVERIFY(!none(QStringLiteral("=\"a\"&le")));       // after a closed string
        // The cursor, not the end, decides.
        const CompletionContext mid = formulaCompletions(b, 0, at(5, 0), QStringLiteral("=SU+1"), 3);
        QCOMPARE(mid.to, 3);
        QVERIFY(labels(mid).contains(QStringLiteral("SUM")));
    }
};

QTEST_GUILESS_MAIN(CalcCompletionTest)
#include "tst_calc_completion.moc"
