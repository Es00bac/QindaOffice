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
    // Array constants are #NAME? in v1 (plan section 7.2).
    ParseResult array = parseFormula(QStringLiteral("{1,2;3,4}"));
    QVERIFY(array.error.isEmpty());
    QCOMPARE(array.root->kind, Node::Kind::Error);
    QCOMPARE(array.root->error, Err::Name);
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

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcParserTest test;
    Q_UNUSED(&test);
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_parser.moc"
