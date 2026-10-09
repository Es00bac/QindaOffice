// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): every conditional-format kind, against hand-worked
// expectations — Excel's semantics as the
// formulas Excel itself stores for each kind spell them: a blank compares
// as 0 or "", text sorts after numbers, SEARCH's wildcards, LEN(TRIM()) for
// blanks, the sample standard deviation, Sunday-based weeks, PERCENTILE.INC
// thresholds, Excel 2007's 10–90 % bars and Excel 2010's axis, icon bands
// with gte and reverse, and a rule's whole sqref for its statistics with
// formulas anchored at its first range. Validations: tst_calc_validation.

#include "functions/functions.h"
#include "sheet_rules.h"
#include "structure.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QDate>
#include <QtTest>

#include <cmath>

using namespace QindaOffice::Calc;
using RuleOp = CondFormatRule::Op;

namespace {

CellRange span(qint32 r0, qint32 c0, qint32 r1, qint32 c1)
{
    return {{r0, c0}, {r1, c1}};
}

CondFormatRule makeRule(RuleOp op, const CellRange &range)
{
    CondFormatRule r;
    r.op = op;
    r.range = range;
    r.format.background = Rgb{255, 0, 0};
    return r;
}

// The cells of column `col` (rows 0..) take `inputs` as typed.
void column(Workbook &wb, qint32 col, const QStringList &inputs)
{
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        if (!inputs.at(i).isNull()) {
            wb.setCellInput(0, {qint32(i), col}, inputs.at(i));
        }
    }
}

bool painted(const Workbook &wb, qint32 row, qint32 col)
{
    return wb.condFormatFor(0, {row, col}).background.has_value();
}

Workbook book()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    return wb;
}

double today()
{
    return serialFromDate(QDate::currentDate());
}

} // namespace

class CalcCondFormatTest : public QObject {
    Q_OBJECT
private slots:
    void cellIsComparesAsExcel();
    void cellIsFormulaOperands();
    void textBlankAndErrorKinds();
    void top10ByCountPercentAndBottom();
    void aboveAverageAndStdDev();
    void duplicatesAndUniques();
    void datesOccurring();
    void colorScalesInterpolate();
    void dataBarsExcel2007And2010();
    void iconSetsBandAndReverse();
    void expressionsAnchorAtTheFirstRange();
    void precedenceAcrossKinds();
    void formulasFollowStructuralEdits();
};

void CalcCondFormatTest::cellIsComparesAsExcel()
{
    Workbook wb = book();
    column(wb, 0, {QString(), QStringLiteral("5"), QStringLiteral("abc"), QStringLiteral("=1/0"), QStringLiteral("15"),
                   QStringLiteral("=0.1+0.2")});
    CondFormatRule less = makeRule(RuleOp::LessThan, span(0, 0, 5, 0));
    less.first = 10;
    wb.sheetAt(0).setCondFormats({less});
    QVERIFY(painted(wb, 0, 0));  // a blank is 0 to Excel's comparison
    QVERIFY(painted(wb, 1, 0));
    QVERIFY(!painted(wb, 2, 0)); // text sorts after every number
    QVERIFY(!painted(wb, 3, 0)); // an error matches nothing
    QVERIFY(!painted(wb, 4, 0));

    CondFormatRule greater = makeRule(RuleOp::GreaterThan, span(0, 0, 5, 0));
    greater.first = 10;
    wb.sheetAt(0).setCondFormats({greater});
    QVERIFY(!painted(wb, 0, 0));
    QVERIFY(painted(wb, 2, 0));
    QVERIFY(painted(wb, 4, 0));

    CondFormatRule text = makeRule(RuleOp::EqualTo, span(0, 0, 5, 0));
    text.text = QStringLiteral("ABC");
    wb.sheetAt(0).setCondFormats({text});
    QVERIFY(painted(wb, 2, 0)); // without regard to case
    QVERIFY(!painted(wb, 1, 0));

    CondFormatRule outside = makeRule(RuleOp::NotBetween, span(0, 0, 5, 0));
    outside.first = 10; // Excel takes the bounds in either order
    outside.second = 1;
    wb.sheetAt(0).setCondFormats({outside});
    QVERIFY(!painted(wb, 1, 0));
    QVERIFY(painted(wb, 4, 0));
    QVERIFY(painted(wb, 0, 0)); // 0 is outside 1..10

    CondFormatRule equal = makeRule(RuleOp::EqualTo, span(0, 0, 5, 0));
    equal.first = 0.3;
    wb.sheetAt(0).setCondFormats({equal});
    QVERIFY(painted(wb, 5, 0)); // 0.1+0.2 equals 0.3 within Excel's 15 digits

    // The classic path (no workbook) agrees.
    QVERIFY(condRuleMatches(less, Value{}));
    QVERIFY(!condRuleMatches(less, Value(QStringLiteral("abc"))));
}

void CalcCondFormatTest::cellIsFormulaOperands()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("5"), QStringLiteral("8"), QStringLiteral("7")});
    column(wb, 1, {QStringLiteral("1"), QStringLiteral("10"), QStringLiteral("5")});
    wb.setCellInput(0, {0, 2}, QStringLiteral("7"));
    CondFormatRule absolute = makeRule(RuleOp::GreaterThan, span(0, 0, 2, 0));
    absolute.formula = QStringLiteral("$C$1");
    QVERIFY(prepareCondRule(absolute));
    wb.sheetAt(0).setCondFormats({absolute});
    QCOMPARE(QList<bool>({painted(wb, 0, 0), painted(wb, 1, 0), painted(wb, 2, 0)}), QList<bool>({false, true, false}));
    // Relative to the anchor A1: at A2 it reads B2.
    CondFormatRule relative = makeRule(RuleOp::GreaterThan, span(0, 0, 2, 0));
    relative.formula = QStringLiteral("B1");
    QVERIFY(prepareCondRule(relative));
    wb.sheetAt(0).setCondFormats({relative});
    QCOMPARE(QList<bool>({painted(wb, 0, 0), painted(wb, 1, 0), painted(wb, 2, 0)}), QList<bool>({true, false, true}));
    CondFormatRule average = makeRule(RuleOp::GreaterThanOrEqual, span(0, 0, 2, 0));
    average.formula = QStringLiteral("AVERAGE($A$1:$A$3)");
    QVERIFY(prepareCondRule(average));
    wb.sheetAt(0).setCondFormats({average});
    QCOMPARE(QList<bool>({painted(wb, 0, 0), painted(wb, 1, 0), painted(wb, 2, 0)}), QList<bool>({false, true, true}));
    CondFormatRule between = makeRule(RuleOp::Between, span(0, 0, 2, 0));
    between.first = 6;
    between.formula2 = QStringLiteral("$C$1");
    QVERIFY(prepareCondRule(between));
    wb.sheetAt(0).setCondFormats({between});
    QCOMPARE(QList<bool>({painted(wb, 0, 0), painted(wb, 1, 0), painted(wb, 2, 0)}), QList<bool>({false, false, true}));
    // An operand that does not parse fails preparation.
    CondFormatRule broken = makeRule(RuleOp::EqualTo, span(0, 0, 2, 0));
    broken.formula = QStringLiteral("A1+");
    QVERIFY(!prepareCondRule(broken));
}

void CalcCondFormatTest::textBlankAndErrorKinds()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("Draft plan"), QStringLiteral("final"), QString(), QStringLiteral("=NA()"),
                   QStringLiteral("'   "), QStringLiteral("a*b")});
    const auto check = [&wb](RuleOp op, const QString &text, const QList<bool> &expected) {
        CondFormatRule r = makeRule(op, span(0, 0, 5, 0));
        r.text = text;
        wb.sheetAt(0).setCondFormats({r});
        QList<bool> got;
        for (int row = 0; row < 6; ++row) {
            got.append(painted(wb, row, 0));
        }
        return got == expected;
    };
    QVERIFY(check(RuleOp::ContainsText, QStringLiteral("dra"), {true, false, false, false, false, false}));
    QVERIFY(check(RuleOp::NotContainsText, QStringLiteral("dra"), {false, true, true, true, true, true}));
    QVERIFY(check(RuleOp::BeginsWith, QStringLiteral("DRAFT"), {true, false, false, false, false, false}));
    QVERIFY(check(RuleOp::EndsWith, QStringLiteral("PLAN"), {true, false, false, false, false, false}));
    QVERIFY(check(RuleOp::ContainsText, QStringLiteral("f?n"), {false, true, false, false, false, false})); // SEARCH's ?
    QVERIFY(check(RuleOp::ContainsText, QStringLiteral("~*"), {false, false, false, false, false, true}));  // a literal *
    QVERIFY(check(RuleOp::IsEmpty, QString(), {false, false, true, false, true, false}));                   // spaces count
    QVERIFY(check(RuleOp::NotEmpty, QString(), {true, true, false, false, false, true}));
    QVERIFY(check(RuleOp::ContainsErrors, QString(), {false, false, false, true, false, false}));
    QVERIFY(check(RuleOp::NotContainsErrors, QString(), {true, true, true, false, true, true}));
}

void CalcCondFormatTest::top10ByCountPercentAndBottom()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("30"), QStringLiteral("40"), QStringLiteral("50"),
                   QStringLiteral("60"), QStringLiteral("70"), QStringLiteral("80"), QStringLiteral("90"), QStringLiteral("90"),
                   QStringLiteral("text")});
    const auto hits = [&wb](int rank, bool percent, bool bottom) {
        CondFormatRule r = makeRule(RuleOp::Top10, span(0, 0, 10, 0));
        r.rank = rank;
        r.percent = percent;
        r.bottom = bottom;
        wb.sheetAt(0).setCondFormats({r});
        QList<int> rows;
        for (int row = 0; row < 11; ++row) {
            if (painted(wb, row, 0)) {
                rows.append(row);
            }
        }
        return rows;
    };
    QCOMPARE(hits(2, false, false), (QList<int>{8, 9}));      // the tie at the top counts once
    QCOMPARE(hits(3, false, false), (QList<int>{7, 8, 9}));
    QCOMPARE(hits(10, true, false), (QList<int>{8, 9}));      // 10 % of 10 numbers: one item, and its tie
    QCOMPARE(hits(25, true, false), (QList<int>{8, 9}));      // 2.5 → 2 items
    QCOMPARE(hits(2, false, true), (QList<int>{0, 1}));
    QCOMPARE(hits(5, true, true), (QList<int>{0}));           // 0.5 → at least one
    QCOMPARE(hits(50, false, false).size(), 10);              // more than there are: every number, never the text
    // Over a two-range sqref the statistics take both ranges.
    column(wb, 2, {QStringLiteral("100"), QStringLiteral("5"), QStringLiteral("50")});
    CondFormatRule wide = makeRule(RuleOp::Top10, span(0, 0, 2, 0));
    wide.moreRanges = {span(0, 2, 2, 2)};
    wide.rank = 1;
    wb.sheetAt(0).setCondFormats({wide});
    QVERIFY(painted(wb, 0, 2));
    QVERIFY(!painted(wb, 2, 0));
}

void CalcCondFormatTest::aboveAverageAndStdDev()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("4"), QStringLiteral("4"), QStringLiteral("5"),
                   QStringLiteral("5"), QStringLiteral("7"), QStringLiteral("9")});
    const auto hits = [&wb](bool above, bool equal, int stdDev) {
        CondFormatRule r = makeRule(RuleOp::AboveAverage, span(0, 0, 7, 0));
        r.aboveAverage = above;
        r.equalAverage = equal;
        r.stdDev = stdDev;
        wb.sheetAt(0).setCondFormats({r});
        QList<int> rows;
        for (int row = 0; row < 8; ++row) {
            if (painted(wb, row, 0)) {
                rows.append(row);
            }
        }
        return rows;
    };
    // Mean 5; sample standard deviation √(32/7) ≈ 2.138.
    QCOMPARE(hits(true, false, 0), (QList<int>{6, 7}));
    QCOMPARE(hits(true, true, 0), (QList<int>{4, 5, 6, 7}));
    QCOMPARE(hits(false, false, 0), (QList<int>{0, 1, 2, 3}));
    QCOMPARE(hits(true, false, 1), (QList<int>{7}));  // above 7.138
    QCOMPARE(hits(false, false, 1), (QList<int>{0})); // below 2.862
}

void CalcCondFormatTest::duplicatesAndUniques()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("Apple"), QStringLiteral("apple"), QStringLiteral("Pear"), QStringLiteral("5"),
                   QStringLiteral("5.0"), QString(), QStringLiteral("Kiwi")});
    wb.setCellInput(0, {0, 2}, QStringLiteral("PEAR"));
    CondFormatRule dup = makeRule(RuleOp::Duplicate, span(0, 0, 6, 0));
    dup.moreRanges = {span(0, 2, 0, 2)};
    wb.sheetAt(0).setCondFormats({dup});
    QList<bool> got;
    for (int row = 0; row < 7; ++row) {
        got.append(painted(wb, row, 0));
    }
    QCOMPARE(got, (QList<bool>{true, true, true, true, true, false, false}));
    QVERIFY(painted(wb, 0, 2)); // PEAR matches Pear in the other range
    CondFormatRule unique = dup;
    unique.op = RuleOp::Unique;
    wb.sheetAt(0).setCondFormats({unique});
    QVERIFY(painted(wb, 6, 0));
    QVERIFY(!painted(wb, 0, 0));
    QVERIFY(!painted(wb, 5, 0)); // a blank is neither
}

void CalcCondFormatTest::datesOccurring()
{
    Workbook wb = book();
    const double now = today();
    const int weekday = QDate::currentDate().dayOfWeek() % 7 + 1; // Sunday 1
    const double weekStart = now - (weekday - 1);
    const QDate month = QDate::currentDate();
    const QList<double> dates{now,           now - 1,       now + 1,       now - 6,       now - 7,
                              weekStart,     weekStart - 1, weekStart - 7, weekStart - 8, weekStart + 7,
                              serialFromDate(QDate(month.year(), month.month(), 1)),
                              serialFromDate(QDate(month.year(), month.month(), 1).addMonths(-1)),
                              serialFromDate(QDate(month.year(), month.month(), 1).addMonths(1)), now + 0.75};
    for (qsizetype i = 0; i < dates.size(); ++i) {
        wb.setCellValue(0, {qint32(i), 0}, dates.at(i));
    }
    wb.setCellInput(0, {qint32(dates.size()), 0}, QStringLiteral("today"));
    const auto rows = [&wb, &dates](CondFormatRule::Period period) {
        CondFormatRule r = makeRule(RuleOp::TimePeriod, span(0, 0, qint32(dates.size()), 0));
        r.period = period;
        wb.sheetAt(0).setCondFormats({r});
        QList<int> out;
        for (int row = 0; row <= dates.size(); ++row) {
            if (painted(wb, row, 0)) {
                out.append(row);
            }
        }
        return out;
    };
    using P = CondFormatRule::Period;
    QVERIFY(rows(P::Today).contains(0) && rows(P::Today).contains(13) && !rows(P::Today).contains(1));
    QVERIFY(rows(P::Yesterday).contains(1) && !rows(P::Yesterday).contains(0));
    QVERIFY(rows(P::Tomorrow).contains(2) && !rows(P::Tomorrow).contains(0));
    const QList<int> week = rows(P::Last7Days);
    QVERIFY(week.contains(0) && week.contains(1) && week.contains(3) && !week.contains(2) && !week.contains(4));
    QVERIFY(rows(P::ThisWeek).contains(5) && !rows(P::ThisWeek).contains(6));
    QVERIFY(rows(P::LastWeek).contains(6) && rows(P::LastWeek).contains(7) && !rows(P::LastWeek).contains(8));
    QVERIFY(rows(P::NextWeek).contains(9) && !rows(P::NextWeek).contains(5));
    QVERIFY(rows(P::ThisMonth).contains(10) && !rows(P::ThisMonth).contains(11));
    QVERIFY(rows(P::LastMonth).contains(11) && !rows(P::LastMonth).contains(10));
    QVERIFY(rows(P::NextMonth).contains(12) && !rows(P::NextMonth).contains(11));
    QVERIFY(!rows(P::Today).contains(int(dates.size()))); // text is never a date
}

void CalcCondFormatTest::colorScalesInterpolate()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("0"), QStringLiteral("50"), QStringLiteral("100")});
    CondFormatRule two = makeRule(RuleOp::ColorScale, span(0, 0, 2, 0));
    two.thresholds = {CfThreshold{CfThreshold::Type::Min, {}, true}, CfThreshold{CfThreshold::Type::Max, {}, true}};
    two.colors = {Rgb{255, 0, 0}, Rgb{0, 0, 255}};
    wb.sheetAt(0).setCondFormats({two});
    QVERIFY(*wb.condFormatFor(0, {0, 0}).background == (Rgb{255, 0, 0}));
    QVERIFY(*wb.condFormatFor(0, {1, 0}).background == (Rgb{128, 0, 128}));
    QVERIFY(*wb.condFormatFor(0, {2, 0}).background == (Rgb{0, 0, 255}));

    Workbook five = book();
    column(five, 0, {QStringLiteral("0"), QStringLiteral("25"), QStringLiteral("50"), QStringLiteral("75"), QStringLiteral("100"),
                     QStringLiteral("text")});
    CondFormatRule three = makeRule(RuleOp::ColorScale, span(0, 0, 5, 0));
    three.thresholds = {CfThreshold{CfThreshold::Type::Min, {}, true},
                        CfThreshold{CfThreshold::Type::Percentile, QStringLiteral("50"), true},
                        CfThreshold{CfThreshold::Type::Max, {}, true}};
    three.colors = {Rgb{255, 0, 0}, Rgb{255, 255, 255}, Rgb{0, 255, 0}};
    five.sheetAt(0).setCondFormats({three});
    QVERIFY(*five.condFormatFor(0, {1, 0}).background == (Rgb{255, 128, 128}));
    QVERIFY(*five.condFormatFor(0, {2, 0}).background == (Rgb{255, 255, 255}));
    QVERIFY(*five.condFormatFor(0, {3, 0}).background == (Rgb{128, 255, 128}));
    QVERIFY(!five.condFormatFor(0, {5, 0}).background.has_value()); // no colour on text
    // A number threshold: below it, the first colour.
    three.thresholds.first() = CfThreshold{CfThreshold::Type::Number, QStringLiteral("40"), true};
    five.sheetAt(0).setCondFormats({three});
    QVERIFY(*five.condFormatFor(0, {1, 0}).background == (Rgb{255, 0, 0}));
}

void CalcCondFormatTest::dataBarsExcel2007And2010()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("30"), QStringLiteral("40"), QStringLiteral("x")});
    CondFormatRule bar = makeRule(RuleOp::DataBar, span(0, 0, 4, 0));
    bar.barColor = Rgb{99, 142, 198};
    wb.sheetAt(0).setCondFormats({bar});
    // Excel 2007: the shortest bar is 10 % of the cell, the longest 90 %.
    QVERIFY(qAbs(wb.condFormatFor(0, {0, 0}).dataBar->fraction - 0.1) < 1e-9);
    QVERIFY(qAbs(wb.condFormatFor(0, {1, 0}).dataBar->fraction - (0.1 + 0.8 / 3)) < 1e-9);
    QVERIFY(qAbs(wb.condFormatFor(0, {3, 0}).dataBar->fraction - 0.9) < 1e-9);
    QCOMPARE(wb.condFormatFor(0, {3, 0}).dataBar->origin, 0.0);
    QVERIFY(!wb.condFormatFor(0, {4, 0}).dataBar.has_value());

    Workbook signs = book();
    column(signs, 0, {QStringLiteral("-50"), QStringLiteral("0"), QStringLiteral("50"), QStringLiteral("100")});
    CondFormatRule axis = makeRule(RuleOp::DataBar, span(0, 0, 3, 0));
    axis.thresholds = {CfThreshold{CfThreshold::Type::AutoMin, {}, true}, CfThreshold{CfThreshold::Type::AutoMax, {}, true}};
    axis.bar.x14 = true;
    axis.bar.minLength = 0;
    axis.bar.maxLength = 100;
    axis.bar.gradient = false;
    axis.showValue = false;
    signs.sheetAt(0).setCondFormats({axis});
    const CondFormat top = signs.condFormatFor(0, {3, 0});
    QVERIFY(qAbs(top.dataBar->origin - 1.0 / 3) < 1e-9); // the axis sits at 50 of 150
    QVERIFY(qAbs(top.dataBar->fraction - 2.0 / 3) < 1e-9);
    QVERIFY(!top.dataBar->negative && !top.dataBar->gradient);
    QVERIFY(top.hideValue);
    QVERIFY(qAbs(signs.condFormatFor(0, {2, 0}).dataBar->fraction - 1.0 / 3) < 1e-9);
    const DataBarFill negative = *signs.condFormatFor(0, {0, 0}).dataBar;
    QVERIFY(negative.negative);
    QVERIFY(qAbs(negative.fraction - 1.0 / 3) < 1e-9);
    QVERIFY(negative.color == (Rgb{255, 0, 0}));
    QVERIFY(negative.axisColor.has_value());
    QCOMPARE(signs.condFormatFor(0, {1, 0}).dataBar->fraction, 0.0);
    axis.bar.axis = DataBarStyle::Axis::Middle;
    axis.bar.direction = DataBarStyle::Direction::RightToLeft;
    signs.sheetAt(0).setCondFormats({axis});
    QCOMPARE(signs.condFormatFor(0, {3, 0}).dataBar->origin, 0.5);
    QVERIFY(qAbs(signs.condFormatFor(0, {3, 0}).dataBar->fraction - 0.5) < 1e-9);
    QVERIFY(qAbs(signs.condFormatFor(0, {0, 0}).dataBar->fraction - 0.25) < 1e-9);
    QVERIFY(signs.condFormatFor(0, {3, 0}).dataBar->rightToLeft);
}

void CalcCondFormatTest::iconSetsBandAndReverse()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("0"), QStringLiteral("33"), QStringLiteral("50"), QStringLiteral("67"), QStringLiteral("100")});
    CondFormatRule lights = makeRule(RuleOp::IconSet, span(0, 0, 4, 0));
    wb.sheetAt(0).setCondFormats({lights}); // Excel's default thresholds: 0, 33, 67 percent
    const auto bands = [&wb]() {
        QList<int> out;
        for (int row = 0; row < 5; ++row) {
            const CondFormat format = wb.condFormatFor(0, {row, 0});
            out.append(format.icon.has_value() ? format.icon->index : -1);
        }
        return out;
    };
    QCOMPARE(bands(), (QList<int>{0, 1, 1, 2, 2}));
    QCOMPARE(wb.condFormatFor(0, {0, 0}).icon->set, QStringLiteral("3TrafficLights1"));
    lights.thresholds = {CfThreshold{CfThreshold::Type::Percent, QStringLiteral("0"), true},
                         CfThreshold{CfThreshold::Type::Percent, QStringLiteral("33"), false},
                         CfThreshold{CfThreshold::Type::Percent, QStringLiteral("67"), true}};
    wb.sheetAt(0).setCondFormats({lights});
    QCOMPARE(bands(), (QList<int>{0, 0, 1, 2, 2})); // 33 is not above 33
    lights.reverse = true;
    lights.showValue = false;
    wb.sheetAt(0).setCondFormats({lights});
    QCOMPARE(bands(), (QList<int>{2, 2, 1, 0, 0}));
    QVERIFY(wb.condFormatFor(0, {0, 0}).hideValue);
    CondFormatRule quarters = makeRule(RuleOp::IconSet, span(0, 0, 4, 0));
    quarters.iconSet = QStringLiteral("5Quarters");
    wb.sheetAt(0).setCondFormats({quarters});
    QCOMPARE(bands(), (QList<int>{0, 1, 2, 3, 4}));
    QCOMPARE(iconSetSize(QStringLiteral("3Stars")), 3);
    QCOMPARE(iconSetSize(QStringLiteral("4Rating")), 4);
    QCOMPARE(iconSetSize(QStringLiteral("NoIcons")), 0);
}

void CalcCondFormatTest::expressionsAnchorAtTheFirstRange()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("10"), QStringLiteral("1")});
    column(wb, 2, {QStringLiteral("1"), QStringLiteral("10")});
    CondFormatRule r = makeRule(RuleOp::Expression, span(0, 0, 1, 0));
    r.moreRanges = {span(0, 2, 1, 2)};
    r.formula = QStringLiteral("A1>5"); // relative to A1, the first range's top-left
    QVERIFY(prepareCondRule(r));
    wb.sheetAt(0).setCondFormats({r});
    QVERIFY(painted(wb, 0, 0));
    QVERIFY(!painted(wb, 1, 0));
    QVERIFY(!painted(wb, 0, 2)); // C1 reads C1
    QVERIFY(painted(wb, 1, 2));
}

void CalcCondFormatTest::precedenceAcrossKinds()
{
    Workbook wb = book();
    column(wb, 0, {QStringLiteral("10"), QStringLiteral("100")});
    CondFormatRule high = makeRule(RuleOp::GreaterThan, span(0, 0, 1, 0));
    high.first = 50;
    high.format.background = Rgb{0, 255, 0};
    high.format.underline = true;
    CondFormatRule scale = makeRule(RuleOp::ColorScale, span(0, 0, 1, 0));
    scale.colors = {Rgb{255, 0, 0}, Rgb{0, 0, 255}};
    wb.sheetAt(0).setCondFormats({high, scale});
    QVERIFY(*wb.condFormatFor(0, {1, 0}).background == (Rgb{0, 255, 0})); // the higher rule's fill wins
    QVERIFY(wb.condFormatFor(0, {1, 0}).underline.value_or(false));
    QVERIFY(*wb.condFormatFor(0, {0, 0}).background == (Rgb{255, 0, 0}));
    high.stopIfTrue = true;
    CondFormatRule bar = makeRule(RuleOp::DataBar, span(0, 0, 1, 0));
    wb.sheetAt(0).setCondFormats({high, bar});
    QVERIFY(!wb.condFormatFor(0, {1, 0}).dataBar.has_value()); // stopped
    QVERIFY(wb.condFormatFor(0, {0, 0}).dataBar.has_value());
}

// A rule's formula is a formula at its anchor: it follows inserted and
// removed lines like a cell's, and when the anchor's own row goes the rule
// re-anchors on the first cell that stays (Excel's behaviour).
void CalcCondFormatTest::formulasFollowStructuralEdits()
{
    Workbook wb = book();
    wb.setCellInput(0, {0, 2}, QStringLiteral("5"));
    CondFormatRule expression = makeRule(RuleOp::Expression, span(2, 0, 5, 0));
    expression.formula = QStringLiteral("A3>$C$1");
    CondFormatRule operand = makeRule(RuleOp::GreaterThan, span(2, 1, 5, 1));
    operand.formula = QStringLiteral("$C$1");
    QVERIFY(prepareCondRule(expression) && prepareCondRule(operand));
    wb.sheetAt(0).setCondFormats({expression, operand});
    ValidationRule custom;
    custom.kind = ValidationRule::Kind::Custom;
    custom.range = span(2, 3, 5, 3);
    custom.formula1 = QStringLiteral("D3>E3");
    QVERIFY(prepareValidationRule(custom));
    wb.sheetAt(0).setValidations({custom});
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 1, 2}); // C1 stays, the rules move down two
    QCOMPARE(wb.sheetAt(0).condFormats().at(0).range.topLeft.row, 4);
    QCOMPARE(wb.sheetAt(0).condFormats().at(0).formula, QStringLiteral("A5>$C$1"));
    QCOMPARE(wb.sheetAt(0).condFormats().at(1).formula, QStringLiteral("$C$1"));
    QCOMPARE(wb.sheetAt(0).validations().at(0).formula1, QStringLiteral("D5>E5"));
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 1}); // now C1 moves to C2
    QCOMPARE(wb.sheetAt(0).condFormats().at(1).formula, QStringLiteral("$C$2"));
    applyStructureEdit(wb, {StructureOp::InsertColumns, 0, 4, 1}); // a column before E
    QCOMPARE(wb.sheetAt(0).validations().at(0).formula1, QStringLiteral("D6>F6"));
    // The anchor's row is removed: the rule keeps its meaning from the next row.
    Workbook other = book();
    column(other, 0, {QStringLiteral("1"), QStringLiteral("9"), QStringLiteral("2"), QStringLiteral("8")});
    CondFormatRule self = makeRule(RuleOp::Expression, span(0, 0, 3, 0));
    self.formula = QStringLiteral("A1>5");
    QVERIFY(prepareCondRule(self));
    other.sheetAt(0).setCondFormats({self});
    applyStructureEdit(other, {StructureOp::RemoveRows, 0, 0, 1});
    QCOMPARE(other.sheetAt(0).condFormats().at(0).formula, QStringLiteral("A1>5"));
    QVERIFY(painted(other, 0, 0));  // 9
    QVERIFY(!painted(other, 1, 0)); // 2
    QVERIFY(painted(other, 2, 0));  // 8
}

QTEST_GUILESS_MAIN(CalcCondFormatTest)

#include "tst_calc_condformat.moc"
