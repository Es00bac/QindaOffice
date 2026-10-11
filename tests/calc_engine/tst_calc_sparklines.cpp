// SPDX-License-Identifier: GPL-3.0-or-later
// M90 (D-098) in the engine: sparklines — Insert ▸ Sparklines' ranges
// (one row or column per location cell, Excel's refusals), the values a
// sparkline draws (gaps, zero, connect; hidden rows and filtered rows left
// out unless shown; a date axis), the vertical axis per sparkline, shared
// and custom, the theme's colours and Excel's tint, and sparklines through
// inserted and deleted rows and columns and a renamed sheet. Table Design:
// Excel's table-name rules, Rename Table moving every structured reference
// (cells, names, calculated columns, a pivot cache), Resize Table growing
// and shrinking with its refusals. The rules kept as Excel wrote them:
// their formulas follow inserted and deleted rows like cell formulas.

#include "pivot.h"
#include "page_layout.h"
#include "sheet.h"
#include "sparkline.h"
#include "structure.h"
#include "table_design.h"
#include "table_edit.h"
#include "theme_colors.h"
#include "workbook.h"

#include <QtTest>

#include <cmath>

using namespace QindaOffice::Calc;

namespace {

CellAddr at(const char *a1)
{
    const QString text = QString::fromLatin1(a1);
    return {text.mid(1).toInt() - 1, text.at(0).unicode() - 'A'};
}

void put(Workbook &wb, const char *a1, const QString &input, qint32 sheet = 0)
{
    QVERIFY2(wb.setCellInput(sheet, at(a1), input), a1);
}

QVector<std::optional<double>> values(std::initializer_list<std::optional<double>> list)
{
    return QVector<std::optional<double>>(list);
}

// Sheet1: B2:F4 three rows of five values (one gap, one negative).
void sales(Workbook &wb)
{
    wb.addSheet(QStringLiteral("Sheet1"));
    const char *cells[3][5] = {{"B2", "C2", "D2", "E2", "F2"}, {"B3", "C3", "D3", "E3", "F3"}, {"B4", "C4", "D4", "E4", "F4"}};
    const char *data[3][5] = {{"1", "3", "", "2", "5"}, {"4", "-2", "6", "1", "3"}, {"10", "20", "30", "40", "50"}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 5; ++c) {
            if (*data[r][c] != '\0') {
                put(wb, cells[r][c], QString::fromLatin1(data[r][c]));
            }
        }
    }
}

} // namespace

class CalcSparklinesTest : public QObject {
    Q_OBJECT
private slots:
    void insertRangesAsExcelTakesThem();
    void valuesGapsZeroAndConnect();
    void hiddenAndFilteredRowsAndDates();
    void axisScales();
    void themeColoursAndTint();
    void sparklinesFollowStructuralEdits();
    void tableNamesAsExcelChecksThem();
    void renameTableMovesEveryReference();
    void resizeTableGrowsAndShrinks();
    void keptRuleFormulasFollowEdits();
    void sparklinesExtendPrintArea();
};

void CalcSparklinesTest::insertRangesAsExcelTakesThem()
{
    Workbook wb;
    sales(wb);
    QString error;
    // A column of three locations takes the three rows.
    std::optional<QList<Sparkline>> lines = sparklinesFor(wb, 0, QStringLiteral("B2:F4"), QStringLiteral("G2:G4"), &error);
    QVERIFY2(lines.has_value(), qPrintable(error));
    QCOMPARE(lines->size(), 3);
    QCOMPARE(lines->at(0).location, at("G2"));
    QCOMPARE(lines->at(0).data, QStringLiteral("Sheet1!B2:F2"));
    QCOMPARE(lines->at(2).data, QStringLiteral("Sheet1!B4:F4"));
    // A row of five locations takes the five columns.
    lines = sparklinesFor(wb, 0, QStringLiteral("B2:F4"), QStringLiteral("$B$6:$F$6"), &error);
    QVERIFY(lines.has_value());
    QCOMPARE(lines->size(), 5);
    QCOMPARE(lines->at(1).location, at("C6"));
    QCOMPARE(lines->at(1).data, QStringLiteral("Sheet1!C2:C4"));
    // One location, one row of data, from another sheet.
    wb.addSheet(QStringLiteral("Other Data"));
    lines = sparklinesFor(wb, 0, QStringLiteral("'Other Data'!A1:A9"), QStringLiteral("H2"), &error);
    QVERIFY(lines.has_value());
    QCOMPARE(lines->first().data, QStringLiteral("'Other Data'!A1:A9"));
    // Excel's refusals.
    QVERIFY(!sparklinesFor(wb, 0, QStringLiteral("B2:F4"), QStringLiteral("G2:H4"), &error).has_value());
    QVERIFY(error.startsWith(QStringLiteral("Location reference is not valid")));
    QVERIFY(!sparklinesFor(wb, 0, QStringLiteral("B2:F4"), QStringLiteral("G2:G3"), &error).has_value());
    QVERIFY(error.startsWith(QStringLiteral("Data reference is not valid")));
    QVERIFY(!sparklinesFor(wb, 0, QStringLiteral("B2:F4"), QStringLiteral("G2"), &error).has_value());
    QVERIFY(!sparklinesFor(wb, 0, QStringLiteral("nonsense"), QStringLiteral("G2"), &error).has_value());
    QVERIFY(!sparklinesFor(wb, 0, QStringLiteral("B2:F2"), QStringLiteral("'Other Data'!G2"), &error).has_value());
    // Excel's new group: theme colours, gaps.
    const SparklineGroup line = defaultSparklineGroup(SparklineGroup::Type::Line);
    QCOMPARE(line.series.theme, 4);
    QVERIFY(line.series.tint < -0.49);
    QCOMPARE(line.negativeColor.theme, 5);
    QCOMPARE(line.empty, SparklineGroup::Empty::Gap);
}

void CalcSparklinesTest::valuesGapsZeroAndConnect()
{
    Workbook wb;
    sales(wb);
    put(wb, "D3", QStringLiteral("text")); // text counts as empty
    SparklineGroup group = defaultSparklineGroup(SparklineGroup::Type::Line);
    const Sparkline first{at("G2"), QStringLiteral("Sheet1!B2:F2")};
    group.sparklines = {first};
    QCOMPARE(sparklineSeries(wb, group, first).values, values({1, 3, std::nullopt, 2, 5}));
    group.empty = SparklineGroup::Empty::Zero;
    QCOMPARE(sparklineSeries(wb, group, first).values, values({1, 3, 0, 2, 5}));
    group.empty = SparklineGroup::Empty::Span;
    QCOMPARE(sparklineSeries(wb, group, first).values, values({1, 3, 2, 5}));
    const Sparkline second{at("G3"), QStringLiteral("Sheet1!B3:F3")};
    group.empty = SparklineGroup::Empty::Gap;
    QCOMPARE(sparklineSeries(wb, group, second).values, values({4, -2, std::nullopt, 1, 3}));
    // A data range that is gone draws nothing.
    QVERIFY(sparklineSeries(wb, group, {at("G5"), QString()}).values.isEmpty());
}

void CalcSparklinesTest::hiddenAndFilteredRowsAndDates()
{
    Workbook wb;
    sales(wb);
    SparklineGroup group = defaultSparklineGroup(SparklineGroup::Type::Column);
    const Sparkline down{at("G2"), QStringLiteral("Sheet1!D2:D4")};
    group.sparklines = {down};
    SheetOutline outline = wb.sheetAt(0).outline();
    outline.setLine(OutlineAxis::Rows, 2, LineOutline{0, true, false}); // row 3 hidden
    wb.sheetAt(0).setOutline(outline);
    QCOMPARE(sparklineSeries(wb, group, down).values, values({std::nullopt, 30}));
    group.displayHidden = true;
    QCOMPARE(sparklineSeries(wb, group, down).values, values({std::nullopt, 6, 30}));
    // A date axis: each value at its date.
    put(wb, "B6", QStringLiteral("1"));
    put(wb, "C6", QStringLiteral("2"));
    put(wb, "D6", QStringLiteral("10"));
    put(wb, "E6", QStringLiteral("11"));
    put(wb, "F6", QStringLiteral("30"));
    group.dateAxis = true;
    group.dateRange = QStringLiteral("Sheet1!B6:F6");
    const Sparkline across{at("G4"), QStringLiteral("Sheet1!B4:F4")};
    const SparklineSeries series = sparklineSeries(wb, group, across);
    QCOMPARE(series.dates, (QVector<double>{1, 2, 10, 11, 30}));
}

void CalcSparklinesTest::axisScales()
{
    Workbook wb;
    sales(wb);
    SparklineGroup group = defaultSparklineGroup(SparklineGroup::Type::Line);
    group.sparklines = {{at("G2"), QStringLiteral("Sheet1!B2:F2")}, {at("G3"), QStringLiteral("Sheet1!B3:F3")},
                        {at("G4"), QStringLiteral("Sheet1!B4:F4")}};
    const SparklineSeries first = sparklineSeries(wb, group, group.sparklines.first());
    SparklineScale scale = sparklineScale(wb, group, first);
    QCOMPARE(scale.min, 1.0);
    QCOMPARE(scale.max, 5.0);
    group.minAxis = SparklineGroup::AxisScale::Group;
    group.maxAxis = SparklineGroup::AxisScale::Group;
    scale = sparklineScale(wb, group, first);
    QCOMPARE(scale.min, -2.0);
    QCOMPARE(scale.max, 50.0);
    group.minAxis = SparklineGroup::AxisScale::Custom;
    group.manualMin = 0;
    group.maxAxis = SparklineGroup::AxisScale::Custom;
    group.manualMax = 10;
    scale = sparklineScale(wb, group, first);
    QCOMPARE(scale.min, 0.0);
    QCOMPARE(scale.max, 10.0);
}

void CalcSparklinesTest::themeColoursAndTint()
{
    // Without a theme part: the Office theme calc_io writes.
    const QVector<Rgb> palette = themePalette({});
    QCOMPARE(palette.size(), 12);
    QCOMPARE(palette.at(4), (Rgb{0x4F, 0x81, 0xBD})); // Accent 1
    QCOMPARE(palette.at(1), (Rgb{0, 0, 0}));          // Dark 1 (theme index 1)
    // A theme part's scheme, Excel's index order (lt1 is 0, dk1 is 1).
    const QByteArray theme = QByteArrayLiteral(
        "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:themeElements><a:clrScheme name=\"X\">"
        "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"111111\"/></a:dk1><a:lt1><a:srgbClr val=\"FEFEFE\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"44546A\"/></a:dk2><a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1><a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>"
        "</a:clrScheme></a:themeElements></a:theme>");
    const QVector<Rgb> own = themePalette(theme);
    QCOMPARE(own.at(0), (Rgb{0xFE, 0xFE, 0xFE}));
    QCOMPARE(own.at(1), (Rgb{0x11, 0x11, 0x11}));
    QCOMPARE(own.at(3), (Rgb{0x44, 0x54, 0x6A}));
    QCOMPARE(own.at(4), (Rgb{0x44, 0x72, 0xC4}));
    QCOMPARE(own.at(6), (Rgb{0x9B, 0xBB, 0x59})); // not in the part: the default's
    // Excel's tint: Accent 1 darker 50 % and lighter 40 %.
    const Rgb darker = tinted(Rgb{0x44, 0x72, 0xC4}, -0.499984740745262);
    QVERIFY(std::abs(int(darker.r) - 0x20) <= 2 && std::abs(int(darker.g) - 0x38) <= 2 && std::abs(int(darker.b) - 0x64) <= 2);
    const Rgb lighter = tinted(Rgb{0x44, 0x72, 0xC4}, 0.39997558519241921);
    QVERIFY(std::abs(int(lighter.r) - 0x8F) <= 2 && std::abs(int(lighter.g) - 0xAA) <= 2 && std::abs(int(lighter.b) - 0xDC) <= 2);
    QCOMPARE(themeColor(own, 99, 0, Rgb{1, 2, 3}), (Rgb{1, 2, 3}));
}

void CalcSparklinesTest::sparklinesFollowStructuralEdits()
{
    Workbook wb;
    sales(wb);
    wb.addSheet(QStringLiteral("Summary"));
    SparklineGroup group = defaultSparklineGroup(SparklineGroup::Type::Line);
    group.sparklines = {{at("G2"), QStringLiteral("Sheet1!B2:F2")}, {at("G3"), QStringLiteral("Sheet1!B3:F3")},
                        {at("G4"), QStringLiteral("Sheet1!B4:F4")}};
    wb.sheetAt(0).setSparklineGroups({group});
    // Data on another sheet: a sparkline on Summary follows Sheet1's edits.
    SparklineGroup elsewhere = defaultSparklineGroup(SparklineGroup::Type::Column);
    elsewhere.sparklines = {{at("A1"), QStringLiteral("Sheet1!B4:F4")}};
    wb.sheetAt(1).setSparklineGroups({elsewhere});
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 1});
    QList<SparklineGroup> moved = wb.sheetAt(0).sparklineGroups();
    QCOMPARE(moved.first().sparklines.at(0).location, at("G3"));
    QCOMPARE(moved.first().sparklines.at(0).data, QStringLiteral("Sheet1!B3:F3"));
    QCOMPARE(wb.sheetAt(1).sparklineGroups().first().sparklines.first().data, QStringLiteral("Sheet1!B5:F5"));
    QCOMPARE(wb.sheetAt(1).sparklineGroups().first().sparklines.first().location, at("A1")); // its own sheet did not move
    // Deleting a data column narrows the data; deleting a sparkline's row takes it.
    applyStructureEdit(wb, {StructureOp::RemoveColumns, 0, 2, 1}); // column C
    moved = wb.sheetAt(0).sparklineGroups();
    QCOMPARE(moved.first().sparklines.at(0).location, at("F3"));
    QCOMPARE(moved.first().sparklines.at(0).data, QStringLiteral("Sheet1!B3:E3"));
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 2, 1}); // row 3: the first sparkline and its data
    moved = wb.sheetAt(0).sparklineGroups();
    QCOMPARE(moved.first().sparklines.size(), 2);
    QCOMPARE(moved.first().sparklines.first().location, at("F3"));
    QCOMPARE(moved.first().sparklines.first().data, QStringLiteral("Sheet1!B3:E3"));
    // A renamed sheet is followed by name.
    QVERIFY(renameSheet(wb, 0, QStringLiteral("Q1 Sales")));
    QCOMPARE(wb.sheetAt(0).sparklineGroups().first().sparklines.first().data, QStringLiteral("'Q1 Sales'!B3:E3"));
    QCOMPARE(wb.sheetAt(1).sparklineGroups().first().sparklines.first().data, QStringLiteral("'Q1 Sales'!B4:E4"));
    // Its sparkline drawn in Summary reads the moved data.
    QCOMPARE(sparklineSeries(wb, wb.sheetAt(1).sparklineGroups().first(), wb.sheetAt(1).sparklineGroups().first().sparklines.first()).values,
             values({10, 30, 40, 50}));
    QCOMPARE(sparklineAt(wb.sheetAt(0), at("F3")), (SparklineRef{0, 0}));
    QVERIFY(!sparklineAt(wb.sheetAt(0), at("A1")).isValid());
}

void CalcSparklinesTest::tableNamesAsExcelChecksThem()
{
    Workbook wb;
    sales(wb);
    QString error;
    QVERIFY(createTable(wb, 0, {at("B2"), at("F4")}, &error));
    wb.defineName(QStringLiteral("Budget"), RangeRef{QStringLiteral("Sheet1"), {at("A1"), at("A1")}});
    const QString current = wb.sheetAt(0).tables().first().name;
    QVERIFY(tableNameProblem(wb, QStringLiteral("Sales_2026"), current).isEmpty());
    QVERIFY(tableNameProblem(wb, QStringLiteral("_Sales.Q1"), current).isEmpty());
    QVERIFY(tableNameProblem(wb, QStringLiteral("\\Sales"), current).isEmpty());
    QVERIFY(tableNameProblem(wb, current.toLower(), current).isEmpty()); // its own name, any case
    QVERIFY(tableNameProblem(wb, QStringLiteral("2026Sales"), current).startsWith(QStringLiteral("The name that you entered is not valid.")));
    QVERIFY(!tableNameProblem(wb, QStringLiteral("Q1 Sales"), current).isEmpty());
    QVERIFY(!tableNameProblem(wb, QStringLiteral("A1"), current).isEmpty());
    QVERIFY(!tableNameProblem(wb, QStringLiteral("R1C1"), current).isEmpty());
    QVERIFY(!tableNameProblem(wb, QStringLiteral("c"), current).isEmpty());
    QVERIFY(!tableNameProblem(wb, QString(300, QLatin1Char('a')), current).isEmpty());
    QCOMPARE(tableNameProblem(wb, QStringLiteral("budget"), current), QStringLiteral("The name entered already exists. Enter a unique name."));
}

void CalcSparklinesTest::renameTableMovesEveryReference()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    put(wb, "A1", QStringLiteral("Region"));
    put(wb, "B1", QStringLiteral("Sales"));
    put(wb, "A2", QStringLiteral("East"));
    put(wb, "B2", QStringLiteral("10"));
    put(wb, "A3", QStringLiteral("West"));
    put(wb, "B3", QStringLiteral("32"));
    QString error;
    QVERIFY(createTable(wb, 0, {at("A1"), at("B3")}, &error));
    const QString old = wb.sheetAt(0).tables().first().name;
    put(wb, "D1", QStringLiteral("=SUM(") + old + QStringLiteral("[Sales])"));
    wb.setDefinedNames({DefinedName{QStringLiteral("AllSales"), -1, old + QStringLiteral("[Sales]"), {}, false, {}}});
    put(wb, "D2", QStringLiteral("=SUM(AllSales)"));
    PivotCache cache;
    cache.id = 1;
    cache.source.name = old;
    wb.pivotCaches = {cache};
    QVERIFY(!renameTable(wb, 0, 0, QStringLiteral("bad name"), &error));
    QVERIFY(error.startsWith(QStringLiteral("The name that you entered is not valid.")));
    QCOMPARE(wb.sheetAt(0).tables().first().name, old); // nothing changed
    QVERIFY2(renameTable(wb, 0, 0, QStringLiteral("Regions"), &error), qPrintable(error));
    QCOMPARE(wb.sheetAt(0).tables().first().name, QStringLiteral("Regions"));
    QCOMPARE(wb.sheetAt(0).cellAt(at("D1"))->formula, QStringLiteral("SUM(Regions[Sales])"));
    QCOMPARE(wb.definedNames().first().formula, QStringLiteral("Regions[Sales]"));
    QCOMPARE(wb.pivotCaches.first().source.name, QStringLiteral("Regions"));
    QCOMPARE(toNumber(wb.cellValue(0, 0, 3)).value_or(-1), 42.0);
    put(wb, "B2", QStringLiteral("20")); // still live after the rename
    QCOMPARE(toNumber(wb.cellValue(0, 0, 3)).value_or(-1), 52.0);
    QCOMPARE(toNumber(wb.cellValue(0, 1, 3)).value_or(-1), 52.0);
}

void CalcSparklinesTest::resizeTableGrowsAndShrinks()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    put(wb, "A1", QStringLiteral("Item"));
    put(wb, "B1", QStringLiteral("Price"));
    put(wb, "C1", QStringLiteral("Qty"));
    for (int r = 2; r <= 3; ++r) {
        const QByteArray row = QByteArray::number(r);
        put(wb, QByteArray("A" + row).constData(), QStringLiteral("x"));
        put(wb, QByteArray("B" + row).constData(), QStringLiteral("2"));
        put(wb, QByteArray("C" + row).constData(), QStringLiteral("5"));
    }
    QString error;
    QVERIFY(createTable(wb, 0, {at("A1"), at("C3")}, &error));
    // Typed next to the table: it grows and the column is calculated (M69).
    QVERIFY(setTableAwareInput(wb, 0, at("D2"), QStringLiteral("=[@Price]*[@Qty]")));
    const QString name = wb.sheetAt(0).tables().first().name;
    put(wb, "F1", QStringLiteral("=SUM(") + name + QStringLiteral("[Qty])"));
    // Grow down two rows: the calculated column fills them.
    put(wb, "B4", QStringLiteral("3"));
    put(wb, "C4", QStringLiteral("3"));
    QVERIFY2(resizeTable(wb, 0, 0, {at("A1"), at("D5")}, &error), qPrintable(error));
    QCOMPARE(wb.sheetAt(0).tables().first().range, (CellRange{at("A1"), at("D5")}));
    QCOMPARE(toNumber(wb.cellValue(0, 3, 3)).value_or(-1), 9.0); // D4 = 3·3
    QCOMPARE(toNumber(wb.cellValue(0, 0, 5)).value_or(-1), 13.0);
    // Refusals: the header row must stay; the range must overlap; tables may not overlap.
    QVERIFY(!resizeTable(wb, 0, 0, {at("A2"), at("D5")}, &error));
    QVERIFY(error.startsWith(QStringLiteral("The headers must remain in the same row")));
    QVERIFY(!resizeTable(wb, 0, 0, {at("H1"), at("I5")}, &error));
    QCOMPARE(wb.sheetAt(0).tables().first().range, (CellRange{at("A1"), at("D5")}));
    // Shrink past the Qty column: references to it are #REF!; the data stays.
    QVERIFY(resizeTable(wb, 0, 0, {at("A1"), at("B5")}, &error));
    QCOMPARE(wb.sheetAt(0).tables().first().columnNames, (QStringList{QStringLiteral("Item"), QStringLiteral("Price")}));
    QCOMPARE(wb.sheetAt(0).cellAt(at("F1"))->formula, QStringLiteral("SUM(#REF!)"));
    QCOMPARE(toNumber(wb.cellValue(0, 1, 2)).value_or(-1), 5.0); // C2 kept its value
    // Grow right again: the new column gets a name in its header.
    QVERIFY(resizeTable(wb, 0, 0, {at("A1"), at("C5")}, &error));
    QCOMPARE(wb.sheetAt(0).tables().first().columnNames.size(), 3);
    QCOMPARE(toText(wb.cellValue(0, 0, 2)), QStringLiteral("Qty")); // the header text was there
}

void CalcSparklinesTest::keptRuleFormulasFollowEdits()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    wb.addSheet(QStringLiteral("Other"));
    KeptRule rule;
    rule.kind = KeptRule::Kind::CondFormat;
    rule.ranges = {{at("B2"), at("B9")}};
    rule.xml = QStringLiteral("<conditionalFormatting sqref=\"B2:B9\"><cfRule type=\"expression\" priority=\"1\" dxfId=\"0\">"
                              "<formula>AND(B2&gt;$D$1,_xlfn.IFS(B2&gt;0,1,TRUE,0)=1)</formula></cfRule>"
                              "<cfRule type=\"colorScale\" priority=\"2\"><colorScale><cfvo type=\"formula\" val=\"$D$1\"/>"
                              "<cfvo type=\"max\"/><color rgb=\"FFFF0000\"/><color rgb=\"FF00FF00\"/></colorScale></cfRule>"
                              "</conditionalFormatting>");
    KeptRule validation;
    validation.kind = KeptRule::Kind::ValidationX14;
    validation.ranges = {{at("C2"), at("C9")}};
    validation.xml = QStringLiteral("<x14:dataValidation type=\"list\"><x14:formula1><xm:f>Other!$A$1:$A$4</xm:f></x14:formula1>"
                                    "<xm:sqref>C2:C9</xm:sqref></x14:dataValidation>");
    KeptRule broken;
    broken.kind = KeptRule::Kind::Validation;
    broken.ranges = {{at("E2"), at("E3")}};
    broken.xml = QStringLiteral("<dataValidation type=\"custom\" sqref=\"E2:E3\"><formula1>E2 E3 (</formula1></dataValidation>");
    wb.sheetAt(0).setKeptRules({rule, validation, broken});
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2}); // two rows above everything
    const QList<KeptRule> moved = wb.sheetAt(0).keptRules();
    QVERIFY2(moved.at(0).xml.contains(QStringLiteral("<formula>AND(B4&gt;$D$3,_xlfn.IFS(B4&gt;0,1,TRUE,0)=1)</formula>")),
             qPrintable(moved.at(0).xml));
    QVERIFY(moved.at(0).xml.contains(QStringLiteral("val=\"$D$3\"")));
    QCOMPARE(moved.at(0).ranges.first(), (CellRange{at("B4"), at("B11")}));
    QVERIFY(moved.at(1).xml.contains(QStringLiteral("<xm:f>Other!$A$1:$A$4</xm:f>"))); // another sheet: unmoved
    QVERIFY(!moved.at(0).staleFormulas);
    QVERIFY(moved.at(2).staleFormulas); // a formula the engine cannot read stays and is reported
    // An edit on the other sheet moves the validation's list.
    applyStructureEdit(wb, {StructureOp::InsertRows, 1, 0, 1});
    QVERIFY(wb.sheetAt(0).keptRules().at(1).xml.contains(QStringLiteral("<xm:f>Other!$A$2:$A$5</xm:f>")));
    // Deleting the first rows of the rule shifts its anchor's formula with it.
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 3, 2}); // rows 4–5: B4:B5 go
    const KeptRule &after = wb.sheetAt(0).keptRules().at(0);
    QCOMPARE(after.ranges.first(), (CellRange{at("B4"), at("B9")}));
    QVERIFY2(after.xml.contains(QStringLiteral("<formula>AND(B4&gt;$D$3,")), qPrintable(after.xml));
}

void CalcSparklinesTest::sparklinesExtendPrintArea()
{
    Workbook book;
    sales(book);
    auto group = defaultSparklineGroup(SparklineGroup::Type::Line);
    group.sparklines.append({{20, 10}, QStringLiteral("Sheet1!B2:F2")});
    book.sheetAt(0).setSparklineGroups({group});
    const auto area = printUsedRange(book, 0);
    QCOMPARE(area.bottomRight, (CellAddr{20, 10}));
}

QTEST_GUILESS_MAIN(CalcSparklinesTest)
#include "tst_calc_sparklines.moc"
