// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// M78 (D-087): Excel-shaped worksheets for the conditional-format and
// validation gates (tst_calc_io_kept), written the way Excel 2010 and later
// write them — 2007 <conditionalFormatting> blocks with their priorities and
// dxfIds, x14 rules in the worksheet extLst, a data bar and its x14 twin
// tied by {GUID}, the dxf table in xl/styles.xml. Each element the model
// cannot hold is a constant, so a test can look for its exact text in the
// saved part.

#include "excel_package.h"

#include "sheet_rules.h"

namespace QindaOffice::Test::Kept {

inline const char kX14[] = "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main";
inline const char kXm[] = "http://schemas.microsoft.com/office/excel/2006/main";

// The three dxfs, as Excel writes them: a red highlight, a green one, and
// one with a number format, a theme-coloured fill and a border.
inline QByteArray dxfs()
{
    return QByteArrayLiteral(
        "<dxfs count=\"3\">"
        "<dxf><font><b/><color rgb=\"FF9C0006\"/></font><fill><patternFill><bgColor rgb=\"FFFFC7CE\"/></patternFill></fill></dxf>"
        "<dxf><font><color rgb=\"FF006100\"/></font><fill><patternFill><bgColor rgb=\"FFC6EFCE\"/></patternFill></fill></dxf>"
        "<dxf><numFmt numFmtId=\"164\" formatCode=\"0.0%\"/><fill><patternFill><bgColor theme=\"4\" tint=\"0.79998168889431442\"/>"
        "</patternFill></fill><border><left style=\"thin\"><color auto=\"1\"/></left></border></dxf>"
        "</dxfs>");
}

inline QByteArray stylesWithDxfs()
{
    QByteArray styles = ExcelWorkbook::defaultStyles();
    styles.replace("</styleSheet>", dxfs() + "</styleSheet>");
    return styles;
}

// A rule whose formula the engine cannot parse (the space intersection
// operator), in a block with one it models.
inline const char kKeptExpression[] =
    "<cfRule type=\"expression\" dxfId=\"1\" priority=\"2\"><formula>SUM(A1:A3 A2:B2)&gt;0</formula></cfRule>";
// A block the model keeps whole: its only rule is out of reach too.
inline const char kKeptBlock[] =
    "<conditionalFormatting sqref=\"J1:J10\"><cfRule type=\"expression\" dxfId=\"1\" priority=\"6\">"
    "<formula>SUM(J1:J3 J2:K2)&gt;1</formula></cfRule></conditionalFormatting>";
// Excel 2010's icon set with an icon of its own per threshold.
inline QByteArray keptX14Block()
{
    return QByteArrayLiteral("<x14:conditionalFormatting xmlns:xm=\"") + kXm
        + QByteArrayLiteral("\"><x14:cfRule type=\"iconSet\" priority=\"4\" id=\"{AAAAAAAA-0000-4000-8000-000000000001}\">"
                            "<x14:iconSet iconSet=\"3Triangles\" custom=\"1\"><x14:cfvo type=\"percent\"><xm:f>0</xm:f></x14:cfvo>"
                            "<x14:cfvo type=\"percent\"><xm:f>33</xm:f></x14:cfvo><x14:cfvo type=\"percent\"><xm:f>67</xm:f></x14:cfvo>"
                            "<x14:cfIcon iconSet=\"3Flags\" iconId=\"0\"/><x14:cfIcon iconSet=\"3Flags\" iconId=\"1\"/>"
                            "<x14:cfIcon iconSet=\"3Flags\" iconId=\"2\"/></x14:iconSet></x14:cfRule><xm:sqref>G1:G10</xm:sqref>"
                            "</x14:conditionalFormatting>");
}
// Validations with an East Asian input mode, which the model has no place for.
inline const char kKeptValidation[] =
    "<dataValidation type=\"whole\" imeMode=\"hiragana\" allowBlank=\"1\" showErrorMessage=\"1\" sqref=\"E1:E5\">"
    "<formula1>1</formula1><formula2>9</formula2></dataValidation>";
inline const char kKeptX14Validation[] =
    "<x14:dataValidation type=\"list\" imeMode=\"off\" allowBlank=\"1\" showErrorMessage=\"1\">"
    "<x14:formula1><xm:f>Lists!$A$1:$A$3</xm:f></x14:formula1><xm:sqref>I1:I5</xm:sqref></x14:dataValidation>";

// Sheet "Data": modelled and kept rules side by side, priorities 1–6.
//   A1:A10  cellIs > 5 (dxf 0, p1, modelled) + kKeptExpression (dxf 1, p2)
//   C1:C10  cellIs < 0 (dxf 2, p3, modelled)
//   G1:G10  x14 custom icons (p4, kept)
//   D1:D10  3Arrows icon set (p5, modelled)
//   J1:J10  kKeptBlock (p6, kept)
//   E1:E5   kKeptValidation; F1:F5 decimal > 0 with an input message (modelled)
//   H1:H5   x14 list from another sheet (modelled); I1:I5 kKeptX14Validation
inline ExcelWorkbook mixedBook()
{
    ExcelWorkbook book;
    QByteArray rows;
    for (int r = 1; r <= 10; ++r) {
        rows += QByteArrayLiteral("<row r=\"") + QByteArray::number(r) + QByteArrayLiteral("\"><c r=\"A")
            + QByteArray::number(r) + QByteArrayLiteral("\"><v>") + QByteArray::number(r) + QByteArrayLiteral("</v></c></row>");
    }
    const QByteArray after = QByteArrayLiteral("<conditionalFormatting sqref=\"A1:A10\">"
                                               "<cfRule type=\"cellIs\" dxfId=\"0\" priority=\"1\" operator=\"greaterThan\">"
                                               "<formula>5</formula></cfRule>")
        + kKeptExpression
        + QByteArrayLiteral("</conditionalFormatting><conditionalFormatting sqref=\"C1:C10\">"
                            "<cfRule type=\"cellIs\" dxfId=\"2\" priority=\"3\" operator=\"lessThan\"><formula>0</formula></cfRule>"
                            "</conditionalFormatting><conditionalFormatting sqref=\"D1:D10\"><cfRule type=\"iconSet\" priority=\"5\">"
                            "<iconSet iconSet=\"3Arrows\"><cfvo type=\"percent\" val=\"0\"/><cfvo type=\"percent\" val=\"33\"/>"
                            "<cfvo type=\"percent\" val=\"67\"/></iconSet></cfRule></conditionalFormatting>")
        + kKeptBlock
        + QByteArrayLiteral("<dataValidations count=\"2\">") + kKeptValidation
        + QByteArrayLiteral("<dataValidation type=\"decimal\" operator=\"greaterThan\" allowBlank=\"1\" showInputMessage=\"1\" "
                            "showErrorMessage=\"1\" promptTitle=\"Rate\" prompt=\"Above zero\" sqref=\"F1:F5\">"
                            "<formula1>0</formula1></dataValidation></dataValidations>"
                            "<extLst><ext uri=\"{78C0D931-6437-407d-A8EE-F0AAD7539E65}\" xmlns:x14=\"")
        + kX14 + QByteArrayLiteral("\"><x14:conditionalFormattings>") + keptX14Block()
        + QByteArrayLiteral("</x14:conditionalFormattings></ext><ext uri=\"{CCE6A557-97BC-4b89-ADB6-D9C93CAAB3DF}\" xmlns:x14=\"")
        + kX14 + QByteArrayLiteral("\"><x14:dataValidations count=\"2\" xmlns:xm=\"") + kXm
        + QByteArrayLiteral("\"><x14:dataValidation type=\"list\" allowBlank=\"1\" showInputMessage=\"1\" showErrorMessage=\"1\">"
                            "<x14:formula1><xm:f>Lists!$A$1:$A$3</xm:f></x14:formula1><xm:sqref>H1:H5</xm:sqref>"
                            "</x14:dataValidation>")
        + kKeptX14Validation + QByteArrayLiteral("</x14:dataValidations></ext></extLst>");
    book.sheets.append(ExcelSheet{QStringLiteral("Data"), rows, after, {}});
    book.sheets.append(ExcelSheet{QStringLiteral("Lists"),
                                  QByteArrayLiteral("<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>Red</t></is></c></row>"
                                                    "<row r=\"2\"><c r=\"A2\" t=\"inlineStr\"><is><t>Green</t></is></c></row>"
                                                    "<row r=\"3\"><c r=\"A3\" t=\"inlineStr\"><is><t>Blue</t></is></c></row>"),
                                  {}, {}});
    book.stylesXml = stylesWithDxfs();
    return book;
}

// Sheet "Bars": an Excel 2010 data bar — the 2007 rule and its x14 twin.
inline ExcelWorkbook dataBarBook()
{
    ExcelWorkbook book;
    QByteArray rows;
    const int values[] = {-50, 0, 50, 100};
    for (int r = 1; r <= 4; ++r) {
        rows += QByteArrayLiteral("<row r=\"") + QByteArray::number(r) + QByteArrayLiteral("\"><c r=\"B")
            + QByteArray::number(r) + QByteArrayLiteral("\"><v>") + QByteArray::number(values[r - 1])
            + QByteArrayLiteral("</v></c></row>");
    }
    const QByteArray after =
        QByteArrayLiteral("<conditionalFormatting sqref=\"B1:B4\"><cfRule type=\"dataBar\" priority=\"1\"><dataBar>"
                          "<cfvo type=\"min\"/><cfvo type=\"max\"/><color rgb=\"FF638EC6\"/></dataBar><extLst>"
                          "<ext uri=\"{B025F937-C7B1-47D3-B67F-A62EFF666E3E}\" xmlns:x14=\"")
        + kX14
        + QByteArrayLiteral("\"><x14:id>{11111111-2222-4333-8444-555555555555}</x14:id></ext></extLst></cfRule>"
                            "</conditionalFormatting><extLst><ext uri=\"{78C0D931-6437-407d-A8EE-F0AAD7539E65}\" xmlns:x14=\"")
        + kX14 + QByteArrayLiteral("\"><x14:conditionalFormattings><x14:conditionalFormatting xmlns:xm=\"") + kXm
        + QByteArrayLiteral("\"><x14:cfRule type=\"dataBar\" id=\"{11111111-2222-4333-8444-555555555555}\">"
                            "<x14:dataBar minLength=\"0\" maxLength=\"100\" border=\"1\" gradient=\"0\" "
                            "negativeBarBorderColorSameAsPositive=\"0\" axisPosition=\"middle\">"
                            "<x14:cfvo type=\"autoMin\"/><x14:cfvo type=\"autoMax\"/><x14:borderColor rgb=\"FF638EC6\"/>"
                            "<x14:negativeFillColor rgb=\"FFFF0000\"/><x14:negativeBorderColor rgb=\"FFFF0000\"/>"
                            "<x14:axisColor rgb=\"FF000000\"/></x14:dataBar></x14:cfRule><xm:sqref>B1:B4</xm:sqref>"
                            "</x14:conditionalFormatting></x14:conditionalFormattings></ext></extLst>");
    book.sheets.append(ExcelSheet{QStringLiteral("Bars"), rows, after, {}});
    return book;
}


// One rule of every kind the model holds, over A1:A6 (a duplicate rule
// over A1:A6 and C1:C6), each with Excel's markup to look for in the saved
// part (tst_calc_io_kept).
inline QList<Calc::CondFormatRule> everyKind()
{
    using namespace Calc;
    using RuleOp = CondFormatRule::Op;
    const CellRange a{{0, 0}, {5, 0}};
    QList<CondFormatRule> rules;
    const auto add = [&rules, &a](RuleOp op) -> CondFormatRule & {
        CondFormatRule rule;
        rule.op = op;
        rule.range = a;
        rule.format.background = Rgb{0xFF, 0xEB, 0x9C};
        rules.append(rule);
        return rules.last();
    };
    add(RuleOp::NotBetween).first = 5;
    rules.last().second = 25;
    add(RuleOp::EqualTo).text = QStringLiteral("Yes");
    add(RuleOp::GreaterThan).formula = QStringLiteral("$C$1");
    add(RuleOp::Expression).formula = QStringLiteral("MOD($A1,20)=0");
    add(RuleOp::ContainsText).text = QStringLiteral("a\"b");
    add(RuleOp::NotContainsText).text = QStringLiteral("x");
    add(RuleOp::BeginsWith).text = QStringLiteral("Q");
    add(RuleOp::EndsWith).text = QStringLiteral("z");
    add(RuleOp::IsEmpty);
    add(RuleOp::NotEmpty);
    add(RuleOp::ContainsErrors);
    add(RuleOp::NotContainsErrors);
    CondFormatRule &top = add(RuleOp::Top10);
    top.rank = 3;
    top.percent = true;
    top.bottom = true;
    CondFormatRule &average = add(RuleOp::AboveAverage);
    average.aboveAverage = false;
    average.equalAverage = true;
    average.stdDev = 1;
    CondFormatRule &dup = add(RuleOp::Duplicate);
    dup.moreRanges = {{{0, 2}, {5, 2}}};
    add(RuleOp::Unique);
    add(RuleOp::TimePeriod).period = CondFormatRule::Period::LastWeek;
    rules.last().format = CondFormat{};
    rules.last().format.bold = true;
    rules.last().format.italic = true;
    rules.last().format.underline = true;
    rules.last().format.strikeout = true;
    rules.last().format.foreground = Rgb{0x9C, 0x00, 0x06};
    CondFormatRule &scale = add(RuleOp::ColorScale);
    scale.format = CondFormat{};
    scale.thresholds = {CfThreshold{CfThreshold::Type::Min, {}, true},
                        CfThreshold{CfThreshold::Type::Percentile, QStringLiteral("50"), true},
                        CfThreshold{CfThreshold::Type::Number, QStringLiteral("100"), true}};
    scale.colors = {Rgb{0xF8, 0x69, 0x6B}, Rgb{0xFF, 0xEB, 0x84}, Rgb{0x63, 0xBE, 0x7B}};
    CondFormatRule &bar2007 = add(RuleOp::DataBar);
    bar2007.format = CondFormat{};
    bar2007.thresholds = {CfThreshold{CfThreshold::Type::Min, {}, true}, CfThreshold{CfThreshold::Type::Max, {}, true}};
    bar2007.barColor = Rgb{0x5A, 0x8A, 0xC6};
    bar2007.showValue = false;
    CondFormatRule &bar2010 = add(RuleOp::DataBar);
    bar2010.format = CondFormat{};
    bar2010.thresholds = {CfThreshold{CfThreshold::Type::AutoMin, {}, true},
                          CfThreshold{CfThreshold::Type::Formula, QStringLiteral("$C$2"), true}};
    bar2010.bar.x14 = true;
    bar2010.bar.minLength = 0;
    bar2010.bar.maxLength = 100;
    bar2010.bar.direction = DataBarStyle::Direction::RightToLeft;
    bar2010.bar.negativeSameAsPositive = true;
    CondFormatRule &arrows = add(RuleOp::IconSet);
    arrows.format = CondFormat{};
    arrows.iconSet = QStringLiteral("4Rating");
    arrows.reverse = true;
    arrows.thresholds = {CfThreshold{CfThreshold::Type::Percent, QStringLiteral("0"), true},
                         CfThreshold{CfThreshold::Type::Percent, QStringLiteral("25"), false},
                         CfThreshold{CfThreshold::Type::Number, QStringLiteral("30"), true},
                         CfThreshold{CfThreshold::Type::Percentile, QStringLiteral("75"), true}};
    CondFormatRule &stars = add(RuleOp::IconSet);
    stars.format = CondFormat{};
    stars.iconSet = QStringLiteral("3Stars");
    stars.showValue = false;
    stars.thresholds = {CfThreshold{CfThreshold::Type::Percent, QStringLiteral("0"), true},
                        CfThreshold{CfThreshold::Type::Percent, QStringLiteral("33"), true},
                        CfThreshold{CfThreshold::Type::Percent, QStringLiteral("67"), true}};
    return rules;
}

// One validation of every kind, a column each (J and L: an Excel 2010 list
// from another sheet).
inline QList<Calc::ValidationRule> everyValidation()
{
    using namespace Calc;
    using Kind = ValidationRule::Kind;
    using VOp = ValidationRule::Operator;
    QList<ValidationRule> rules;
    const auto add = [&rules](Kind kind, qint32 col) -> ValidationRule & {
        ValidationRule rule;
        rule.kind = kind;
        rule.range = {{0, col}, {9, col}};
        rules.append(rule);
        return rules.last();
    };
    ValidationRule &whole = add(Kind::WholeNumber, 0);
    whole.min = 1;
    whole.max = 10;
    ValidationRule &decimal = add(Kind::Decimal, 1);
    decimal.op = VOp::NotBetween;
    decimal.min = 0.5;
    decimal.max = 1.5;
    decimal.style = ValidationRule::Style::Warning;
    decimal.errorTitle = QStringLiteral("Careful");
    decimal.message = QStringLiteral("Outside 0.5–1.5 \"please\"");
    ValidationRule &date = add(Kind::Date, 2);
    date.op = VOp::GreaterThan;
    date.formula1 = QStringLiteral("TODAY()");
    ValidationRule &time = add(Kind::Time, 3);
    time.min = 0.375;
    time.max = 0.75;
    time.style = ValidationRule::Style::Information;
    ValidationRule &length = add(Kind::TextLength, 4);
    length.op = VOp::LessThanOrEqual;
    length.min = 5;
    ValidationRule &list = add(Kind::List, 5);
    list.list = {QStringLiteral("Low"), QStringLiteral("High")};
    list.inCellDropdown = false;
    ValidationRule &custom = add(Kind::Custom, 6);
    custom.formula1 = QStringLiteral("ISNUMBER(G1)");
    custom.allowBlank = false;
    ValidationRule &any = add(Kind::Any, 7);
    any.promptTitle = QStringLiteral("Note");
    any.prompt = QStringLiteral("Anything goes");
    any.showError = false;
    ValidationRule &required = add(Kind::NotEmpty, 8);
    required.moreRanges = {};
    ValidationRule &region = add(Kind::List, 9);
    region.listSource = QStringLiteral("Lists!$A$1:$A$2");
    region.x14 = true;
    region.moreRanges = {{{0, 11}, {9, 11}}};
    return rules;
}

} // namespace QindaOffice::Test::Kept
