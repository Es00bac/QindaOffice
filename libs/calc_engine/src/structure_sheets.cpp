// SPDX-License-Identifier: GPL-3.0-or-later
// Sheet-list operations and range sorting (structure.h). Split from
// structure.cpp to keep both files inside the line budget; the two share
// the same contract — mutate in place, then reindex and recalculate.

#include "structure.h"

#include "chart_spec.h"
#include "pivot_edit.h"
#include "sheet.h"
#include "structure_internal.h"
#include "table_refs.h"
#include "workbook.h"

#include <algorithm>

namespace QindaOffice::Calc {
namespace {

// Rewrites the sheet qualifier of every reference: `from` becomes `to`, or,
// when `to` is empty, the reference dies (#REF!) because its sheet is gone.
[[nodiscard]] NodePtr requalify(const NodePtr &node, const QString &from, const QString &to)
{
    if (!node) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::Number:
    case Node::Kind::Text:
    case Node::Kind::Bool:
    case Node::Kind::Error:
    case Node::Kind::Name:
    case Node::Kind::Array:
    case Node::Kind::Missing:
        return node;
    case Node::Kind::StructRef: // follows its table by name (structure_tables.cpp)
        return node;
    case Node::Kind::Ref: {
        // Unquote first: 'Q1 Sales'!A1 names the sheet Q1 Sales (M61).
        if (unquotedSheetName(node->ref.sheet).compare(from, Qt::CaseInsensitive) != 0) {
            return node;
        }
        if (to.isEmpty()) {
            return Node::makeError(Err::Ref);
        }
        RangeRef ref = node->ref;
        ref.sheet = to;
        return Node::makeRef(ref);
    }
    case Node::Kind::Unary:
        return Node::makeUnary(node->op, requalify(node->left, from, to));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op, requalify(node->left, from, to),
                                requalify(node->right, from, to));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        for (const NodePtr &arg : node->args) {
            args.append(requalify(arg, from, to));
        }
        return Node::makeCall(node->text, std::move(args));
    }
    }
    return node;
}

void requalifyWorkbook(Workbook &book, const QString &from, const QString &to)
{
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        Sheet &sheet = book.sheetAt(s);
        QHash<quint64, Cell> cells = sheet.cells();
        bool touched = false;
        for (auto it = cells.begin(); it != cells.end(); ++it) {
            if (!it.value().ast) {
                continue;
            }
            const NodePtr adjusted = requalify(it.value().ast, from, to);
            if (adjusted == it.value().ast) {
                continue;
            }
            it.value().ast = adjusted;
            it.value().formula = nodeText(adjusted);
            touched = true;
        }
        if (touched) {
            sheet.replaceContents(std::move(cells), sheet.styleIds(), sheet.columnWidths(),
                                  sheet.rowHeights(), sheet.merges());
        }
    }
    const QHash<QString, RangeRef> names = book.namedRanges();
    for (const auto &[name, target] : names.asKeyValueRange()) {
        if (unquotedSheetName(target.sheet).compare(from, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (to.isEmpty()) {
            book.removeName(name);
            continue;
        }
        RangeRef ref = target;
        ref.sheet = to;
        book.defineName(name, ref);
    }
    // Validation list sources (D-069) follow the sheet, or lose it.
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<ValidationRule> rules = book.sheetAt(s).validations();
        bool moved = false;
        for (ValidationRule &rule : rules) {
            const ParseResult parsed = parseFormula(rule.listSource);
            if (rule.listSource.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
                continue;
            }
            const NodePtr adjusted = requalify(parsed.root, from, to);
            if (adjusted != parsed.root) {
                rule.listSource = adjusted->kind == Node::Kind::Ref ? rangeRefText(adjusted->ref) : QString();
                moved = true;
            }
        }
        if (moved) {
            book.sheetAt(s).setValidations(rules);
        }
    }
    QList<DefinedName> defined = book.definedNames();
    bool touched = false;
    for (DefinedName &entry : defined) {
        const NodePtr adjusted = requalify(entry.ast, from, to);
        if (adjusted != entry.ast) {
            entry.ast = adjusted;
            entry.formula = nodeText(adjusted);
            touched = true;
        }
    }
    if (touched) {
        book.setDefinedNames(defined);
    }
    // Chart series name their sheet too (M69): they follow a rename, and a
    // series whose sheet is removed goes (a chart sheet keeps its chart).
    for (ChartSpec &chart : book.charts) {
        for (ChartSeriesSpec &series : chart.series) {
            for (QString *refText : {&series.catRange, &series.valRange, &series.name}) {
                const ParseResult parsed = parseFormula(*refText);
                if (refText->isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref
                    || parsed.root->ref.sheet.isEmpty()) {
                    continue;
                }
                const NodePtr adjusted = requalify(parsed.root, from, to);
                if (adjusted != parsed.root) {
                    *refText = adjusted->kind == Node::Kind::Ref ? rangeRefText(adjusted->ref) : QString();
                }
            }
        }
        chart.series.erase(std::remove_if(chart.series.begin(), chart.series.end(),
                                          [](const ChartSeriesSpec &series) { return series.valRange.isEmpty(); }),
                           chart.series.end());
    }
}

} // namespace

bool removeSheet(Workbook &book, qint32 index)
{
    if (index < 0 || index >= book.sheetCount() || book.sheetCount() <= 1) {
        return false;
    }
    const QString gone = book.sheetName(index);
    // Formulas are requalified while the sheet still exists, so every
    // reference to it can be found by name before the indices shift.
    requalifyWorkbook(book, gone, QString());
    // Its tables go with it: structured references to them are #REF! (M69).
    const QList<TableSpec> goneTables = book.sheetAt(index).tables();
    if (!goneTables.isEmpty()) {
        rewriteStructRefs(book, [&goneTables](const StructRef &, const QString &tableName) -> NodePtr {
            for (const TableSpec &table : goneTables) {
                if (table.name.compare(tableName, Qt::CaseInsensitive) == 0) {
                    return Node::makeError(Err::Ref);
                }
            }
            return nullptr;
        });
    }
    QList<ChartSpec> charts;
    for (ChartSpec spec : std::as_const(book.charts)) {
        if (spec.sheet == index) {
            continue;
        }
        if (spec.sheet > index) {
            --spec.sheet;
        }
        charts.append(spec);
    }
    book.charts = charts;
    QList<ImageSpec> images;
    for (ImageSpec spec : std::as_const(book.images)) {
        if (spec.sheet == index) {
            continue;
        }
        if (spec.sheet > index) {
            --spec.sheet;
        }
        images.append(spec);
    }
    book.images = images;
    // Names scoped to the sheet go with it; later scopes shift down (D-069).
    QList<DefinedName> defined;
    for (DefinedName entry : book.definedNames()) {
        if (entry.sheet == index) {
            continue;
        }
        if (entry.sheet > index) {
            --entry.sheet;
        }
        defined.append(entry);
    }
    book.setDefinedNames(defined);
    if (!book.removeSheetAt(index)) {
        return false;
    }
    book.reindexAll();
    return true;
}

bool renameSheet(Workbook &book, qint32 index, const QString &newName)
{
    if (index < 0 || index >= book.sheetCount()) {
        return false;
    }
    const QString oldName = book.sheetName(index);
    if (oldName == newName) {
        return true;
    }
    if (!book.renameSheetAt(index, newName)) {
        return false;
    }
    // AGENT-GUARD: references address sheets by NAME, so a rename that does
    // not rewrite them turns every cross-sheet formula into #NAME?.
    requalifyWorkbook(book, oldName, newName);
    StructureMath::requalifyLinkLocations(book, oldName, newName);
    requalifyPivotSources(book, oldName, newName); // M74: range sources follow
    book.reindexAll();
    return true;
}

bool moveSheet(Workbook &book, qint32 from, qint32 to)
{
    if (!book.moveSheetTo(from, to)) {
        return false;
    }
    // Charts store a sheet INDEX; remap the ones the move displaced.
    const auto remap = [from, to](qint32 sheet) {
        if (sheet == from) {
            return to;
        }
        if (from < to && sheet > from && sheet <= to) {
            return sheet - 1;
        }
        if (from > to && sheet >= to && sheet < from) {
            return sheet + 1;
        }
        return sheet;
    };
    QList<ChartSpec> charts;
    charts.reserve(book.charts.size());
    for (ChartSpec spec : std::as_const(book.charts)) {
        spec.sheet = remap(spec.sheet);
        charts.append(spec);
    }
    book.charts = charts;
    for (ImageSpec &spec : book.images) {
        spec.sheet = remap(spec.sheet);
    }
    QList<DefinedName> defined = book.definedNames();
    for (DefinedName &entry : defined) {
        if (entry.sheet >= 0) {
            entry.sheet = remap(entry.sheet);
        }
    }
    book.setDefinedNames(defined);
    book.reindexAll();
    return true;
}

void sortRange(Workbook &book, qint32 sheet, const CellRange &range, qint32 keyColumn,
               bool ascending, bool headerRow)
{
    if (sheet < 0 || sheet >= book.sheetCount() || !range.isValid()) {
        return;
    }
    if (keyColumn < range.topLeft.col || keyColumn > range.bottomRight.col) {
        return;
    }
    const qint32 first = range.topLeft.row + (headerRow ? 1 : 0);
    if (first >= range.bottomRight.row) {
        return; // nothing to reorder
    }
    Sheet &target = book.sheetAt(sheet);

    QVector<qint32> order;
    order.reserve(range.bottomRight.row - first + 1);
    for (qint32 r = first; r <= range.bottomRight.row; ++r) {
        order.append(r);
    }
    const auto keyAt = [&](qint32 row) {
        const Cell *cell = target.cellAt({row, keyColumn});
        return cell != nullptr ? cell->value : Value(std::monostate{});
    };
    // std::stable_sort so equal keys keep their original order, which is
    // what makes a second sort on another column behave as a tie-break.
    std::stable_sort(order.begin(), order.end(), [&](qint32 a, qint32 b) {
        const Value va = keyAt(a);
        const Value vb = keyAt(b);
        // Blanks sort last in BOTH directions: a descending sort that
        // floated the empty tail to the top would bury the data.
        const bool ba = isBlank(va);
        const bool bb = isBlank(vb);
        if (ba != bb) {
            return !ba;
        }
        if (ba) {
            return false;
        }
        const int cmp = compareValues(va, vb);
        return ascending ? cmp < 0 : cmp > 0;
    });

    QHash<quint64, Cell> cells = target.cells();
    QHash<quint64, qint32> styles = target.styleIds();
    // Lift the block out first: source and destination rows overlap, so
    // writing in place would read cells that have already been overwritten.
    QHash<quint64, Cell> moved;
    QHash<quint64, qint32> movedStyles;
    for (int i = 0; i < order.size(); ++i) {
        const qint32 source = order.at(i);
        const qint32 destination = first + i;
        const qint32 delta = destination - source;
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            const CellAddr from{source, c};
            const CellAddr to{destination, c};
            const auto cell = cells.constFind(from.key());
            if (cell != cells.constEnd()) {
                Cell copy = cell.value();
                if (copy.ast && delta != 0) {
                    // A formula that travels keeps meaning the same thing
                    // relative to its new row (absolute axes stay put).
                    copy.ast = shiftNode(copy.ast, delta, 0);
                    copy.formula = nodeText(copy.ast);
                }
                moved.insert(to.key(), copy);
            }
            const auto style = styles.constFind(from.key());
            if (style != styles.constEnd()) {
                movedStyles.insert(to.key(), style.value());
            }
        }
    }
    for (qint32 r = first; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            const quint64 key = CellAddr{r, c}.key();
            cells.remove(key);
            styles.remove(key);
        }
    }
    for (const auto &[key, cell] : moved.asKeyValueRange()) {
        cells.insert(key, cell);
    }
    for (const auto &[key, id] : movedStyles.asKeyValueRange()) {
        styles.insert(key, id);
    }
    target.replaceContents(std::move(cells), std::move(styles), target.columnWidths(),
                           target.rowHeights(), target.merges());
    book.reindexAll();
}

} // namespace QindaOffice::Calc
