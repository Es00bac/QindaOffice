// SPDX-License-Identifier: GPL-3.0-or-later
// Writing a sheet's data validations (xlsx_rules_internal.h, M78, D-087):
// the model's rules as Excel writes them — every kind, operator, error
// style and message, the input message, Ignore blank and the in-cell
// dropdown —, Excel 2010's x14 rules in the worksheet's extLst, and the
// rules kept as written, byte for byte unless their cells moved.
//
// AGENT-NOTE: Excel limits a validation's titles to 32 characters, its
// error message to 225 and its input message to 255, and repairs a file
// with longer ones; longer text is cut, and the save says so.

#include "xlsx_formula.h"
#include "xlsx_rules_internal.h"

#include "workbook.h"

#include <QObject>

namespace QindaOffice::Calc::IO::Rules {

namespace {

using Kind = ValidationRule::Kind;
using VOp = ValidationRule::Operator;

QString limited(const QString &text, int limit, bool *cut)
{
    if (text.size() <= limit) {
        return text;
    }
    *cut = true;
    return text.left(limit);
}

bool usesOperator(Kind kind)
{
    return kind == Kind::WholeNumber || kind == Kind::Decimal || kind == Kind::Date || kind == Kind::Time
        || kind == Kind::TextLength;
}

// The rule's formulas in the file's spelling; false when it has nothing to
// offer (a list without items or a source).
bool formulas(const ValidationRule &rule, const Workbook &wb, HiddenLists *lists, QString *first, QString *second)
{
    switch (rule.kind) {
    case Kind::List:
        if (rule.list.isEmpty() && rule.listSource.isEmpty()) {
            return false;
        }
        *first = validationListFormula(rule, wb, lists);
        return true;
    case Kind::NotEmpty:
        *first = QStringLiteral("LEN(TRIM(%1))>0").arg(addrText(rule.range.topLeft));
        return true;
    case Kind::Custom:
        *first = formulaForFile(rule.formula1);
        return !rule.formula1.isEmpty();
    case Kind::Any:
        return true;
    default:
        *first = rule.formula1.isEmpty() ? numberText(rule.min) : formulaForFile(rule.formula1);
        if (rule.op == VOp::Between || rule.op == VOp::NotBetween) {
            *second = rule.formula2.isEmpty() ? numberText(rule.max) : formulaForFile(rule.formula2);
        }
        return true;
    }
}

QString validationXml(const ValidationRule &rule, const QString &first, const QString &second, bool *cut)
{
    QString out = rule.x14 ? QStringLiteral("<x14:dataValidation") : QStringLiteral("<dataValidation");
    if (rule.kind != Kind::Any) {
        out += QStringLiteral(" type=\"%1\"").arg(validationTypeName(rule.kind));
    }
    if (rule.style == ValidationRule::Style::Warning) {
        out += QStringLiteral(" errorStyle=\"warning\"");
    } else if (rule.style == ValidationRule::Style::Information) {
        out += QStringLiteral(" errorStyle=\"information\"");
    }
    if (usesOperator(rule.kind) && rule.op != VOp::Between) {
        out += QStringLiteral(" operator=\"%1\"").arg(validationOperatorName(rule.op));
    }
    if (rule.allowBlank) {
        out += QStringLiteral(" allowBlank=\"1\"");
    }
    if (rule.kind == Kind::List && !rule.inCellDropdown) {
        out += QStringLiteral(" showDropDown=\"1\""); // Excel's inverted name: hide the dropdown
    }
    if (rule.showInput) {
        out += QStringLiteral(" showInputMessage=\"1\"");
    }
    if (rule.showError) {
        out += QStringLiteral(" showErrorMessage=\"1\"");
    }
    const auto text = [&out, cut](const char *name, const QString &value, int limit) {
        if (!value.isEmpty()) {
            out += QStringLiteral(" %1=\"%2\"").arg(QLatin1String(name), attrEscaped(limited(value, limit, cut)));
        }
    };
    text("errorTitle", rule.errorTitle, 32);
    text("error", rule.message, 225);
    text("promptTitle", rule.promptTitle, 32);
    text("prompt", rule.prompt, 255);
    const QString sqref = sqrefText(QList<CellRange>{rule.range} + rule.moreRanges);
    if (!rule.x14) {
        out += QStringLiteral(" sqref=\"%1\">").arg(sqref);
        if (!first.isEmpty()) {
            out += QStringLiteral("<formula1>") + xmlEscaped(first) + QStringLiteral("</formula1>");
        }
        if (!second.isEmpty()) {
            out += QStringLiteral("<formula2>") + xmlEscaped(second) + QStringLiteral("</formula2>");
        }
        return out + QStringLiteral("</dataValidation>");
    }
    out += QLatin1Char('>');
    if (!first.isEmpty()) {
        out += QStringLiteral("<x14:formula1><xm:f>") + xmlEscaped(first) + QStringLiteral("</xm:f></x14:formula1>");
    }
    if (!second.isEmpty()) {
        out += QStringLiteral("<x14:formula2><xm:f>") + xmlEscaped(second) + QStringLiteral("</xm:f></x14:formula2>");
    }
    return out + QStringLiteral("<xm:sqref>") + sqref + QStringLiteral("</xm:sqref></x14:dataValidation>");
}

} // namespace

void fillValidationPatch(const Workbook &wb, const Sheet &sheet, HiddenLists *lists, PatchManifest::SheetPatch *patch,
                         QStringList *warnings)
{
    QString main;
    QString x14;
    int mainCount = 0;
    int x14Count = 0;
    bool cut = false;
    for (const ValidationRule &rule : sheet.validations()) {
        QString first;
        QString second;
        if (!formulas(rule, wb, lists, &first, &second)) {
            continue;
        }
        (rule.x14 ? x14 : main) += validationXml(rule, first, second, &cut);
        ++(rule.x14 ? x14Count : mainCount);
    }
    for (const KeptRule &rule : sheet.keptRules()) {
        if (rule.kind == KeptRule::Kind::Validation) {
            main += withRanges(rule.xml, rule.ranges, false);
            ++mainCount;
        } else if (rule.kind == KeptRule::Kind::ValidationX14) {
            x14 += withRanges(rule.xml, rule.ranges, true);
            ++x14Count;
        }
    }
    if (mainCount > 0) {
        patch->dataValidationsXml = (QStringLiteral("<dataValidations count=\"%1\">").arg(mainCount) + main
                                     + QStringLiteral("</dataValidations>"))
                                        .toUtf8();
    }
    if (x14Count > 0) {
        patch->extXml += (QStringLiteral("<ext uri=\"%1\" xmlns:x14=\"%2\"><x14:dataValidations count=\"%3\" xmlns:xm=\"%4\">")
                              .arg(QString::fromUtf16(kDvExtUri), QString::fromUtf16(kX14Ns))
                              .arg(x14Count)
                              .arg(QString::fromUtf16(kXmNs))
                          + x14 + QStringLiteral("</x14:dataValidations></ext>"))
                             .toUtf8();
    }
    if (cut && warnings != nullptr) {
        const QString message = QObject::tr("A data validation message on “%1” was cut to Excel's length limit.")
                                    .arg(sheet.name());
        if (!warnings->contains(message)) {
            warnings->append(message);
        }
    }
}

} // namespace QindaOffice::Calc::IO::Rules
