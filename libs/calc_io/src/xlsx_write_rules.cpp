// SPDX-License-Identifier: GPL-3.0-or-later
// Data validations and conditional formats through QXlsx's public model
// (xlsx_write_rules.h; moved out of xlsx_write.cpp in M61, D-069, when long
// validation lists learned to become range references).

#include "xlsx_write_rules.h"

#include "xlsx_formula.h"

#include "workbook.h"

#include <xlsxconditionalformatting.h>
#include <xlsxdatavalidation.h>
#include <xlsxformat.h>
#include <xlsxworksheet.h>

namespace QindaOffice::Calc::IO {

namespace {

constexpr qsizetype kInlineListLimit = 255; // Excel's limit on an inline list

// The dxf for a conditional format. AGENT-NOTE: a dxf's visible fill is the
// file's bgColor element (Excel's dxf convention — the reverse of a cell
// fill), and QXlsx's solid-fill swap writes P_Fill_FgColor AS bgColor, so
// the foreground setter is the right one here and ONLY here.
QXlsx::Format dxfFromCondFormat(const CondFormat &cf)
{
    QXlsx::Format format;
    if (cf.bold.has_value()) {
        format.setFontBold(*cf.bold);
    }
    if (cf.italic.has_value()) {
        format.setFontItalic(*cf.italic);
    }
    if (cf.foreground.has_value()) {
        format.setFontColor(QColor(cf.foreground->r, cf.foreground->g, cf.foreground->b));
    }
    if (cf.background.has_value()) {
        format.setFillPattern(QXlsx::Format::PatternSolid);
        format.setPatternForegroundColor(
            QColor(cf.background->r, cf.background->g, cf.background->b));
    }
    return format;
}

// A range of `wb` holding exactly `items`, top to bottom or left to right,
// as an absolute reference ("Lists!$A$1:$A$40"); empty when none does.
QString existingListRange(const Workbook &wb, const QStringList &items)
{
    const auto textAt = [&wb](qint32 sheet, qint32 row, qint32 col) {
        return toText(wb.cellValue(sheet, row, col));
    };
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        for (auto it = wb.sheetAt(s).cells().constBegin(); it != wb.sheetAt(s).cells().constEnd(); ++it) {
            if (toText(it.value().value) != items.first()) {
                continue;
            }
            const CellAddr at{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
            for (const bool down : {true, false}) {
                bool all = true;
                for (qsizetype i = 1; i < items.size() && all; ++i) {
                    all = textAt(s, at.row + (down ? qint32(i) : 0), at.col + (down ? 0 : qint32(i))) == items.at(i);
                }
                if (!all) {
                    continue;
                }
                const qint32 last = qint32(items.size()) - 1;
                const CellAddr end{at.row + (down ? last : 0), at.col + (down ? 0 : last)};
                return quotedSheetName(wb.sheetName(s)) + QLatin1Char('!') + addrText(at, true, true)
                    + QLatin1Char(':') + addrText(end, true, true);
            }
        }
    }
    return {};
}

} // namespace

// The formula1 of a list validation: the live source it was read from, the
// inline list when Excel can take it, a range of the workbook, or a column
// of the hidden list sheet.
QString validationListFormula(const ValidationRule &rule, const Workbook &wb, HiddenLists *lists)
{
    if (!rule.listSource.isEmpty()) {
        return formulaForFile(rule.listSource);
    }
    QStringList escaped = rule.list;
    bool commas = false;
    for (QString &item : escaped) {
        commas = commas || item.contains(QLatin1Char(','));
        item.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    }
    const QString joined = escaped.join(QLatin1Char(','));
    if (!commas && joined.size() <= kInlineListLimit) {
        return QStringLiteral("\"") + joined + QStringLiteral("\"");
    }
    if (const QString existing = existingListRange(wb, rule.list); !existing.isEmpty()) {
        return existing;
    }
    lists->columns.append(rule.list);
    const qint32 col = qint32(lists->columns.size()) - 1;
    return quotedSheetName(lists->sheetName) + QLatin1Char('!') + addrText({0, col}, true, true)
        + QLatin1Char(':') + addrText({qint32(rule.list.size()) - 1, col}, true, true);
}

QString hiddenListsSheetName(const Workbook &wb)
{
    QString name = QStringLiteral("QindaLists");
    for (int n = 2; wb.sheetIndex(name) >= 0; ++n) {
        name = QStringLiteral("QindaLists%1").arg(n);
    }
    return name;
}

void writeValidations(QXlsx::Worksheet *worksheet, const Workbook &wb, const Sheet &sheet,
                      HiddenLists *lists)
{
    for (const ValidationRule &rule : sheet.validations()) {
        QXlsx::DataValidation validation;
        switch (rule.kind) {
        case ValidationRule::Kind::List: {
            if (rule.list.isEmpty() && rule.listSource.isEmpty()) {
                continue; // nothing to offer: no valid list validation exists
            }
            validation.setValidationType(QXlsx::DataValidation::List);
            validation.setFormula1(validationListFormula(rule, wb, lists));
            break;
        }
        case ValidationRule::Kind::WholeNumber:
            validation.setValidationType(QXlsx::DataValidation::Whole);
            validation.setValidationOperator(QXlsx::DataValidation::Between);
            validation.setFormula1(QString::number(rule.min));
            validation.setFormula2(QString::number(rule.max));
            break;
        case ValidationRule::Kind::Decimal:
            validation.setValidationType(QXlsx::DataValidation::Decimal);
            validation.setValidationOperator(QXlsx::DataValidation::Between);
            validation.setFormula1(QString::number(rule.min));
            validation.setFormula2(QString::number(rule.max));
            break;
        case ValidationRule::Kind::TextLength:
            validation.setValidationType(QXlsx::DataValidation::TextLength);
            validation.setValidationOperator(QXlsx::DataValidation::Between);
            validation.setFormula1(QString::number(rule.min));
            validation.setFormula2(QString::number(rule.max));
            break;
        case ValidationRule::Kind::NotEmpty:
            // Excel has no "not empty" validation; the idiom is a custom
            // formula over the range's first cell.
            validation.setValidationType(QXlsx::DataValidation::Custom);
            validation.setFormula1(QStringLiteral("LEN(TRIM(%1))>0")
                                       .arg(addrText(rule.range.topLeft)));
            break;
        case ValidationRule::Kind::Custom:
            validation.setValidationType(QXlsx::DataValidation::Custom);
            validation.setFormula1(formulaForFile(rule.formula1));
            break;
        case ValidationRule::Kind::Date:
        case ValidationRule::Kind::Time:
        case ValidationRule::Kind::Any:
            // M78 (D-087): the post-pass writes these (xlsx_rules.h); the
            // QXlsx-only path has no file to keep them in.
            continue;
        }
        if (!rule.message.isEmpty()) {
            validation.setErrorMessage(rule.message);
        }
        validation.setErrorStyle(QXlsx::DataValidation::Stop);
        validation.addRange(QXlsx::CellRange(rule.range.topLeft.row + 1,
                                             rule.range.topLeft.col + 1,
                                             rule.range.bottomRight.row + 1,
                                             rule.range.bottomRight.col + 1));
        worksheet->addDataValidation(validation);
    }
}

void writeCondFormats(QXlsx::Worksheet *worksheet, const Sheet &sheet)
{
    using Op = CondFormatRule::Op;
    for (const CondFormatRule &rule : sheet.condFormats()) {
        QXlsx::ConditionalFormatting cf;
        const QXlsx::Format dxf = dxfFromCondFormat(rule.format);
        const QString first = QString::number(rule.first);
        const QString second = QString::number(rule.second);
        bool added = true;
        switch (rule.op) {
        case Op::GreaterThan:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_GreaterThan, first, dxf, rule.stopIfTrue);
            break;
        case Op::GreaterThanOrEqual:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_GreaterThanOrEqual, first, dxf, rule.stopIfTrue);
            break;
        case Op::LessThan:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_LessThan, first, dxf, rule.stopIfTrue);
            break;
        case Op::LessThanOrEqual:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_LessThanOrEqual, first, dxf, rule.stopIfTrue);
            break;
        case Op::Between:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Between, first, second, dxf, rule.stopIfTrue);
            break;
        case Op::EqualTo:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Equal, first, dxf, rule.stopIfTrue);
            break;
        case Op::NotEqualTo:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_NotEqual, first, dxf, rule.stopIfTrue);
            break;
        case Op::ContainsText:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_ContainsText, rule.text, dxf, rule.stopIfTrue);
            break;
        case Op::IsEmpty:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Blanks, dxf, rule.stopIfTrue);
            break;
        case Op::NotEmpty:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_NoBlanks, dxf, rule.stopIfTrue);
            break;
        case Op::Expression:
            cf.addHighlightCellsRule(QXlsx::ConditionalFormatting::Highlight_Expression,
                                     formulaForFile(rule.formula), dxf, rule.stopIfTrue);
            break;
        case Op::DataBar:
            cf.addDataBarRule(QColor(rule.barColor.r, rule.barColor.g, rule.barColor.b),
                              true, rule.stopIfTrue);
            break;
        default:
            // M78 (D-087): every other kind is the post-pass's
            // (xlsx_rules.h); the QXlsx-only path cannot spell them.
            added = false;
            break;
        }
        if (!added) {
            continue;
        }
        cf.addRange(QXlsx::CellRange(rule.range.topLeft.row + 1, rule.range.topLeft.col + 1,
                                     rule.range.bottomRight.row + 1,
                                     rule.range.bottomRight.col + 1));
        worksheet->addConditionalFormatting(cf);
    }
}

} // namespace QindaOffice::Calc::IO
