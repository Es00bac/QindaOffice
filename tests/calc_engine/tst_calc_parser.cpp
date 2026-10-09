// SPDX-License-Identifier: GPL-3.0-or-later
// Lexer/parser behavior: token classes, precedence, the Excel -2^2 = 4 rule,
// reference forms, sheet qualifiers, error literals, and AST round-trips
// (plan section 7.2).

#include "lexer.h"
#include "parser.h"

#include <QCoreApplication>
#include <QtTest>

using namespace QindaOffice::Calc;

class CalcParserTest : public QObject {
    Q_OBJECT
private slots:
    void lexing();
    void precedence();
    void roundTrip_data();
    void roundTrip();
    void errorLiterals();
    void badInput();
    void shift();
    void printingKeepsGrouping_data();
    void printingKeepsGrouping();
    void absoluteWholeColumnsAndRows();
    void sheetNamesQuoteWhenTheyMust();
};

static double evalSimple(const QString &expr, bool *ok)
{
    ParseResult parsed = parseFormula(expr);
    if (!parsed.error.isEmpty() || !parsed.root) {
        *ok = false;
        return 0;
    }
    // Constant-fold through the public AST: use a tiny constant evaluator via
    // number/text/bool nodes only (no host needed for these cases).
    *ok = true;
    return 0; // evaluated in fixture tests; here we only parse
}

void CalcParserTest::lexing()
{
    QVector<Token> tokens = lexFormula(QStringLiteral("1.5e2+SUM(A1:$B$3,'S 2'!C4)"));
    QCOMPARE(tokens.at(0).type, Token::Type::Number);
    QCOMPARE(tokens.at(1).text, QStringLiteral("+"));
    QCOMPARE(tokens.at(2).type, Token::Type::Identifier);
    QCOMPARE(tokens.at(3).text, QStringLiteral("("));
    QCOMPARE(tokens.at(4).type, Token::Type::Ref); // A1:$B$3 (one range token)
    QCOMPARE(tokens.at(5).text, QStringLiteral(","));
    QCOMPARE(tokens.at(6).type, Token::Type::QuotedSheetName);
    QCOMPARE(tokens.at(7).type, Token::Type::Ref); // C4
    QCOMPARE(tokens.last().type, Token::Type::End);

    // Whole-column and whole-row forms lex as single refs.
    QCOMPARE(lexFormula(QStringLiteral("A:A")).at(0).type, Token::Type::Ref);
    QCOMPARE(lexFormula(QStringLiteral("1:3")).at(0).type, Token::Type::Ref);
    // Identifiers vs refs.
    QCOMPARE(lexFormula(QStringLiteral("ABC1")).at(0).type, Token::Type::Ref);
    QCOMPARE(lexFormula(QStringLiteral("ABCD1")).at(0).type, Token::Type::Identifier);
    // String escapes and doubled quotes.
    QVector<Token> str = lexFormula(QStringLiteral("\"a\"\"b\""));
    QCOMPARE(str.at(0).type, Token::Type::String);
    QCOMPARE(str.at(0).text, QStringLiteral("a\"b"));
}

void CalcParserTest::precedence()
{
    // ':' binds tighter than arithmetic: SUM(A1:B2)*2 shape.
    ParseResult range = parseFormula(QStringLiteral("A1:B2*2"));
    QVERIFY(range.error.isEmpty());
    QCOMPARE(range.root->kind, Node::Kind::Binary); // (A1:B2) * 2
    QCOMPARE(range.root->op, Op::Mul);

    // Excel rule: -2^2 = 4 (unary binds tighter than ^).
    ParseResult unary = parseFormula(QStringLiteral("-2^2"));
    QVERIFY(unary.error.isEmpty());
    QCOMPARE(unary.root->op, Op::Pow);
    QCOMPARE(unary.root->left->kind, Node::Kind::Unary);

    // Postfix % binds tighter than ^: 50%^2 = (0.5)^2.
    ParseResult pct = parseFormula(QStringLiteral("50%^2"));
    QVERIFY(pct.error.isEmpty());
    QCOMPARE(pct.root->op, Op::Pow);
    QCOMPARE(pct.root->left->op, Op::Percent);

    // Comparisons are loosest: 1+2>2 = (1+2)>2.
    ParseResult cmp = parseFormula(QStringLiteral("1+2>2"));
    QVERIFY(cmp.error.isEmpty());
    QCOMPARE(cmp.root->op, Op::Gt);
}

void CalcParserTest::roundTrip_data()
{
    QTest::addColumn<QString>("expr");
    const char *kExprs[] = {
        "1+2*3", "(1+2)*3", "A1", "$A$1", "$A1", "A$1", "A1:B3", "A:A", "3:7",
        "'My Sheet'!A1", "Sheet1!A1:B2", "-1+2", "1E3+2.5", "\"a\"\"b\"&\"c\"",
        "SUM(A1:A5,2,\"x\")", "IF(TRUE,1,2)", "A1<=B1", "A1<>B1", "2^3%",
        "TRUE+FALSE", "#N/A", "1:3", "MAX(1,2,3)",
    };
    for (const char *expr : kExprs) {
        QTest::newRow(expr) << QString::fromLatin1(expr);
    }
}

void CalcParserTest::roundTrip()
{
    QFETCH(QString, expr);
    ParseResult parsed = parseFormula(expr);
    QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
    const QString out = nodeText(parsed.root);
    ParseResult reparsed = parseFormula(out);
    QVERIFY2(reparsed.error.isEmpty(), qPrintable(out));
    QCOMPARE(nodeText(reparsed.root), out);
}

void CalcParserTest::errorLiterals()
{
    for (const char *lit : {"#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A"}) {
        ParseResult parsed = parseFormula(QString::fromLatin1(lit));
        QVERIFY(parsed.error.isEmpty());
        QCOMPARE(parsed.root->kind, Node::Kind::Error);
        QCOMPARE(nodeText(parsed.root), QString::fromLatin1(lit));
    }
    // Array constants parse since M69 (D-078; they were #NAME? before).
    ParseResult array = parseFormula(QStringLiteral("{1,2;3,4}"));
    QVERIFY(array.error.isEmpty());
    QCOMPARE(array.root->kind, Node::Kind::Array);
    QCOMPARE(array.root->array->rows, 2);
    QCOMPARE(array.root->array->cols, 2);
}

void CalcParserTest::badInput()
{
    QVERIFY(!parseFormula(QStringLiteral("1+")).error.isEmpty());
    QVERIFY(!parseFormula(QStringLiteral("SUM(1,2")).error.isEmpty());
    QVERIFY(!parseFormula(QStringLiteral("1 2")).error.isEmpty());
    // Evaluated empty formula is a blank text node.
    ParseResult empty = parseFormula(QString());
    QVERIFY(empty.error.isEmpty());
    QCOMPARE(empty.root->kind, Node::Kind::Text);
}

void CalcParserTest::shift()
{
    ParseResult parsed = parseFormula(QStringLiteral("A1+$B$2+B$3"));
    const NodePtr shifted = shiftNode(parsed.root, 2, 5); // down 2, right 5
    QCOMPARE(nodeText(shifted), QStringLiteral("F3+$B$2+G$3"));
}

// nodeText is how a tree becomes text again — shared-formula dependents,
// structural edits and sheet renames all store what it prints, and the xlsx
// writer saves that. The parser folds parentheses into the tree, so the
// printer must put back every pair the grouping needs: "(A1+B1)*2" printed
// as "A1+B1*2" is a different formula (M61, D-069).
void CalcParserTest::printingKeepsGrouping_data()
{
    QTest::addColumn<QString>("expr");
    QTest::addColumn<QString>("printed");
    const char *kRows[][2] = {
        {"(1+2)*3", "(1+2)*3"},
        {"1+2*3", "1+2*3"},
        {"(1+2)+3", "1+2+3"},
        {"1-(2-3)", "1-(2-3)"},
        {"1-(2+3)", "1-(2+3)"},
        {"(1-2)-3", "1-2-3"},
        {"8/(4/2)", "8/(4/2)"},
        {"8/(4*2)", "8/(4*2)"},
        {"2^(1+1)", "2^(1+1)"},
        {"(2^3)^2", "2^3^2"},
        {"2^(3^2)", "2^(3^2)"},
        {"-(1+2)", "-(1+2)"},
        {"-(2^2)", "-(2^2)"},
        {"(-2)^2", "-2^2"},
        {"(1+2)%", "(1+2)%"},
        {"(-5)%", "(-5)%"},
        {"-5%", "-5%"},
        {"(\"a\"&\"b\")&\"c\"", "\"a\"&\"b\"&\"c\""},
        {"\"a\"&(\"b\"&\"c\")", "\"a\"&(\"b\"&\"c\")"},
        {"(1<2)=TRUE", "1<2=TRUE"},
        {"1<(2=TRUE)", "1<(2=TRUE)"},
        {"1+(2>1)", "1+(2>1)"},
        {"SUM((A1,B1))", "SUM((A1,B1))"},
        {"2^-(1-2)", "2^-(1-2)"},
        {"A1-(B1-A1)/(B1*2)^-(A1-2)", "A1-(B1-A1)/(B1*2)^-(A1-2)"},
    };
    for (const auto &row : kRows) {
        QTest::newRow(row[0]) << QString::fromLatin1(row[0]) << QString::fromLatin1(row[1]);
    }
}

void CalcParserTest::printingKeepsGrouping()
{
    QFETCH(QString, expr);
    QFETCH(QString, printed);
    const ParseResult parsed = parseFormula(expr);
    QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
    QCOMPARE(nodeText(parsed.root), printed);
    // And the printed text parses back to the very same tree.
    const ParseResult again = parseFormula(printed);
    QVERIFY2(again.error.isEmpty(), qPrintable(again.error));
    QCOMPARE(nodeText(again.root), printed);
}

// $A:$C and $1:$3 are how Excel spells absolute whole columns and rows —
// VLOOKUP(x,$A:$C,2,FALSE) is everyday Excel. They lex as one reference,
// keep their anchors in print, and a shifted copy keeps whole-column rows
// (and whole-row columns) where they are.
void CalcParserTest::absoluteWholeColumnsAndRows()
{
    for (const char *ref : {"$A:$C", "A:$C", "$A:C", "$1:$3", "1:$3", "$1:3"}) {
        const QVector<Token> tokens = lexFormula(QString::fromLatin1(ref));
        QCOMPARE(tokens.size(), 2);
        QCOMPARE(tokens.at(0).type, Token::Type::Ref);
        const ParseResult parsed = parseFormula(QString::fromLatin1(ref));
        QVERIFY2(parsed.error.isEmpty() && parsed.root->kind == Node::Kind::Ref, ref);
        QCOMPARE(nodeText(parsed.root), QString::fromLatin1(ref));
    }
    const ParseResult cols = parseFormula(QStringLiteral("SUM($A:B)"));
    QVERIFY(cols.error.isEmpty());
    const NodePtr shiftedCols = shiftNode(cols.root, 5, 1);
    QCOMPARE(nodeText(shiftedCols), QStringLiteral("SUM($A:C)"));
    QCOMPARE(shiftedCols->args.at(0)->ref.range.topLeft.row, 0);
    QCOMPARE(shiftedCols->args.at(0)->ref.range.bottomRight.row, 1048575);
    const ParseResult rows = parseFormula(QStringLiteral("SUM(2:$4)"));
    QVERIFY(rows.error.isEmpty());
    const NodePtr shiftedRows = shiftNode(rows.root, 1, 3);
    QCOMPARE(nodeText(shiftedRows), QStringLiteral("SUM(3:$4)"));
    QCOMPARE(shiftedRows->args.at(0)->ref.range.topLeft.col, 0);
    QCOMPARE(shiftedRows->args.at(0)->ref.range.bottomRight.col, 16383);
}

// A sheet name that is not a plain identifier is quoted when a reference is
// printed: "2024", "Q1 Sales", "It's" (doubled quote), a name that reads as
// a cell address. A name the parser kept quoted stays as it was.
void CalcParserTest::sheetNamesQuoteWhenTheyMust()
{
    RangeRef ref;
    ref.range = {{0, 0}, {0, 0}};
    const auto printed = [&ref](const QString &sheet) {
        ref.sheet = sheet;
        return rangeRefText(ref);
    };
    QCOMPARE(printed(QStringLiteral("Data")), QStringLiteral("Data!A1"));
    QCOMPARE(printed(QStringLiteral("Data_2")), QStringLiteral("Data_2!A1"));
    QCOMPARE(printed(QStringLiteral("Data.v1")), QStringLiteral("'Data.v1'!A1"));
    QCOMPARE(printed(QStringLiteral("2024")), QStringLiteral("'2024'!A1"));
    QCOMPARE(printed(QStringLiteral("Q1 Sales")), QStringLiteral("'Q1 Sales'!A1"));
    QCOMPARE(printed(QStringLiteral("It's")), QStringLiteral("'It''s'!A1"));
    QCOMPARE(printed(QStringLiteral("AB12")), QStringLiteral("'AB12'!A1"));
    QCOMPARE(printed(QStringLiteral("R1C1")), QStringLiteral("'R1C1'!A1"));
    QCOMPARE(printed(QStringLiteral("Q-1")), QStringLiteral("'Q-1'!A1"));
    QCOMPARE(printed(QStringLiteral("'My Sheet'")), QStringLiteral("'My Sheet'!A1"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcParserTest test;
    Q_UNUSED(&test);
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_parser.moc"
