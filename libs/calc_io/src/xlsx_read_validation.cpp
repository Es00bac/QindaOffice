// SPDX-License-Identifier: GPL-3.0-or-later
// Reading data validations, modelled and kept (xlsx_rules_internal.h, M78,
// D-087): every <dataValidation> and Excel 2010's x14:dataValidation into
// ValidationRule with all of Excel's settings — type, operator, operands as
// numbers or formulas, error style and messages, the input message, Ignore
// blank, the in-cell dropdown —, and anything else (an IME mode, a kind or
// a formula the model cannot hold) into Sheet::keptRules as written.
//
// AGENT-NOTE: Excel writes the attributes it leaves at their default out
// (showErrorMessage absent is FALSE: such a rule refuses nothing in
// Excel); the reader takes the file's values, not ValidationRule's defaults,
// which are those of a rule made in QindaCalc.

#include "xlsx_formula.h"
#include "xlsx_rules_internal.h"

#include "sheet.h"

#include <QRegularExpression>

namespace QindaOffice::Calc::IO::Rules {

namespace {

using Kind = ValidationRule::Kind;
using VOp = ValidationRule::Operator;

// An inline list's items ("a,b,c" with doubled quotes for a quote).
QStringList inlineItems(const QString &formula)
{
    QString body = formula.mid(1);
    if (body.endsWith(QLatin1Char('"'))) {
        body.chop(1);
    }
    body.replace(QStringLiteral("\"\""), QStringLiteral("\x01"));
    QStringList items;
    for (QString item : body.split(QLatin1Char(','))) {
        items.append(item.replace(QStringLiteral("\x01"), QStringLiteral("\"")));
    }
    return items;
}

// An operand: a number, or a formula in the engine's spelling.
void setOperand(ValidationRule *rule, bool second, const QString &raw)
{
    QString text = raw.trimmed();
    bool number = false;
    const double value = text.toDouble(&number);
    if (number) {
        (second ? rule->max : rule->min) = value;
        return;
    }
    if (text.startsWith(QLatin1Char('='))) {
        text.remove(0, 1);
    }
    (second ? rule->formula2 : rule->formula1) = formulaFromFile(text);
}

// QindaCalc's own "not empty" rule is Excel's custom LEN(TRIM(cell))>0
// over its first cell; read back as what it was made as.
bool isNotEmptyFormula(const QString &formula, const CellRange &range)
{
    QString compact = formula;
    compact.remove(QLatin1Char(' '));
    return compact.compare(QStringLiteral("LEN(TRIM(%1))>0").arg(addrText(range.topLeft)), Qt::CaseInsensitive) == 0;
}

// A validation into the model, or nullopt when it must be kept.
std::optional<ValidationRule> modelValidation(const RawNode &node, bool x14)
{
    if (!onlyKnownAttributes(node, {"type", "errorStyle", "imeMode", "operator", "allowBlank", "showDropDown",
                                    "showInputMessage", "showErrorMessage", "errorTitle", "error", "promptTitle",
                                    "prompt", "sqref"})) {
        return std::nullopt;
    }
    const QString ime = node.attr(u"imeMode");
    if (!ime.isEmpty() && ime != QLatin1String("noControl")) {
        return std::nullopt; // an East Asian input mode: not modelled
    }
    QString sqref = x14 ? QString() : node.attr(u"sqref");
    QString formula1;
    QString formula2;
    bool hasFormula1 = false;
    bool hasFormula2 = false;
    for (const RawNode &child : node.children) {
        QString text = child.text;
        if (x14 && child.name != QLatin1String("sqref")) {
            const RawNode *f = child.child(u"f");
            if (f == nullptr || child.children.size() != 1) {
                return std::nullopt;
            }
            text = f->text;
        }
        if (child.name == QLatin1String("formula1")) {
            formula1 = text;
            hasFormula1 = true;
        } else if (child.name == QLatin1String("formula2")) {
            formula2 = text;
            hasFormula2 = true;
        } else if (x14 && child.name == QLatin1String("sqref")) {
            sqref = child.text.trimmed();
        } else {
            return std::nullopt;
        }
    }
    const QList<CellRange> ranges = parseSqref(sqref);
    const std::optional<Kind> kind = validationKind(node.attr(u"type"));
    const std::optional<VOp> op = validationOperator(node.attr(u"operator"));
    const QString style = node.attr(u"errorStyle");
    if (ranges.isEmpty() || !kind.has_value() || !op.has_value()
        || (!style.isEmpty() && style != QLatin1String("stop") && style != QLatin1String("warning")
            && style != QLatin1String("information"))) {
        return std::nullopt;
    }
    ValidationRule rule;
    rule.range = ranges.first();
    rule.moreRanges = ranges.mid(1);
    rule.kind = *kind;
    rule.op = *op;
    rule.style = style == QLatin1String("warning")       ? ValidationRule::Style::Warning
                 : style == QLatin1String("information") ? ValidationRule::Style::Information
                                                         : ValidationRule::Style::Stop;
    rule.errorTitle = node.attr(u"errorTitle");
    rule.message = node.attr(u"error");
    rule.promptTitle = node.attr(u"promptTitle");
    rule.prompt = node.attr(u"prompt");
    rule.showInput = boolAttr(node.attr(u"showInputMessage"), false);
    rule.showError = boolAttr(node.attr(u"showErrorMessage"), false);
    rule.allowBlank = boolAttr(node.attr(u"allowBlank"), false);
    rule.inCellDropdown = !boolAttr(node.attr(u"showDropDown"), false);
    rule.x14 = x14;
    switch (*kind) {
    case Kind::List: {
        const QString source = formula1.trimmed();
        if (source.isEmpty()) {
            return std::nullopt;
        }
        if (source.startsWith(QLatin1Char('"'))) {
            rule.list = inlineItems(source);
        } else {
            rule.listSource = formulaFromFile(source.startsWith(QLatin1Char('=')) ? source.mid(1) : source);
        }
        break;
    }
    case Kind::Custom:
        if (formula1.trimmed().isEmpty()) {
            return std::nullopt;
        }
        rule.formula1 = formulaFromFile(formula1.trimmed());
        if (isNotEmptyFormula(rule.formula1, rule.range) && rule.moreRanges.isEmpty()) {
            rule.kind = Kind::NotEmpty;
            rule.formula1.clear();
        }
        break;
    case Kind::Any:
        if (hasFormula1 || hasFormula2) {
            return std::nullopt;
        }
        break;
    default: {
        const bool two = *op == VOp::Between || *op == VOp::NotBetween;
        if (!hasFormula1 || formula1.trimmed().isEmpty() || (two && (!hasFormula2 || formula2.trimmed().isEmpty()))) {
            return std::nullopt;
        }
        setOperand(&rule, false, formula1);
        if (two) {
            setOperand(&rule, true, formula2);
        }
        break;
    }
    }
    if (!prepareValidationRule(rule)) {
        return std::nullopt;
    }
    return rule;
}

} // namespace

void readValidationRules(const RuleXml &xml, Sheet *sheet, QList<KeptRule> *kept, RuleReadCounts *counts)
{
    QList<ValidationRule> modelled;
    const auto keepNode = [&](const RawNode &node, bool x14) {
        KeptRule rule;
        rule.kind = x14 ? KeptRule::Kind::ValidationX14 : KeptRule::Kind::Validation;
        const RawNode *sqref = node.child(u"sqref");
        rule.ranges = parseSqref(x14 ? (sqref != nullptr ? sqref->text.trimmed() : QString()) : node.attr(u"sqref"));
        rule.xml = selfContained(xml.text, node, x14 ? x14Scope(true) : worksheetScope());
        rule.reason = QStringLiteral("a validation setting QindaCalc does not model");
        kept->append(rule);
        ++counts->keptValidations;
    };
    for (const RawNode &node : xml.dataValidations) {
        if (const std::optional<ValidationRule> rule = modelValidation(node, false)) {
            modelled.append(*rule);
        } else {
            keepNode(node, false);
        }
    }
    for (const RawNode &node : xml.x14Validations) {
        if (const std::optional<ValidationRule> rule = modelValidation(node, true)) {
            modelled.append(*rule);
        } else {
            keepNode(node, true);
        }
    }
    counts->modelledValidations += int(modelled.size());
    sheet->setValidations(modelled);
}

} // namespace QindaOffice::Calc::IO::Rules
