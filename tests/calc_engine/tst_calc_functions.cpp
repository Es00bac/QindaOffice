// SPDX-License-Identifier: GPL-3.0-or-later
// Fixture-driven function suite (plan section 7.2): every row of
// tests/fixtures/functions/functions.tsv (and functions_m61.tsv, M61) is an
// expression whose expected value was produced by tools/lo_oracle.py
// through headless LibreOffice.
// Error classes are compared after normalization (LibreOffice exports some
// errors as Err:5xx in CSV form).

#include "eval.h"
#include "functions/functions.h"
#include "parser.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QFile>
#include <QtTest>

#include <limits>

using namespace QindaOffice::Calc;

class CalcFunctionsTest : public QObject {
    Q_OBJECT
private slots:
    void fixtureRows();
    void arrayArithmeticBroadcasts();
    void excelRulesBeyondTheOracle();
};

static Value parseInput(const QString &input)
{
    if (input == QLatin1String("TRUE")) return true;
    if (input == QLatin1String("FALSE")) return false;
    if (input.isEmpty()) {
        return Value(std::monostate{}); // an empty fixture cell stays blank
    }
    bool ok = false;
    const double d = input.toDouble(&ok);
    if (ok) return d;
    return input;
}

// Shared verification data - must stay identical to DATA in tools/lo_oracle.py.
static void buildDataBlock(Workbook &wb)
{
    Sheet &s = wb.addSheet(QStringLiteral("Sheet1"));
    struct Lit {
        const char *addr;
        const char *value;
    };
    static const Lit kLit[] = {
        {"A1", "1"},  {"A2", "2"},  {"A3", "3"},  {"A4", "4"},  {"A5", "5"},
        {"B1", "10"}, {"B2", "20"}, {"B3", "30"}, {"B4", "40"}, {"B5", "50"},
        {"C1", "apple"}, {"C2", "Apple"}, {"C3", "banana"}, {"C4", ""}, {"C5", "10"},
        {"D1", "TRUE"}, {"D2", "FALSE"}, {"D3", "TRUE"}, {"D4", "FALSE"}, {"D5", "FALSE"},
        {"G1", "aa"}, {"G2", "bb"}, {"G3", "cc"}, {"G4", "dd"}, {"G5", "ee"},
        {"H1", "100"}, {"H2", "200"}, {"H3", "300"}, {"H4", "400"}, {"H5", "500"},
        {"I1", "k1"}, {"J1", "k2"}, {"K1", "v1"}, {"L1", "v2"},
        {"I2", "a"}, {"J2", "b"}, {"K2", "1"}, {"L2", "2"},
        {"I3", "c"}, {"J3", "d"}, {"K3", "3"}, {"L3", "4"},
        {"I4", "e"}, {"J4", "f"}, {"K4", "5"}, {"L4", "6"},
        // M61: dated cash flows (Microsoft's XNPV/XIRR example), MIRR
        // flows, and columns with ties for MODE/RANK.AVG/XMATCH.
        {"M1", "-10000"}, {"M2", "2750"}, {"M3", "4250"}, {"M4", "3250"}, {"M5", "2750"},
        {"N1", "39448"}, {"N2", "39508"}, {"N3", "39751"}, {"N4", "39859"}, {"N5", "39904"},
        {"O1", "-120000"}, {"O2", "39000"}, {"O3", "30000"}, {"O4", "21000"}, {"O5", "37000"},
        {"O6", "46000"},
        {"P1", "7"}, {"P2", "3"}, {"P3", "7"}, {"P4", "1"}, {"P5", "7"},
        {"Q1", "2"}, {"Q2", "2"}, {"Q3", "5"}, {"Q4", "5"}, {"Q5", "9"},
    };
    for (const Lit &lit : kLit) {
        const QString text = QString::fromLatin1(lit.addr);
        int i = 0;
        while (!text.at(i).isDigit()) {
            ++i;
        }
        const auto col = colFromName(text.left(i));
        const CellAddr addr{text.mid(i).toInt() - 1, col.value_or(0)};
        const Value parsed = parseInput(QString::fromLatin1(lit.value));
        if (isBlank(parsed)) {
            continue; // an empty fixture cell stays truly blank (mirrors CSV import)
        }
        wb.setCellValue(0, addr, parsed);
    }
}

static QString normalizeError(QString text)
{
    // LibreOffice exports some error cells as Err:5xx codes in CSV form;
    // map them to the shared error class (Err:509 = missing object -> #REF!).
    text = text.trimmed();
    if (text == QLatin1String("#NUM!") || text == QLatin1String("Err:523")
        || text == QLatin1String("Err:502")) return QStringLiteral("num");
    if (text == QLatin1String("#REF!") || text == QLatin1String("Err:509")) return QStringLiteral("ref");
    if (text == QLatin1String("#NAME?")) return QStringLiteral("name");
    if (text == QLatin1String("#VALUE!") || text == QLatin1String("Err:504")) {
        return QStringLiteral("value");
    }
    if (text == QLatin1String("#N/A")) return QStringLiteral("na");
    if (text == QLatin1String("#DIV/0!")) return QStringLiteral("div0");
    return QString();
}

static bool valueMatches(const Value &got, const QString &expectedRaw)
{
    const QString expected = expectedRaw.trimmed();
    const QString errClass = normalizeError(expected);
    if (!errClass.isEmpty()) {
        if (!isErr(got)) {
            return false;
        }
        return normalizeError(errText(std::get<Err>(got))) == errClass;
    }
    if (expected.isEmpty()) {
        return isBlank(got);
    }
    if (const auto *b = std::get_if<bool>(&got)) {
        // LibreOffice exports booleans as 1/0 through the CSV oracle.
        return expected.compare(*b ? QStringLiteral("TRUE") : QStringLiteral("FALSE"),
                                Qt::CaseInsensitive) == 0
            || expected == (*b ? QStringLiteral("1") : QStringLiteral("0"));
    }
    bool numOk = false;
    const double want = expected.toDouble(&numOk);
    if (numOk && isNumber(got)) {
        const double have = std::get<double>(got);
        return std::abs(have - want) <= qMax(1e-6, std::abs(want) * 1e-9);
    }
    return toText(got) == expected;
}

void CalcFunctionsTest::fixtureRows()
{
    Workbook wb;
    buildDataBlock(wb);
    Sheet &scratch = wb.addSheet(QStringLiteral("Scratch"));
    Q_UNUSED(scratch);

    // functions_m61.tsv holds the rows added with M61 (D-069), produced the
    // same way (tools/lo_oracle.py exprs_m61.txt functions_m61.tsv).
    int row = 0;
    int failed = 0;
    for (const char *fixture : {SOURCE_DIR "/../fixtures/functions/functions.tsv",
                                SOURCE_DIR "/../fixtures/functions/functions_m61.tsv"}) {
        QFile file(QString::fromLatin1(fixture));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.fileName()));
        while (!file.atEnd()) {
            const QByteArray rawLine = file.readLine();
            const QByteArray line = rawLine.endsWith('\n') ? rawLine.left(rawLine.size() - 1) : rawLine;
            if (line.isEmpty() || line.startsWith("# ")) {
                continue;
            }
            const int tab = line.indexOf('\t');
            // A row without a tab is an expression whose oracle result was an
            // empty cell (e.g. T(5)); its expectation is blank.
            const QString expr = QString::fromUtf8(tab > 0 ? line.left(tab) : line);
            const QString expected = tab > 0 ? QString::fromUtf8(line.mid(tab + 1)) : QString();
            ParseResult parsed = parseFormula(expr);
            if (!parsed.error.isEmpty() || !parsed.root) {
                QFAIL(qPrintable(QStringLiteral("row %1: parse failed for %2: %3")
                                     .arg(row).arg(expr, parsed.error)));
            }
            EvalContext ctx{&wb, 0, {}};
            const Value got = evalNode(parsed.root, ctx);
            if (!valueMatches(got, expected)) {
                ++failed;
                if (failed < 25) {
                    qWarning("row %d: %s\n  expected: %s\n  got:      %s", row,
                             qPrintable(expr), qPrintable(expected), qPrintable(toText(got)));
                }
            }
            ++row;
        }
    }
    QVERIFY2(row >= 300, qPrintable(QStringLiteral("fixture must carry >=300 rows, has %1").arg(row)));
    QCOMPARE(failed, 0);
}

// Array arithmetic (M38): an operator over a range or an array evaluates
// element-wise in argument position. The shapes here are the ones the
// Northstar sample workbook uses — SUMPRODUCT of a range against a powered
// range — plus the broadcast and shape rules that make them work.
void CalcFunctionsTest::arrayArithmeticBroadcasts()
{
    Workbook wb;
    buildDataBlock(wb); // A1:A5 = 1..5, B1:B5 = 10..50
    EvalContext ctx{&wb, 0, {}, {}};
    const auto eval = [&ctx](const char *expr) {
        ParseResult parsed = parseFormula(QString::fromLatin1(expr));
        if (!parsed.error.isEmpty() || !parsed.root) {
            return Value(Err::Name);
        }
        return evalNode(parsed.root, ctx);
    };
    const auto number = [&eval](const char *expr) {
        const Value v = eval(expr);
        return isNumber(v) ? std::get<double>(v) : std::numeric_limits<double>::quiet_NaN();
    };

    // The sample's own shape: SUMPRODUCT(values, (1+rate)^periods). Every
    // one of these was #VALUE! before the evaluator could carry an array.
    QVERIFY(qFuzzyCompare(number("SUMPRODUCT(A1:A5,(1+0.1)^A1:A5)"),
                          1 * 1.1 + 2 * 1.21 + 3 * 1.331 + 4 * 1.4641 + 5 * 1.61051));
    QVERIFY(qFuzzyCompare(number("SUMPRODUCT(A1:A5,B1:B5*2)"), 1100.0));

    // Every operator broadcasts: scalar against range, range against range.
    QCOMPARE(number("SUM(A1:A5*2)"), 30.0);
    QCOMPARE(number("SUM(A1:A5+B1:B5)"), 165.0);
    QCOMPARE(number("SUM(B1:B5/A1:A5)"), 50.0);
    QCOMPARE(number("SUM(A1:A5*B1:B5)"), 550.0);
    QCOMPARE(number("MAX(A1:A5*B1:B5)"), 250.0);
    QCOMPARE(number("COUNT(A1:A5*2)"), 5.0);
    // Comparisons yield booleans, and the --(…) idiom coerces them to 1/0.
    QCOMPARE(number("SUMPRODUCT(--(A1:A5>2))"), 3.0);
    QCOMPARE(number("SUM(-A1:A5)"), -15.0);
    QCOMPARE(eval("TEXTJOIN(\",\",TRUE,A1:A5*2)"), Value(QStringLiteral("2,4,6,8,10")));

    // A single row and a single column stretch along their degenerate axis
    // the way Excel's outer product does: 3x1 against 1x2 is 3x2.
    QCOMPARE(number("SUM(A1:A3*A1:B1)"), 66.0);

    // Shapes that cannot line up, and arrays outside argument position.
    QCOMPARE(eval("SUM(A1:A5*B1:B3)"), Value(Err::Value));
    QCOMPARE(eval("A1:A5*2"), Value(Err::Value));  // no spilling: #VALUE!
    QCOMPARE(number("A1:A1*2"), 2.0);              // …but 1x1 derefs
    // An error element is the whole aggregate's answer.
    QCOMPARE(eval("SUM(A1:A5*(1/0))"), Value(Err::Div0));
}

// M61 (D-069): answers the LibreOffice oracle cannot give. LibreOffice
// 25.2 has no TEXTBEFORE/TEXTAFTER, and for a few error kinds and the MODE
// tie-break it departs from what Microsoft documents for Excel — the
// behaviour a workbook written by Excel expects. Each expectation below is
// Excel's documented rule, worked by hand.
void CalcFunctionsTest::excelRulesBeyondTheOracle()
{
    Workbook wb;
    buildDataBlock(wb);
    wb.addSheet(QStringLiteral("Scratch"));
    wb.setCellInput(1, {0, 0}, QStringLiteral("=1+1"));
    EvalContext ctx{&wb, 0, {}, {}};
    const auto eval = [&ctx](const char *expr) {
        const ParseResult parsed = parseFormula(QString::fromUtf8(expr));
        return parsed.root ? evalNode(parsed.root, ctx) : Value(Err::Name);
    };
    const auto text = [](const char *s) { return Value(QString::fromUtf8(s)); };

    // MODE: on a tie, the value met first in the data (LibreOffice: the
    // smallest); no repeated value at all is #N/A.
    QCOMPARE(eval("MODE(3,3,2,2,1)"), Value(3.0));
    QCOMPARE(eval("MODE.SNGL(5,5,1,1)"), Value(5.0));
    QCOMPARE(eval("MODE.SNGL(A1:A5)"), Value(Err::NA));
    // Out-of-range arguments are #NUM!, mismatched arrays #N/A.
    QCOMPARE(eval("PERCENTILE.EXC(A1:A5,0.1)"), Value(Err::Num));
    QCOMPARE(eval("COVARIANCE.P(A1:A5,B1:B4)"), Value(Err::NA));
    QCOMPARE(eval("NORM.INV(0,40,1.5)"), Value(Err::Num));
    QCOMPARE(eval("NETWORKDAYS.INTL(DATE(2024,1,1),DATE(2024,1,31),99)"), Value(Err::Num));
    QCOMPARE(eval("WORKDAY.INTL(DATE(2024,1,1),3,\"1111111\")"), Value(Err::Value));

    // TEXTBEFORE / TEXTAFTER.
    QCOMPARE(eval("TEXTBEFORE(\"Red riding hood\",\"riding\")"), text("Red "));
    QCOMPARE(eval("TEXTAFTER(\"Red riding hood\",\"riding\")"), text(" hood"));
    QCOMPARE(eval("TEXTBEFORE(\"a-b-c\",\"-\",2)"), text("a-b"));
    QCOMPARE(eval("TEXTBEFORE(\"a-b-c\",\"-\",-1)"), text("a-b"));
    QCOMPARE(eval("TEXTAFTER(\"a-b-c\",\"-\",-2)"), text("b-c"));
    QCOMPARE(eval("TEXTAFTER(\"a-b-c\",\"-\")"), text("b-c"));
    QCOMPARE(eval("TEXTBEFORE(\"a-b-c\",\"-\",3)"), Value(Err::NA));
    QCOMPARE(eval("TEXTBEFORE(\"a-b-c\",\"-\",3,0,1)"), text("a-b-c"));
    QCOMPARE(eval("TEXTAFTER(\"a-b-c\",\"-\",3,0,1)"), text(""));
    QCOMPARE(eval("TEXTAFTER(\"a-b-c\",\"-\",-3,0,1)"), text("a-b-c"));
    QCOMPARE(eval("TEXTBEFORE(\"aXbxc\",\"x\",1,1)"), text("a"));
    QCOMPARE(eval("TEXTBEFORE(\"aXbxc\",\"x\",1,0)"), text("aXb"));
    QCOMPARE(eval("TEXTBEFORE(\"abc\",\"\")"), text(""));
    QCOMPARE(eval("TEXTAFTER(\"abc\",\"\")"), text("abc"));
    QCOMPARE(eval("TEXTBEFORE(\"abc\",\"\",-1)"), text("abc"));
    QCOMPARE(eval("TEXTBEFORE(\"abc\",\"z\",1,0,0,\"none\")"), text("none"));
    QCOMPARE(eval("TEXTBEFORE(\"abc\",\"b\",0)"), Value(Err::Value));
    QCOMPARE(eval("TEXTBEFORE(\"abc\",\"b\",4)"), Value(Err::Value));
    // A delimiter list: whichever comes first (G1:G2 holds "aa", "bb").
    QCOMPARE(eval("TEXTBEFORE(\"xxbbyyaazz\",G1:G2)"), text("xx"));
    QCOMPARE(eval("TEXTAFTER(\"xxbbyyaazz\",G1:G2,2)"), text("zz"));

    // Cell introspection reads the host: Scratch!A1 holds =1+1.
    QCOMPARE(eval("ISFORMULA(Scratch!A1)"), Value(true));
    QCOMPARE(eval("ISFORMULA(A1)"), Value(false));
    QCOMPARE(eval("FORMULATEXT(Scratch!A1)"), text("=1+1"));
    QCOMPARE(eval("FORMULATEXT(A1)"), Value(Err::NA));
    QCOMPARE(eval("ISFORMULA(5)"), Value(Err::Value));
    QCOMPARE(eval("SHEETS()"), Value(2.0));
    QCOMPARE(eval("SHEET(Scratch!A1)"), Value(2.0));
    QCOMPARE(eval("SHEET(\"Scratch\")"), Value(2.0));

    // The criteria family compares an error cell instead of failing on it:
    // it just does not match (unless the criterion names that error), and an
    // error in a value cell counts only when its row matches.
    wb.setCellValue(1, {0, 1}, 1.0);
    wb.setCellValue(1, {1, 1}, Err::Div0);
    wb.setCellValue(1, {2, 1}, 3.0);
    wb.setCellValue(1, {0, 2}, 10.0);
    wb.setCellValue(1, {1, 2}, Err::NA);
    wb.setCellValue(1, {2, 2}, 30.0);
    QCOMPARE(eval("COUNTIF(Scratch!B1:B3,\">0\")"), Value(2.0));
    QCOMPARE(eval("COUNTIF(Scratch!B1:B3,\"#DIV/0!\")"), Value(1.0));
    QCOMPARE(eval("SUMIF(Scratch!B1:B3,\">0\")"), Value(4.0));
    QCOMPARE(eval("SUMIF(Scratch!B1:B3,\">0\",Scratch!C1:C3)"), Value(40.0));
    QCOMPARE(eval("SUMIF(Scratch!B1:B3,\"#DIV/0!\",Scratch!C1:C3)"), Value(Err::NA));
    QCOMPARE(eval("COUNTIFS(Scratch!B1:B3,\">0\",Scratch!C1:C3,\">0\")"), Value(2.0));
    QCOMPARE(eval("SUMIFS(Scratch!C1:C3,Scratch!B1:B3,\">2\")"), Value(30.0));
    QCOMPARE(eval("MAXIFS(Scratch!C1:C3,Scratch!B1:B3,\">0\")"), Value(30.0));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcFunctionsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_functions.moc"
