// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../parser.h"

namespace QindaOffice::Calc {
// Inspect calls, never text literals containing a function's spelling.
inline bool containsSubtotal(const NodePtr &node)
{
    if (!node) return false;
    if (node->kind == Node::Kind::Call
        && (node->text == QLatin1String("SUBTOTAL") || node->text == QLatin1String("AGGREGATE"))) return true;
    if (containsSubtotal(node->left) || containsSubtotal(node->right)) return true;
    for (const NodePtr &arg : node->args) if (containsSubtotal(arg)) return true;
    return false;
}
inline bool nestedSubtotal(const EvalHost &host, qint32 sheet, const CellAddr &cell)
{
    const QString formula = host.cellFormula(sheet, cell.row, cell.col);
    return !formula.isEmpty() && containsSubtotal(parseFormula(formula).root);
}
} // namespace QindaOffice::Calc
