// SPDX-License-Identifier: GPL-3.0-or-later
// Fixture-driven function suite (plan section 7.2): every row of
// tests/fixtures/functions/functions.tsv is an expression whose expected
// value was produced by tools/lo_oracle.py through headless LibreOffice.
// Error classes are compared after normalization (LibreOffice exports some
// errors as Err:5xx in CSV form).

#include "eval.h"
#include "functions/functions.h"
#include "parser.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QFile>
#include <QtTest>

using namespace QindaOffice::Calc;

class CalcFunctionsTest : public QObject {
    Q_OBJECT
private slots:
    void fixtureRows();
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

    QFile file(QStringLiteral(SOURCE_DIR "/../fixtures/functions/functions.tsv"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.fileName()));
    int row = 0;
    int failed = 0;
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
    QVERIFY2(row >= 300, qPrintable(QStringLiteral("fixture must carry >=300 rows, has %1").arg(row)));
    QCOMPARE(failed, 0);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcFunctionsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_functions.moc"
