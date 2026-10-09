// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): every data-validation kind and operator against hand-worked
// expectations — whole numbers, decimals, dates and times with formula
// operands, text lengths, typed lists and a list's live source, custom
// formulas with and without "Ignore blank", "Any value", QindaCalc's own
// not-empty rule —, the typed-input path the app asks first, and the
// descriptions a refusal shows.

#include "functions/functions.h"
#include "sheet_rules.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QDate>
#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

CellRange span(qint32 r0, qint32 c0, qint32 r1, qint32 c1)
{
    return {{r0, c0}, {r1, c1}};
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

Workbook book()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    return wb;
}

} // namespace

class CalcValidationTest : public QObject {
    Q_OBJECT
private slots:
    void validationKindsAndOperators();
    void validationTextPath();
    void validationDescriptions();
};

void CalcValidationTest::validationKindsAndOperators()
{
    using Kind = ValidationRule::Kind;
    using VOp = ValidationRule::Operator;
    Workbook wb = book();
    wb.setCellInput(0, {0, 2}, QStringLiteral("10"));
    wb.setCellInput(0, {0, 4}, QStringLiteral("Red"));
    wb.setCellInput(0, {1, 4}, QStringLiteral("Blue"));
    column(wb, 1, {QStringLiteral("3"), QStringLiteral("4")});
    const auto ok = [&wb](const ValidationRule &rule, const Value &value, const CellAddr &at = {0, 0}) {
        return validationAcceptsValue(rule, at, value, wb, 0);
    };
    ValidationRule whole;
    whole.kind = Kind::WholeNumber;
    whole.range = span(0, 0, 9, 0);
    whole.min = 1;
    whole.max = 10;
    QVERIFY(ok(whole, 5.0) && !ok(whole, 5.5) && !ok(whole, 11.0) && ok(whole, Value{}));
    QVERIFY(!ok(whole, QStringLiteral("5"))); // text is not a number, even "5"
    whole.op = VOp::GreaterThan;
    whole.min = 3;
    QVERIFY(ok(whole, 4.0) && !ok(whole, 3.0));
    ValidationRule decimal = whole;
    decimal.kind = Kind::Decimal;
    decimal.op = VOp::NotBetween;
    decimal.min = 0;
    decimal.max = 1;
    QVERIFY(!ok(decimal, 0.5) && ok(decimal, 2.0));
    ValidationRule date = whole;
    date.kind = Kind::Date;
    date.op = VOp::Between;
    date.formula1 = QStringLiteral("DATE(2026,1,1)");
    date.formula2 = QStringLiteral("DATE(2026,12,31)");
    QVERIFY(prepareValidationRule(date));
    QVERIFY(ok(date, serialFromDate(QDate(2026, 6, 1))) && !ok(date, serialFromDate(QDate(2025, 12, 31))));
    ValidationRule time = whole;
    time.kind = Kind::Time;
    time.op = VOp::LessThan;
    time.min = 0.5;
    QVERIFY(ok(time, 0.25) && !ok(time, 0.75));
    ValidationRule length = whole;
    length.kind = Kind::TextLength;
    length.op = VOp::Equal;
    length.min = 3;
    QVERIFY(ok(length, QStringLiteral("abc")) && !ok(length, QStringLiteral("ab")) && ok(length, 123.0));
    ValidationRule list;
    list.kind = Kind::List;
    list.range = span(0, 0, 9, 0);
    list.list = {QStringLiteral("Open"), QStringLiteral("Done")};
    QVERIFY(ok(list, QStringLiteral("done")) && !ok(list, QStringLiteral("Closed")));
    ValidationRule source = list;
    source.list.clear();
    source.listSource = QStringLiteral("$E$1:$E$2");
    QVERIFY(prepareValidationRule(source));
    QVERIFY(ok(source, QStringLiteral("blue")) && !ok(source, QStringLiteral("Green")));
    wb.setCellInput(0, {1, 4}, QStringLiteral("Green")); // the live source is read again
    QVERIFY(ok(source, QStringLiteral("Green")));
    ValidationRule custom;
    custom.kind = Kind::Custom;
    custom.range = span(0, 0, 2, 0);
    custom.formula1 = QStringLiteral("A1>B1");
    QVERIFY(prepareValidationRule(custom));
    wb.setCellInput(0, {0, 0}, QStringLiteral("5"));
    wb.setCellInput(0, {1, 0}, QStringLiteral("1"));
    QVERIFY(ok(custom, 5.0, {0, 0}));
    QVERIFY(!ok(custom, 1.0, {1, 0})); // A2 > B2 is false
    ValidationRule notBlank = custom;
    notBlank.formula1 = QStringLiteral("LEN(A1)>0");
    notBlank.allowBlank = false;
    QVERIFY(prepareValidationRule(notBlank));
    QVERIFY(!ok(notBlank, Value{}, {5, 0}));
    notBlank.allowBlank = true;
    QVERIFY(ok(notBlank, Value{}, {5, 0}));
    ValidationRule any;
    any.kind = Kind::Any;
    QVERIFY(ok(any, QStringLiteral("anything")));
    ValidationRule required;
    required.kind = Kind::NotEmpty;
    QVERIFY(!ok(required, Value{}) && ok(required, QStringLiteral("x")));
    ValidationRule operand = whole;
    operand.op = VOp::Between;
    operand.formula1 = QStringLiteral("$C$1"); // 10
    operand.max = 100;
    QVERIFY(prepareValidationRule(operand));
    QVERIFY(!ok(operand, 5.0) && ok(operand, 20.0));
}

void CalcValidationTest::validationTextPath()
{
    using Kind = ValidationRule::Kind;
    using VOp = ValidationRule::Operator;
    ValidationRule whole;
    whole.kind = Kind::WholeNumber;
    whole.op = VOp::GreaterThan;
    whole.min = 3;
    QVERIFY(validationAccepts(whole, QStringLiteral("4")));
    QVERIFY(!validationAccepts(whole, QStringLiteral("2")));
    QVERIFY(!validationAccepts(whole, QStringLiteral("x")));
    QVERIFY(validationAccepts(whole, QString())); // clearing is never refused
    ValidationRule length;
    length.kind = Kind::TextLength;
    length.op = VOp::LessThan;
    length.min = 4;
    QVERIFY(validationAccepts(length, QStringLiteral("abc")));
    QVERIFY(!validationAccepts(length, QStringLiteral("abcd")));
    // What needs the workbook is the host-aware path's to decide.
    ValidationRule date;
    date.kind = Kind::Date;
    QVERIFY(validationAccepts(date, QStringLiteral("whatever")));
    ValidationRule operand = whole;
    operand.formula1 = QStringLiteral("$C$1");
    QVERIFY(validationAccepts(operand, QStringLiteral("1")));
}

void CalcValidationTest::validationDescriptions()
{
    using Kind = ValidationRule::Kind;
    using VOp = ValidationRule::Operator;
    ValidationRule rule;
    for (const Kind kind : {Kind::WholeNumber, Kind::Decimal, Kind::List, Kind::TextLength, Kind::NotEmpty, Kind::Date,
                            Kind::Time, Kind::Custom, Kind::Any}) {
        rule.kind = kind;
        QVERIFY(!validationDescription(rule).isEmpty());
    }
    rule.kind = Kind::WholeNumber;
    rule.op = VOp::GreaterThanOrEqual;
    rule.min = 5;
    QCOMPARE(validationDescription(rule), QStringLiteral("a whole number greater than or equal to 5"));
    rule.formula1 = QStringLiteral("$C$1");
    QVERIFY(validationDescription(rule).contains(QStringLiteral("=$C$1")));
    rule.message = QStringLiteral("Use your own words");
    QCOMPARE(validationDescription(rule), QStringLiteral("Use your own words"));
}

QTEST_GUILESS_MAIN(CalcValidationTest)

#include "tst_calc_validation.moc"
