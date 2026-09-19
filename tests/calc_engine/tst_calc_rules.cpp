// SPDX-License-Identifier: GPL-3.0-or-later
// Per-sheet rules (calc_engine/sheet_rules.h): conditional formatting, data
// validation, the autofilter and cell comments.
//
// These are evaluated by the grid, the printer and the recovery payload
// alike, so they are engine behavior, not view behavior — and the parts
// that are easy to get wrong are the edges: a blank cell against a numeric
// rule, an empty input against a validation rule, and the header row of a
// filter.

#include "sheet.h"
#include "sheet_rules.h"
#include "structure.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

CellAddr at(qint32 row, qint32 col)
{
    return {row, col};
}

CondFormatRule numericRule(CondFormatRule::Op op, double first, const CellRange &range)
{
    CondFormatRule rule;
    rule.op = op;
    rule.first = first;
    rule.range = range;
    rule.format.background = Rgb{255, 0, 0};
    return rule;
}

} // namespace

class CalcRulesTest : public QObject {
    Q_OBJECT
private slots:
    void blankCellsSatisfyNoComparison();
    void rulesLayerInOrder();
    void expressionRulesEvaluateShifted();
    void dataBarsScaleOverTheRange();
    void stopIfTrueTruncatesTheWalk();
    void validationNeverBlocksClearing();
    void validationChecksListsAndLengths();
    void filterHidesRowsButNeverTheHeader();
    void filterValuesAreDistinctAndSorted();
    void commentsRoundTripAndClear();
    void rulesFollowStructuralEdits();
};

void CalcRulesTest::blankCellsSatisfyNoComparison()
{
    const CellRange all{at(0, 0), at(9, 9)};
    const CondFormatRule lessThanTen = numericRule(CondFormatRule::Op::LessThan, 10, all);
    // A blank cell is not zero: treating it as zero would paint every empty
    // cell of a "less than 10" rule.
    QVERIFY(!condRuleMatches(lessThanTen, Value{}));
    QVERIFY(condRuleMatches(lessThanTen, Value(3.0)));

    CondFormatRule empty;
    empty.op = CondFormatRule::Op::IsEmpty;
    empty.range = all;
    QVERIFY(condRuleMatches(empty, Value{}));
    QVERIFY(!condRuleMatches(empty, Value(0.0)));

    CondFormatRule contains;
    contains.op = CondFormatRule::Op::ContainsText;
    contains.text = QStringLiteral("dra");
    contains.range = all;
    QVERIFY(condRuleMatches(contains, Value(QStringLiteral("Draft"))));  // case-insensitive
    QVERIFY(!condRuleMatches(contains, Value(QStringLiteral("final"))));
}

void CalcRulesTest::rulesLayerInOrder()
{
    Sheet sheet(QStringLiteral("Sheet1"));
    const CellRange all{at(0, 0), at(9, 9)};
    CondFormatRule first = numericRule(CondFormatRule::Op::GreaterThan, 0, all);
    first.format.background = Rgb{200, 200, 200};
    CondFormatRule second = numericRule(CondFormatRule::Op::GreaterThan, 5, all);
    second.format.background = Rgb{0, 200, 0};
    second.format.bold = true;
    sheet.setCondFormats({first, second});

    const CondFormat small = sheet.condFormatFor(at(0, 0), Value(2.0));
    QVERIFY(small.background.has_value());
    QCOMPARE(small.background->g, quint8(200));
    QCOMPARE(small.background->r, quint8(200));
    QVERIFY(!small.bold.has_value());

    // First-wins per field (Excel's precedence model): the earlier rule's
    // background is final, and the later rule still contributes the bold it
    // alone sets — the two compose rather than one cancelling the other.
    const CondFormat large = sheet.condFormatFor(at(0, 0), Value(7.0));
    QCOMPARE(large.background->r, quint8(200));
    QVERIFY(large.bold.value_or(false));
}

void CalcRulesTest::expressionRulesEvaluateShifted()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    // "A1>10" relative to the range's top-left: at A2 it reads A2>10, at A3
    // A3>10 — the per-cell view Excel's expression rules produce.
    wb.setCellInput(0, {0, 0}, QStringLiteral("5"));
    wb.setCellInput(0, {1, 0}, QStringLiteral("11"));
    wb.setCellInput(0, {2, 0}, QStringLiteral("12"));
    CondFormatRule rule;
    rule.op = CondFormatRule::Op::Expression;
    rule.range = {at(0, 0), at(2, 0)};
    rule.formula = QStringLiteral("A1>10");
    rule.format.bold = true;
    QVERIFY(prepareCondRule(rule));
    wb.sheetAt(0).setCondFormats({rule});
    QVERIFY(!wb.condFormatFor(0, at(0, 0)).bold.has_value());
    QVERIFY(wb.condFormatFor(0, at(1, 0)).bold.value_or(false));
    QVERIFY(wb.condFormatFor(0, at(2, 0)).bold.value_or(false));
    // A rule whose formula does not parse is dropped by the caller.
    CondFormatRule broken = rule;
    broken.formula = QStringLiteral("A1>");
    QVERIFY(!prepareCondRule(broken));
}

void CalcRulesTest::dataBarsScaleOverTheRange()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    for (int i = 0; i < 4; ++i) {
        wb.setCellInput(0, {i, 0}, QString::number((i + 1) * 10));
    }
    CondFormatRule rule;
    rule.op = CondFormatRule::Op::DataBar;
    rule.range = {at(0, 0), at(3, 0)};
    rule.barColor = Rgb{10, 20, 30};
    wb.sheetAt(0).setCondFormats({rule});
    const double third = 1.0 / 3.0;
    QCOMPARE(wb.condFormatFor(0, at(0, 0)).dataBar->fraction, 0.0);
    QVERIFY(qAbs(wb.condFormatFor(0, at(1, 0)).dataBar->fraction - third) < 0.001);
    QVERIFY(qAbs(wb.condFormatFor(0, at(2, 0)).dataBar->fraction - 2 * third) < 0.001);
    QCOMPARE(wb.condFormatFor(0, at(3, 0)).dataBar->fraction, 1.0);
    QCOMPARE(int(wb.condFormatFor(0, at(3, 0)).dataBar->color.r), 10);
    // No bar on text.
    wb.setCellInput(0, {0, 0}, QStringLiteral("nope"));
    QVERIFY(!wb.condFormatFor(0, at(0, 0)).dataBar.has_value());
}

void CalcRulesTest::stopIfTrueTruncatesTheWalk()
{
    Sheet sheet(QStringLiteral("Sheet1"));
    const CellRange all{at(0, 0), at(9, 9)};
    CondFormatRule stop = numericRule(CondFormatRule::Op::GreaterThan, 0, all);
    stop.format.background = Rgb{255, 0, 0};
    stop.stopIfTrue = true;
    CondFormatRule other = numericRule(CondFormatRule::Op::GreaterThan, 0, all);
    other.format.background = Rgb{0, 0, 255};
    other.format.bold = true;

    // stopIfTrue first: the later rule never contributes.
    sheet.setCondFormats({stop, other});
    CondFormat merged = sheet.condFormatFor(at(0, 0), Value(5.0));
    QCOMPARE(merged.background->r, quint8(255));
    QVERIFY(!merged.bold.has_value());

    // stopIfTrue second: the earlier rule's fields are final anyway, and
    // the stop rule contributes nothing new — but evaluation ends there.
    sheet.setCondFormats({other, stop});
    merged = sheet.condFormatFor(at(0, 0), Value(5.0));
    QCOMPARE(merged.background->b, quint8(255));
    QVERIFY(merged.bold.value_or(false));
}

void CalcRulesTest::validationNeverBlocksClearing()
{
    ValidationRule rule;
    rule.kind = ValidationRule::Kind::WholeNumber;
    rule.min = 1;
    rule.max = 10;
    // AGENT-GUARD: a rule that refused the empty string would trap a
    // mistyped value — the user could neither fix it nor delete it.
    QVERIFY(validationAccepts(rule, QString()));
    QVERIFY(validationAccepts(rule, QStringLiteral("5")));
    QVERIFY(!validationAccepts(rule, QStringLiteral("5.5")));
    QVERIFY(!validationAccepts(rule, QStringLiteral("11")));
    QVERIFY(!validationAccepts(rule, QStringLiteral("nope")));

    ValidationRule required;
    required.kind = ValidationRule::Kind::NotEmpty;
    QVERIFY(!validationAccepts(required, QString()));
    QVERIFY(!validationAccepts(required, QStringLiteral("   ")));
    QVERIFY(validationAccepts(required, QStringLiteral("x")));
}

void CalcRulesTest::validationChecksListsAndLengths()
{
    ValidationRule list;
    list.kind = ValidationRule::Kind::List;
    list.list = {QStringLiteral("Open"), QStringLiteral("Done")};
    QVERIFY(validationAccepts(list, QStringLiteral("done"))); // case-insensitive
    QVERIFY(!validationAccepts(list, QStringLiteral("Closed")));

    ValidationRule length;
    length.kind = ValidationRule::Kind::TextLength;
    length.min = 2;
    length.max = 4;
    QVERIFY(validationAccepts(length, QStringLiteral("abc")));
    QVERIFY(!validationAccepts(length, QStringLiteral("a")));
    QVERIFY(!validationAccepts(length, QStringLiteral("abcde")));
    QVERIFY(!validationDescription(length).isEmpty());
}

void CalcRulesTest::filterHidesRowsButNeverTheHeader()
{
    Workbook book;
    Sheet &sheet = book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(0, 0), QStringLiteral("Status"));
    book.setCellInput(0, at(1, 0), QStringLiteral("Open"));
    book.setCellInput(0, at(2, 0), QStringLiteral("Done"));
    book.setCellInput(0, at(3, 0), QStringLiteral("Open"));

    AutoFilter filter;
    filter.range = {at(0, 0), at(3, 0)};
    filter.active = true;
    filter.allowed.insert(0, {QStringLiteral("Open")});
    sheet.setAutoFilter(filter);

    const QSet<qint32> hidden = sheet.filteredOutRows();
    QCOMPARE(hidden.size(), 1);
    QVERIFY(hidden.contains(2));
    // The header carries the dropdowns; hiding it would take the filter's
    // own controls off the screen.
    QVERIFY(!hidden.contains(0));

    // An inactive filter hides nothing at all, so the view can treat "no
    // filter" and "filter matching everything" alike.
    filter.active = false;
    sheet.setAutoFilter(filter);
    QVERIFY(sheet.filteredOutRows().isEmpty());
}

void CalcRulesTest::filterValuesAreDistinctAndSorted()
{
    Workbook book;
    Sheet &sheet = book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(0, 0), QStringLiteral("Status"));
    for (int i = 0; i < 3; ++i) {
        book.setCellInput(0, at(i + 1, 0),
                          i == 1 ? QStringLiteral("Done") : QStringLiteral("Open"));
    }
    AutoFilter filter;
    filter.range = {at(0, 0), at(3, 0)};
    filter.active = true;
    sheet.setAutoFilter(filter);

    const QStringList values = sheet.filterValuesFor(0, [&book](const CellAddr &addr) {
        return toText(book.cellValue(0, addr.row, addr.col));
    });
    QCOMPARE(values, (QStringList{QStringLiteral("Done"), QStringLiteral("Open")}));
}

void CalcRulesTest::commentsRoundTripAndClear()
{
    Sheet sheet(QStringLiteral("Sheet1"));
    sheet.setCommentAt(at(1, 1), CellComment{QStringLiteral("jc"), QStringLiteral("check this")});
    QVERIFY(sheet.commentAt(at(1, 1)) != nullptr);
    QCOMPARE(sheet.commentAt(at(1, 1))->text, QStringLiteral("check this"));
    QVERIFY(sheet.commentAt(at(0, 0)) == nullptr);
    // Setting empty text removes the comment: an empty balloon marker is
    // noise the user cannot get rid of any other way.
    sheet.setCommentAt(at(1, 1), CellComment{});
    QVERIFY(sheet.commentAt(at(1, 1)) == nullptr);
}

void CalcRulesTest::rulesFollowStructuralEdits()
{
    Workbook book;
    Sheet &sheet = book.addSheet(QStringLiteral("Sheet1"));
    sheet.setCondFormats({numericRule(CondFormatRule::Op::GreaterThan, 1, {at(4, 0), at(6, 0)})});
    ValidationRule validation;
    validation.kind = ValidationRule::Kind::NotEmpty;
    validation.range = {at(4, 0), at(4, 0)};
    sheet.setValidations({validation});
    AutoFilter filter;
    filter.range = {at(4, 0), at(8, 2)};
    filter.active = true;
    sheet.setAutoFilter(filter);
    sheet.setCommentAt(at(4, 0), CellComment{QString(), QStringLiteral("note")});

    applyStructureEdit(book, {StructureOp::InsertRows, 0, 0, 2});

    // Rules address cells by RANGE; one left un-moved paints, validates or
    // filters the wrong cells after an insert.
    const Sheet &moved = book.sheetAt(0);
    QCOMPARE(moved.condFormats().first().range.topLeft.row, 6);
    QCOMPARE(moved.validations().first().range.topLeft.row, 6);
    QCOMPARE(moved.autoFilter().range.topLeft.row, 6);
    QVERIFY(moved.commentAt(at(6, 0)) != nullptr);
    QVERIFY(moved.commentAt(at(4, 0)) == nullptr);
}

QTEST_MAIN(CalcRulesTest)
#include "tst_calc_rules.moc"
