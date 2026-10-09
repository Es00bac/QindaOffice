// SPDX-License-Identifier: GPL-3.0-or-later
#include "structure.h"

#include "chart_spec.h"
#include "pivot_edit.h"
#include "structure_internal.h"
#include "sheet.h"
#include "workbook.h"

#include <algorithm>
#include <optional>

namespace QindaOffice::Calc {
namespace {

using namespace StructureMath;

// Rewrites an AST so its references follow the edit. Dead references become
// literal #REF! nodes, which is what a spreadsheet must show rather than
// quietly re-pointing at whatever slid into the deleted cell's place.
[[nodiscard]] NodePtr adjustNode(const NodePtr &node, const StructureEdit &edit,
                                 bool ownerIsEdited, const QString &editedSheetName)
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
        RangeRef ref = node->ref;
        if (!adjustRef(ref, edit, ownerIsEdited, editedSheetName)) {
            return Node::makeError(Err::Ref);
        }
        return Node::makeRef(ref);
    }
    case Node::Kind::Unary:
        return Node::makeUnary(node->op,
                               adjustNode(node->left, edit, ownerIsEdited, editedSheetName));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op,
                                adjustNode(node->left, edit, ownerIsEdited, editedSheetName),
                                adjustNode(node->right, edit, ownerIsEdited, editedSheetName));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        for (const NodePtr &arg : node->args) {
            args.append(adjustNode(arg, edit, ownerIsEdited, editedSheetName));
        }
        return Node::makeCall(node->text, std::move(args));
    }
    }
    return node;
}

// Rewrites every formula in the workbook for `edit`. Callers reindex after.
void rewriteFormulas(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        Sheet &sheet = book.sheetAt(s);
        QHash<quint64, Cell> cells = sheet.cells();
        bool touched = false;
        for (auto it = cells.begin(); it != cells.end(); ++it) {
            if (!it.value().ast) {
                continue;
            }
            const NodePtr adjusted =
                adjustNode(it.value().ast, edit, s == edit.sheet, editedSheetName);
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
}

// Named ranges are absolute addresses, so they follow the edit exactly like
// a formula reference; one whose range is entirely removed is dropped.
void rewriteNames(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    const QHash<QString, RangeRef> names = book.namedRanges();
    for (const auto &[name, target] : names.asKeyValueRange()) {
        RangeRef ref = target;
        // Names are pinned to a sheet at definition time, so "owner is the
        // edited sheet" is never the fallback here.
        if (!adjustRef(ref, edit, false, editedSheetName)) {
            book.removeName(name);
            continue;
        }
        if (!(ref.range == target.range)) {
            book.defineName(name, ref);
        }
    }
    // Scoped, constant and formula names (D-069) follow like formulas; an
    // unqualified reference in a sheet-scoped name means its own sheet.
    QList<DefinedName> defined = book.definedNames();
    bool touched = false;
    for (DefinedName &entry : defined) {
        if (!entry.ast) {
            continue; // kept verbatim; the engine cannot see its references
        }
        const NodePtr adjusted = adjustNode(entry.ast, edit, entry.sheet == edit.sheet, editedSheetName);
        if (adjusted != entry.ast) {
            entry.ast = adjusted;
            entry.formula = nodeText(adjusted);
            touched = true;
        }
    }
    if (touched) {
        book.setDefinedNames(defined);
    }
}

// A list validation that names a range (D-069) keeps that live source for
// the file; it moves with the edit like a formula reference, and a source
// whose range is removed is dropped (the rule keeps its last items).
void rewriteValidationSources(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        QList<ValidationRule> rules = book.sheetAt(s).validations();
        bool touched = false;
        for (ValidationRule &rule : rules) {
            const ParseResult parsed = parseFormula(rule.listSource);
            if (rule.listSource.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
                continue;
            }
            RangeRef ref = parsed.root->ref;
            if (!adjustRef(ref, edit, s == edit.sheet, editedSheetName)) {
                rule.listSource.clear();
            } else {
                rule.listSource = rangeRefText(ref);
            }
            touched = true;
        }
        if (touched) {
            book.sheetAt(s).setValidations(rules);
        }
    }
}

// Charts carry a range and/or series references plus an anchor cell on one
// sheet; all move with the edit. A series whose range dies is dropped, and
// a chart is dropped when its last data source (range and series alike)
// goes with the band. Series name their sheet, so they follow an edit on
// any sheet — a chart elsewhere, a chart sheet's chart (M69: before, only
// charts anchored on the edited sheet followed). A chart sheet keeps its
// chart even with no data left.
void rewriteCharts(Workbook &book, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    const QString editedSheetName = book.sheetName(edit.sheet);
    QList<ChartSpec> kept;
    kept.reserve(book.charts.size());
    for (ChartSpec spec : std::as_const(book.charts)) {
        const bool onEdited = spec.sheet == edit.sheet;
        if (onEdited && !spec.rangeText.isEmpty()) {
            CellRange range = parseChartRange(spec.rangeText);
            if (range.isValid()) {
                qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
                qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
                if (!movedSpan(lo, hi, band)) {
                    spec.rangeText.clear(); // the chart's range died
                } else {
                    if (rows) {
                        range.topLeft.row = lo;
                        range.bottomRight.row = hi;
                    } else {
                        range.topLeft.col = lo;
                        range.bottomRight.col = hi;
                    }
                    spec.rangeText = addrText(range.topLeft) + QLatin1Char(':')
                        + addrText(range.bottomRight);
                }
            }
        }
        for (ChartSeriesSpec &series : spec.series) {
            for (QString *refText : {&series.catRange, &series.valRange, &series.name}) {
                const ParseResult parsed = parseFormula(*refText);
                // A name is moved only when it is a sheet-qualified cell
                // (a series named after a cell, M69), never a literal "Q1".
                if (!parsed.root || parsed.root->kind != Node::Kind::Ref
                    || (refText == &series.name && parsed.root->ref.sheet.isEmpty())) {
                    continue;
                }
                RangeRef ref = parsed.root->ref;
                // The parser keeps a quoted sheet's display spelling; the
                // comparison and the re-serialized text want the name.
                ref.sheet = unquotedSheetName(ref.sheet);
                if (!adjustRef(ref, edit, false, editedSheetName)) {
                    refText->clear(); // the reference died with the band
                } else {
                    *refText = rangeRefText(ref);
                }
            }
        }
        spec.series.erase(std::remove_if(spec.series.begin(), spec.series.end(),
                                         [](const ChartSeriesSpec &series) {
                                             return series.valRange.isEmpty();
                                         }),
                          spec.series.end());
        const bool chartSheet = spec.sheet >= 0 && spec.sheet < book.sheetCount()
            && book.sheetAt(spec.sheet).isChartSheet();
        const bool hasRange = parseChartRange(spec.rangeText).isValid();
        if (!hasRange && spec.series.isEmpty() && !chartSheet) {
            continue; // no data source left
        }
        if (onEdited) {
            qint32 &anchor = rows ? spec.anchorRow : spec.anchorCol;
            const qint32 moved = movedIndex(anchor, band);
            anchor = moved < 0 ? band.at : moved;
        }
        kept.append(spec);
    }
    book.charts = kept;
}

// Images have no data source, so an edit only ever moves their anchor: a
// picture whose anchor cell is removed lands on the cell that took its
// place, the way a chart's does.
void rewriteImages(Workbook &book, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    for (ImageSpec &spec : book.images) {
        if (spec.sheet != edit.sheet) {
            continue;
        }
        qint32 &anchor = rows ? spec.anchorRow : spec.anchorCol;
        const qint32 moved = movedIndex(anchor, band);
        anchor = moved < 0 ? band.at : moved;
    }
}

// Moves the sheet's sparse maps. Cells and styles that fall in a removed
// band are dropped; merges follow their span and collapse with it.
void moveSheetContents(Sheet &sheet, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);

    // A range that survives the edit, or nullopt when the whole span went.
    const auto movedRange = [&band, rows](CellRange range) -> std::optional<CellRange> {
        qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
        qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
        if (!movedSpan(lo, hi, band)) {
            return std::nullopt;
        }
        if (rows) {
            range.topLeft.row = lo;
            range.bottomRight.row = hi;
        } else {
            range.topLeft.col = lo;
            range.bottomRight.col = hi;
        }
        return range;
    };

    QHash<quint64, Cell> cells;
    cells.reserve(sheet.cells().size());
    for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
        CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        const qint32 moved = movedIndex(rows ? addr.row : addr.col, band);
        if (moved < 0) {
            continue;
        }
        (rows ? addr.row : addr.col) = moved;
        Cell copy = cell;
        if (copy.arrayRange.isValid()) {
            // A legacy array formula's block moves with its anchor (M69);
            // its other cells are spilled values the reindex rewrites.
            copy.arrayRange = movedRange(copy.arrayRange).value_or(CellRange{addr, addr});
        }
        cells.insert(addr.key(), copy);
    }

    QHash<quint64, qint32> styles;
    styles.reserve(sheet.styleIds().size());
    for (const auto &[packed, id] : sheet.styleIds().asKeyValueRange()) {
        CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        const qint32 moved = movedIndex(rows ? addr.row : addr.col, band);
        if (moved < 0) {
            continue;
        }
        (rows ? addr.row : addr.col) = moved;
        styles.insert(addr.key(), id);
    }

    QHash<qint32, qreal> colWidths;
    QHash<qint32, qreal> rowHeights;
    const auto moveGeometry = [&band](const QHash<qint32, qreal> &in) {
        QHash<qint32, qreal> out;
        out.reserve(in.size());
        for (const auto &[index, size] : in.asKeyValueRange()) {
            const qint32 moved = movedIndex(index, band);
            if (moved >= 0) {
                out.insert(moved, size);
            }
        }
        return out;
    };
    colWidths = rows ? sheet.columnWidths() : moveGeometry(sheet.columnWidths());
    rowHeights = rows ? moveGeometry(sheet.rowHeights()) : sheet.rowHeights();

    QList<CellRange> merges;
    for (const CellRange &merge : sheet.merges()) {
        if (const auto moved = movedRange(merge)) {
            merges.append(*moved);
        }
    }

    // AGENT-GUARD: the sheet's RULES address cells by range too. A rule left
    // un-moved paints, validates or filters the wrong cells after an insert
    // — the same class of defect as a formula that does not follow.
    // M78 (D-087): every range of a rule's sqref moves; a rule lives while
    // one of them does (the first survivor becomes its anchor).
    const auto movedRanges = [&movedRange](CellRange &first, QList<CellRange> &more) {
        QList<CellRange> kept;
        for (const CellRange &range : QList<CellRange>{first} + more) {
            if (const auto moved = movedRange(range)) {
                kept.append(*moved);
            }
        }
        if (kept.isEmpty()) {
            return false;
        }
        first = kept.takeFirst();
        more = kept;
        return true;
    };
    QList<CondFormatRule> condFormats;
    for (CondFormatRule rule : sheet.condFormats()) {
        if (movedRanges(rule.range, rule.moreRanges)) {
            condFormats.append(rule);
        }
    }
    QList<ValidationRule> validations;
    for (ValidationRule rule : sheet.validations()) {
        if (movedRanges(rule.range, rule.moreRanges)) {
            validations.append(rule);
        }
    }
    AutoFilter filter = sheet.autoFilter();
    if (filter.range.isValid()) {
        if (const auto moved = movedRange(filter.range)) {
            filter.range = *moved;
        } else {
            filter = AutoFilter{};
        }
    }
    // Tables are range-addressed state like every other rule: a table whose
    // span dies with the band is dropped, one straddling an insertion grows
    // — and (M69) its column records follow inserted and removed columns.
    QList<TableSpec> tables = movedTables(sheet, edit);
    QHash<quint64, CellComment> comments;
    comments.reserve(sheet.comments().size());
    for (const auto &[packed, comment] : sheet.comments().asKeyValueRange()) {
        CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        const qint32 moved = movedIndex(rows ? addr.row : addr.col, band);
        if (moved < 0) {
            continue;
        }
        (rows ? addr.row : addr.col) = moved;
        comments.insert(addr.key(), comment);
    }

    sheet.replaceContents(std::move(cells), std::move(styles), std::move(colWidths),
                          std::move(rowHeights), std::move(merges));
    sheet.setCondFormats(std::move(condFormats));
    sheet.setValidations(std::move(validations));
    sheet.setAutoFilter(std::move(filter));
    sheet.setTables(std::move(tables));
    sheet.setComments(std::move(comments));
    moveSheetPrintAndKept(sheet, edit); // print area, titles, breaks, kept rules (M78)
}

} // namespace

NodePtr StructureMath::adjustFormulaNode(const NodePtr &node, const StructureEdit &edit, bool ownerIsEdited,
                                         const QString &editedSheetName)
{
    return adjustNode(node, edit, ownerIsEdited, editedSheetName);
}

void applyStructureEdit(Workbook &book, const StructureEdit &edit)
{
    if (edit.count <= 0 || edit.at < 0 || edit.sheet < 0 || edit.sheet >= book.sheetCount()) {
        return;
    }
    // AGENT-GUARD: formulas are rewritten BEFORE the cells move, because the
    // rewrite reads the pre-edit sheet name/contents and must see every
    // formula exactly once — including the ones that are about to be
    // deleted, which simply disappear with their cells.
    rewriteTableRefs(book, edit); // structured references to what goes (M69)
    rewriteFormulas(book, edit);
    rewriteNames(book, edit);
    rewriteValidationSources(book, edit);
    moveRuleFormulas(book, edit); // conditional-format and validation formulas (M78)
    rewriteCharts(book, edit);
    rewriteImages(book, edit);
    moveSheetFeatures(book, edit); // links, outline, source journal (M66)
    movePivotsForEdit(book, edit); // PivotTables and their sources (M74)
    moveWhatIfForEdit(book, edit); // scenarios and data tables (M78)
    moveSheetContents(book.sheetAt(edit.sheet), edit);
    // Inserted table columns are named and inserted table rows filled with
    // the calculated columns (M69); endBulkLoad then reindexes once.
    book.beginBulkLoad();
    fillTablesAfterEdit(book, edit);
    book.endBulkLoad();
}

} // namespace QindaOffice::Calc
