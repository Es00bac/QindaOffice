// SPDX-License-Identifier: GPL-3.0-or-later
// Rewriting structured references across a workbook (table_refs.h, M69,
// D-078), and the Workbook's table lookups for the evaluator.

#include "table_refs.h"

#include "eval.h"
#include "workbook.h"

namespace QindaOffice::Calc {

NodePtr fixStructRefs(const NodePtr &node, const QString &containingTable, const StructRefFix &fix)
{
    if (!node) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::StructRef: {
        if (!node->structRef) {
            return node;
        }
        const QString table = node->structRef->table.isEmpty() ? containingTable : node->structRef->table;
        const NodePtr replaced = fix(*node->structRef, table);
        return replaced ? replaced : node;
    }
    case Node::Kind::Unary: {
        const NodePtr operand = fixStructRefs(node->left, containingTable, fix);
        return operand == node->left ? node : Node::makeUnary(node->op, operand);
    }
    case Node::Kind::Binary: {
        const NodePtr left = fixStructRefs(node->left, containingTable, fix);
        const NodePtr right = fixStructRefs(node->right, containingTable, fix);
        return left == node->left && right == node->right ? node : Node::makeBinary(node->op, left, right);
    }
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        bool changed = false;
        for (const NodePtr &arg : node->args) {
            args.append(fixStructRefs(arg, containingTable, fix));
            changed = changed || args.last() != arg;
        }
        return changed ? Node::makeCall(node->text, std::move(args)) : node;
    }
    default:
        return node;
    }
}

bool containsStructRef(const NodePtr &node)
{
    if (!node) {
        return false;
    }
    switch (node->kind) {
    case Node::Kind::StructRef:
        return true;
    case Node::Kind::Unary:
        return containsStructRef(node->left);
    case Node::Kind::Binary:
        return containsStructRef(node->left) || containsStructRef(node->right);
    case Node::Kind::Call:
        for (const NodePtr &arg : node->args) {
            if (containsStructRef(arg)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

QString containingTableName(const Workbook &book, qint32 sheet, const CellAddr &addr)
{
    const TableSpec *table = book.tableAt(sheet, addr);
    return table != nullptr ? table->name : QString();
}

bool rewriteStructRefs(Workbook &book, const StructRefFix &fix)
{
    bool any = false;
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        Sheet &sheet = book.sheetAt(s);
        QHash<quint64, Cell> cells = sheet.cells();
        bool touched = false;
        for (auto it = cells.begin(); it != cells.end(); ++it) {
            if (!it.value().ast || !containsStructRef(it.value().ast)) {
                continue;
            }
            const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
            const NodePtr fixed = fixStructRefs(it.value().ast, containingTableName(book, s, addr), fix);
            if (fixed != it.value().ast) {
                it.value().ast = fixed;
                it.value().formula = nodeText(fixed);
                touched = true;
            }
        }
        if (touched) {
            sheet.replaceContents(std::move(cells), sheet.styleIds(), sheet.columnWidths(), sheet.rowHeights(),
                                  sheet.merges());
            any = true;
        }
        // A table's calculated column formulas speak of the table itself.
        QList<TableSpec> tables = sheet.tables();
        bool tablesTouched = false;
        for (TableSpec &table : tables) {
            for (QString &formula : table.calculatedColumns) {
                const ParseResult parsed = formula.isEmpty() ? ParseResult{} : parseFormula(formula);
                if (!parsed.root || !containsStructRef(parsed.root)) {
                    continue;
                }
                const NodePtr fixed = fixStructRefs(parsed.root, table.name, fix);
                if (fixed != parsed.root) {
                    formula = nodeText(fixed);
                    tablesTouched = true;
                }
            }
        }
        if (tablesTouched) {
            sheet.setTables(tables);
            any = true;
        }
    }
    QList<DefinedName> names = book.definedNames();
    bool namesTouched = false;
    for (DefinedName &name : names) {
        if (!name.ast || !containsStructRef(name.ast)) {
            continue;
        }
        const NodePtr fixed = fixStructRefs(name.ast, QString(), fix);
        if (fixed != name.ast) {
            name.ast = fixed;
            name.formula = nodeText(fixed);
            namesTouched = true;
        }
    }
    if (namesTouched) {
        book.setDefinedNames(names);
        any = true;
    }
    return any;
}

// ---- the Workbook's side (workbook.h) ------------------------------------

const TableSpec *Workbook::tableNamed(const QString &name, qint32 *sheet) const
{
    for (qint32 s = 0; s < sheetCount(); ++s) {
        for (const TableSpec &table : sheetAt(s).tables()) {
            if (table.name.compare(name, Qt::CaseInsensitive) == 0) {
                if (sheet != nullptr) {
                    *sheet = s;
                }
                return &table;
            }
        }
    }
    return nullptr;
}

const TableSpec *Workbook::tableAt(qint32 sheet, const CellAddr &addr) const
{
    if (sheet < 0 || sheet >= sheetCount()) {
        return nullptr;
    }
    return sheetAt(sheet).tableCovering(addr);
}

void Workbook::refreshStructuredRefs()
{
    QSet<CellKey> touched;
    for (qint32 s = 0; s < sheetCount(); ++s) {
        for (const auto &[packed, cell] : sheetAt(s).cells().asKeyValueRange()) {
            if (!cell.ast || !containsStructRef(cell.ast)) {
                continue;
            }
            const CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
            const CellKey key(s, addr.row, addr.col);
            EvalContext ctx;
            ctx.host = this;
            ctx.sheet = s;
            ctx.here = addr;
            // AGENT-GUARD: updateCell clears the volatile mark (see
            // installFormula); put it back.
            m_dependencies.updateCell(key, collectTargets(cell.ast, ctx));
            if (cell.isVolatile) {
                m_dependencies.addVolatile(key);
            }
            touched.insert(key);
        }
    }
    if (!touched.isEmpty()) {
        editedCells(touched);
    }
}

} // namespace QindaOffice::Calc
