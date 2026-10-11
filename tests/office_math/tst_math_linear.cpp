// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_latex.h"
#include "math_linear.h"
#include "math_props.h"
#include "math_symbols.h"

#include <QtTest>

using namespace QindaOffice::Math;
class MathLinearTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrips_data();
    void roundTrips();
    void expectedStructures();
    void autocorrectAndUnknownCommands();
    void quotedTextKeepsQuotesAndKeywords();
};

void MathLinearTest::roundTrips_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("latex");
    const QStringList linear = {
        QStringLiteral("a/b"), QStringLiteral("(a+b)/(c+d)"), QStringLiteral("x^2"),
        QStringLiteral("x_i"), QStringLiteral("x_i^2"), QStringLiteral("ab^2"),
        QStringLiteral("√x"), QStringLiteral("√(a+b)"), QStringLiteral("√(3&x)"),
        QStringLiteral("∑_(i=1)^n▒a_i"), QStringLiteral("∫_0^1▒x^2"), QStringLiteral("lim_(n→∞)"),
        QStringLiteral("sin\u2061x"), QStringLiteral("cos\u2061θ"), QStringLiteral("(x+y)"),
        QStringLiteral("[a/b]"), QStringLiteral("{x,y}"), QStringLiteral("|x|"),
        QStringLiteral("■(a&b@c&d)"), QStringLiteral("(■(1&0@0&1))"), QStringLiteral("█(x=1@y=2)"),
        QStringLiteral("a\u0302"), QStringLiteral("x\u0307"), QStringLiteral("v\u20d7"),
        QStringLiteral("¯(x+y)"), QStringLiteral("⏟(a+b)"), QStringLiteral("a¦b"),
        QStringLiteral("a∕b"), QStringLiteral("∏_(k=1)^n▒k"), QStringLiteral("∮▒F"),
        QStringLiteral("\"area\"=πr^2"), QStringLiteral("α+β=γ"), QStringLiteral("x≤y"),
        QStringLiteral("(a/b)/(c/d)"), QStringLiteral("x^(a+b)"), QStringLiteral("e^(iπ)+1=0")};
    const QStringList tex = {
        QStringLiteral(R"(\frac{a}{b})"), QStringLiteral(R"(\frac{a+b}{c+d})"), QStringLiteral(R"(x^{2})"),
        QStringLiteral(R"(x_{i})"), QStringLiteral(R"(x_{i}^{2})"), QStringLiteral(R"(ab^2)"),
        QStringLiteral(R"(\sqrt{x})"), QStringLiteral(R"(\sqrt{a+b})"), QStringLiteral(R"(\sqrt[3]{x})"),
        QStringLiteral(R"(\sum_{i=1}^{n}a_i)"), QStringLiteral(R"(\int_0^1 x^2)"), QStringLiteral(R"(\lim_{n\to\infty}x)"),
        QStringLiteral(R"(\sin{x})"), QStringLiteral(R"(\cos{\theta})"), QStringLiteral(R"(\left(x+y\right))"),
        QStringLiteral(R"(\left[\frac{a}{b}\right])"), QStringLiteral(R"(\left\{x,y\right\})"), QStringLiteral(R"(\left|x\right|)"),
        QStringLiteral(R"(\begin{matrix}a&b\\c&d\end{matrix})"),
        QStringLiteral(R"(\begin{pmatrix}1&0\\0&1\end{pmatrix})"),
        QStringLiteral(R"(\begin{aligned}x&=1\\y&=2\end{aligned})"),
        QStringLiteral(R"(\hat{a})"), QStringLiteral(R"(\dot{x})"), QStringLiteral(R"(\vec{v})"),
        QStringLiteral(R"(\overline{x+y})"), QStringLiteral(R"(\underbrace{a+b})"), QStringLiteral(R"(\binom{n}{k})"),
        QStringLiteral(R"(\frac{1}{\frac{a}{b}})"), QStringLiteral(R"(\prod_{k=1}^{n}k)"), QStringLiteral(R"(\oint F)"),
        QStringLiteral(R"(\text{area}=\pi r^2)"), QStringLiteral(R"(\alpha+\beta=\gamma)"), QStringLiteral(R"(x\le y)"),
        QStringLiteral(R"(\mathbb{R})"), QStringLiteral(R"(\mathbf{v})"), QStringLiteral(R"(e^{i\pi}+1=0)")};
    int number = 0;
    for (const auto &text : linear) {
        QTest::newRow(qPrintable(QStringLiteral("linear-%1").arg(++number))) << text << false;
    }
    number = 0;
    for (const auto &text : tex) {
        QTest::newRow(qPrintable(QStringLiteral("latex-%1").arg(++number))) << text << true;
    }
}

void MathLinearTest::roundTrips()
{
    QFETCH(QString, input);
    QFETCH(bool, latex);
    const auto first = latex ? fromLatex(input) : fromLinear(input);
    QVERIFY2(first.has_value(), qPrintable(input));
    const QString canonical = latex ? toLatex(*first) : toLinear(*first);
    QVERIFY2(!canonical.isEmpty(), qPrintable(input));
    const auto again = latex ? fromLatex(canonical) : fromLinear(canonical);
    QVERIFY(again.has_value());
    QCOMPARE(latex ? toLatex(*again) : toLinear(*again), canonical);
    // Both input forms also pass through the common accessible representation.
    const auto accessible = fromLinear(toLinear(*first));
    QVERIFY(accessible.has_value());
    QCOMPARE(toLinear(*accessible), toLinear(*first));
}

void MathLinearTest::expectedStructures()
{
    const auto fraction = fromLatex(QStringLiteral(R"(\frac{a+b}{c})"));
    QVERIFY(fraction.has_value());
    const auto &f = *fraction->lines[0].items[0];
    QCOMPARE(f.kind, NodeKind::Fraction);
    QCOMPARE(plainText(f.args[0]), QStringLiteral("a+b"));
    QCOMPARE(plainText(f.args[1]), QStringLiteral("c"));
    const auto scripts = fromLinear(QStringLiteral("x_i^2"));
    QVERIFY(scripts.has_value());
    const auto &s = *scripts->lines[0].items[0];
    QCOMPARE(s.kind, NodeKind::SubSup);
    QCOMPARE(plainText(s.args[0]), QStringLiteral("x"));
    QCOMPARE(plainText(s.args[1]), QStringLiteral("i"));
    QCOMPARE(plainText(s.args[2]), QStringLiteral("2"));
    const auto matrix = fromLinear(QStringLiteral("■(a&b@c&d)"));
    QVERIFY(matrix.has_value());
    const auto &m = *matrix->lines[0].items[0];
    QCOMPARE(m.kind, NodeKind::Matrix);
    QCOMPARE(m.rowSizes, (QList<int>{2, 2}));
    QCOMPARE(plainText(m.args[2]), QStringLiteral("c"));
}

void MathLinearTest::autocorrectAndUnknownCommands()
{
    QCOMPARE(applyAutoCorrect(QStringLiteral(R"(\alpha+\beta\le\infty)")), QStringLiteral("α+β≤∞"));
    const auto unknown = fromLatex(QStringLiteral(R"(\notimplemented{x})"));
    QVERIFY(unknown.has_value());
    QVERIFY(plainText(unknown->lines[0]).contains(QStringLiteral("notimplemented")));
}

void MathLinearTest::quotedTextKeepsQuotesAndKeywords()
{
    Equation equation = makeEquation(false);
    const QString text = QStringLiteral("The word \"alpha\" and \\alpha are literal.");
    auto run = makeRun(text);
    setRunNormalText(*run, true);
    equation.lines[0].items = {run};
    const auto again = fromLinear(toLinear(equation));
    QVERIFY(again.has_value());
    QCOMPARE(plainText(again->lines[0]), text);
    QVERIFY(runIsNormalText(*again->lines[0].items[0]));
}

QTEST_GUILESS_MAIN(MathLinearTest)
#include "tst_math_linear.moc"
