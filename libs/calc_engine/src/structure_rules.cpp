// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): the formulas of conditional formats and validations through
// inserted and removed rows and columns (structure_internal.h). A rule's
// formula is a formula placed at its anchor — the top-left cell of the
// first range of its sqref — and shared over the sqref, so it follows the
// edit like a formula in that cell: references to moved cells move, a
// reference into a removed band becomes #REF!. When the edit takes away
// the anchor's own row or column (or the whole first range), the rule's new
// anchor is the first cell that remains, and the formula is first shifted
// there, so every cell that stays keeps the condition it had — Excel's
// behaviour.

#include "sheet.h"
#include "structure_internal.h"
#include "workbook.h"

namespace QindaOffice::Calc {

using namespace StructureMath;

namespace {

// The pre-edit cell that becomes the top-left of what remains of `range`
// (moveSheetContents keeps the surviving part); nullopt when it all goes.
std::optional<CellAddr> survivingTopLeft(const CellRange &range, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    if (band.insert) {
        return range.topLeft;
    }
    const bool rows = editsRows(edit);
    const qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
    const qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
    const qint32 first = band.at;
    const qint32 last = band.at + band.count - 1;
    if (lo >= first && hi <= last) {
        return std::nullopt;
    }
    CellAddr out = range.topLeft;
    if (lo >= first && lo <= last) {
        (rows ? out.row : out.col) = last + 1;
    }
    return out;
}

std::optional<CellAddr> newAnchor(const QList<CellRange> &ranges, const StructureEdit &edit)
{
    for (const CellRange &range : ranges) {
        if (const std::optional<CellAddr> anchor = survivingTopLeft(range, edit)) {
            return anchor;
        }
    }
    return std::nullopt;
}

// `formula`, placed at `from`, moved to `to` and through the edit; the
// text itself when nothing changes (or it does not parse).
QString moved(const QString &formula, const CellAddr &from, const CellAddr &to, const StructureEdit &edit,
              bool ownerIsEdited, const QString &editedSheetName, bool *changed)
{
    if (formula.isEmpty()) {
        return formula;
    }
    const ParseResult parsed = parseFormula(formula);
    if (!parsed.error.isEmpty() || !parsed.root) {
        return formula;
    }
    NodePtr node = parsed.root;
    if (to != from) {
        node = shiftNode(node, to.row - from.row, to.col - from.col);
    }
    node = adjustFormulaNode(node, edit, ownerIsEdited, editedSheetName);
    const QString text = nodeText(node);
    if (text == nodeText(parsed.root)) {
        return formula;
    }
    *changed = true;
    return text;
}

bool isPlainNumber(const QString &text)
{
    bool number = false;
    static_cast<void>(text.trimmed().toDouble(&number));
    return number;
}

} // namespace

void StructureMath::moveRuleFormulas(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        const bool owner = s == edit.sheet;
        QList<CondFormatRule> formats = book.sheetAt(s).condFormats();
        bool touched = false;
        for (CondFormatRule &rule : formats) {
            const CellAddr from = rule.range.topLeft;
            const std::optional<CellAddr> to = owner ? newAnchor(rule.ranges(), edit) : std::optional<CellAddr>(from);
            if (!to.has_value()) {
                continue; // the rule goes with its cells
            }
            bool changed = false;
            rule.formula = moved(rule.formula, from, *to, edit, owner, editedSheetName, &changed);
            rule.formula2 = moved(rule.formula2, from, *to, edit, owner, editedSheetName, &changed);
            for (CfThreshold &threshold : rule.thresholds) {
                // A cfvo formula takes no relative reference: it moves, it
                // is not shifted.
                if (!isPlainNumber(threshold.value)) {
                    threshold.value = moved(threshold.value, from, from, edit, owner, editedSheetName, &changed);
                }
            }
            if (changed) {
                static_cast<void>(prepareCondRule(rule));
                touched = true;
            }
        }
        if (touched) {
            book.sheetAt(s).setCondFormats(formats);
        }
        QList<ValidationRule> validations = book.sheetAt(s).validations();
        touched = false;
        for (ValidationRule &rule : validations) {
            const CellAddr from = rule.range.topLeft;
            QList<CellRange> ranges{rule.range};
            ranges += rule.moreRanges;
            const std::optional<CellAddr> to = owner ? newAnchor(ranges, edit) : std::optional<CellAddr>(from);
            if (!to.has_value()) {
                continue;
            }
            bool changed = false;
            rule.formula1 = moved(rule.formula1, from, *to, edit, owner, editedSheetName, &changed);
            rule.formula2 = moved(rule.formula2, from, *to, edit, owner, editedSheetName, &changed);
            // A plain range source moved already (rewriteValidationSources);
            // a formula source (OFFSET(…)) moves here.
            const ParseResult source = parseFormula(rule.listSource);
            if (source.root && source.root->kind != Node::Kind::Ref && source.root->kind != Node::Kind::Name) {
                rule.listSource = moved(rule.listSource, from, from, edit, owner, editedSheetName, &changed);
            }
            if (changed || rule.ast1 || rule.ast2) {
                static_cast<void>(prepareValidationRule(rule)); // the list source may have moved too
                touched = true;
            }
        }
        if (touched) {
            book.sheetAt(s).setValidations(validations);
        }
    }
}

} // namespace QindaOffice::Calc
