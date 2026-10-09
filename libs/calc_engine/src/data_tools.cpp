// SPDX-License-Identifier: GPL-3.0-or-later
// Remove Duplicates and Consolidate (data_tools.h, M78, D-087), as Excel's
// Data ▸ Data Tools group does them.

#include "data_tools.h"

#include "data_table.h"
#include "sheet.h"
#include "workbook.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Calc {

namespace {

// What Remove Duplicates compares: the value, numbers to Excel's fifteen
// significant digits, text without regard to case, every blank alike.
QString duplicateKey(const Value &value)
{
    if (isBlank(value)) {
        return QStringLiteral("_");
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return QLatin1Char('n') + QString::number(*number, 'g', 15);
    }
    if (const auto *text = std::get_if<QString>(&value)) {
        return QLatin1Char('t') + text->toCaseFolded();
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag ? QStringLiteral("b1") : QStringLiteral("b0");
    }
    return QLatin1Char('e') + toText(value);
}

bool intersects(const CellRange &a, const CellRange &b)
{
    return a.isValid() && b.isValid() && a.topLeft.row <= b.bottomRight.row && b.topLeft.row <= a.bottomRight.row
        && a.topLeft.col <= b.bottomRight.col && b.topLeft.col <= a.bottomRight.col;
}

// `range` cut down to the sheet's cells with content: a selection of whole
// columns means the data in them, as in Excel (the empty rows below are not
// a million duplicates). Invalid when the range holds nothing.
CellRange clippedToContent(const Sheet &sheet, const CellRange &range)
{
    qint32 lastRow = -1;
    qint32 lastCol = -1;
    for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
        const CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        if (range.contains(addr) && !isBlank(cell.value)) {
            lastRow = qMax(lastRow, addr.row);
            lastCol = qMax(lastCol, addr.col);
        }
    }
    if (lastRow < 0) {
        return {};
    }
    CellRange clipped = range;
    clipped.bottomRight.row = qMin(range.bottomRight.row, lastRow);
    clipped.bottomRight.col = qMin(range.bottomRight.col, qMax(lastCol, range.topLeft.col));
    return clipped;
}

// Excel's refusals for the rows Remove Duplicates would move: merged cells
// across rows, part of an array, a PivotTable, a data table.
QString moveProblem(const Sheet &sheet, const CellRange &block)
{
    for (const CellRange &merge : sheet.merges()) {
        if (intersects(merge, block)
            && (merge.topLeft.row != merge.bottomRight.row || merge.topLeft.col < block.topLeft.col
                || merge.bottomRight.col > block.bottomRight.col)) {
            return QStringLiteral("Remove Duplicates can't move cells merged across rows or past the selection.");
        }
    }
    for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
        const CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        if (!block.contains(addr)) {
            continue;
        }
        const bool tallArray = cell.arrayRange.isValid()
            && (cell.arrayRange.topLeft.row != cell.arrayRange.bottomRight.row || !block.contains(cell.arrayRange.topLeft)
                || !block.contains(cell.arrayRange.bottomRight));
        const bool memberOfOther = !cell.ast && cell.spillAnchor.isValid() && !block.contains(cell.spillAnchor)
            && sheet.cellAt(cell.spillAnchor) != nullptr && sheet.cellAt(cell.spillAnchor)->arrayRange.isValid();
        if (tallArray || memberOfOther) {
            return QStringLiteral("You can't change part of an array.");
        }
    }
    for (const PivotTable &pivot : sheet.pivots()) {
        if (intersects(pivot.extent, block)) {
            return QStringLiteral("Cannot change this part of a PivotTable report.");
        }
    }
    for (const DataTableSpec &table : sheet.dataTables()) {
        if (intersects(table.ref, block)) {
            return QStringLiteral("Cannot change part of a data table.");
        }
    }
    return {};
}

} // namespace

RemoveDuplicatesResult removeDuplicates(Workbook &book, qint32 sheet, const CellRange &range,
                                        const QList<qint32> &columns, bool hasHeader)
{
    RemoveDuplicatesResult result;
    if (sheet < 0 || sheet >= book.sheetCount() || !range.isValid()) {
        result.error = QStringLiteral("Select a range of cells first.");
        return result;
    }
    QList<qint32> keyColumns;
    for (const qint32 column : columns) {
        if (column >= range.topLeft.col && column <= range.bottomRight.col && !keyColumns.contains(column)) {
            keyColumns.append(column);
        }
    }
    if (keyColumns.isEmpty()) {
        result.error = QStringLiteral("Select one or more columns that contain duplicates.");
        return result;
    }
    Sheet &target = book.sheetAt(sheet);
    const qint32 first = range.topLeft.row + (hasHeader ? 1 : 0);
    // Rows past the last one with data are not part of the list.
    const CellRange content = clippedToContent(target, range);
    const qint32 last = content.isValid() ? content.bottomRight.row : -1;
    if (first > last) {
        return result;
    }
    QSet<QString> seen;
    QList<qint32> kept;
    for (qint32 row = first; row <= last; ++row) {
        QString key;
        for (const qint32 column : std::as_const(keyColumns)) {
            const Cell *cell = target.cellAt({row, column});
            key += duplicateKey(cell != nullptr ? cell->value : Value()) + QChar(0x1f);
        }
        if (!seen.contains(key)) {
            seen.insert(key);
            kept.append(row);
        }
    }
    result.remaining = qint32(kept.size());
    result.removed = (last - first + 1) - result.remaining;
    if (result.removed == 0) {
        return result;
    }
    const CellRange block{{first, range.topLeft.col}, {last, range.bottomRight.col}};
    if (const QString problem = moveProblem(target, block); !problem.isEmpty()) {
        result.error = problem;
        result.removed = 0;
        result.remaining = last - first + 1;
        return result;
    }
    // Lift the kept rows out, empty the block, put them back from its top
    // (source and destination rows overlap — the sortRange shape).
    QHash<quint64, Cell> cells = target.cells();
    QHash<quint64, qint32> styles = target.styleIds();
    QHash<quint64, CellComment> comments = target.comments();
    QHash<quint64, Cell> movedCells;
    QHash<quint64, qint32> movedStyles;
    QHash<quint64, CellComment> movedComments;
    QHash<qint32, qint32> rowMap; // source row → its new row
    for (qsizetype i = 0; i < kept.size(); ++i) {
        const qint32 source = kept.at(i);
        const qint32 destination = first + qint32(i);
        rowMap.insert(source, destination);
        const qint32 delta = destination - source;
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            const quint64 from = CellAddr{source, c}.key();
            const quint64 to = CellAddr{destination, c}.key();
            if (const auto cell = cells.constFind(from); cell != cells.constEnd()) {
                Cell copy = cell.value();
                if (copy.ast && delta != 0) {
                    // A formula keeps meaning the same cells of its own row.
                    copy.ast = shiftNode(copy.ast, delta, 0);
                    copy.formula = nodeText(copy.ast);
                }
                if (copy.arrayRange.isValid()) {
                    copy.arrayRange.topLeft.row += delta;
                    copy.arrayRange.bottomRight.row += delta;
                }
                if (copy.spillAnchor.isValid() && block.contains(copy.spillAnchor)) {
                    copy.spillAnchor.row += delta;
                }
                movedCells.insert(to, copy);
            }
            if (const auto style = styles.constFind(from); style != styles.constEnd()) {
                movedStyles.insert(to, style.value());
            }
            if (const auto comment = comments.constFind(from); comment != comments.constEnd()) {
                movedComments.insert(to, comment.value());
            }
        }
    }
    for (qint32 r = first; r <= last; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            const quint64 key = CellAddr{r, c}.key();
            cells.remove(key);
            styles.remove(key);
            comments.remove(key);
        }
    }
    cells.insert(movedCells);
    styles.insert(movedStyles);
    comments.insert(movedComments);
    QList<CellRange> merges;
    for (CellRange merge : target.merges()) {
        if (intersects(merge, block)) {
            if (!rowMap.contains(merge.topLeft.row)) {
                continue; // its row was a duplicate
            }
            merge.topLeft.row = merge.bottomRight.row = rowMap.value(merge.topLeft.row);
        }
        merges.append(merge);
    }
    // AGENT-NOTE: replaceContents is structure.cpp's tool; Remove
    // Duplicates is a structural move inside one block, so it uses it the
    // same way sortRange does, then rebuilds the dependency index.
    target.replaceContents(std::move(cells), std::move(styles), target.columnWidths(), target.rowHeights(),
                           std::move(merges));
    target.setComments(std::move(comments));
    book.reindexAll();
    return result;
}

bool looksLikeHeaderRow(const Workbook &book, qint32 sheet, const CellRange &whole)
{
    if (sheet < 0 || sheet >= book.sheetCount() || !whole.isValid()) {
        return false;
    }
    const CellRange range = clippedToContent(book.sheetAt(sheet), whole);
    if (!range.isValid() || range.topLeft.row >= range.bottomRight.row) {
        return false;
    }
    for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
        if (!isText(book.cellValue(sheet, range.topLeft.row, c))) {
            continue;
        }
        for (qint32 r = range.topLeft.row + 1; r <= range.bottomRight.row; ++r) {
            if (isNumber(book.cellValue(sheet, r, c))) {
                return true;
            }
        }
    }
    return false;
}

CellRange currentRegion(const Sheet &sheet, const CellAddr &at)
{
    const auto filled = [&sheet](qint32 row, qint32 col) {
        if (row < 0 || col < 0 || row >= Sheet::kMaxRows || col >= Sheet::kMaxCols) {
            return false;
        }
        const Cell *cell = sheet.cellAt({row, col});
        return cell != nullptr && !isBlank(cell->value);
    };
    // A side grows by one line while the line beyond it, spanning the ring's
    // corners, holds something; each check costs that line's length, so a
    // long list costs its rows, not rows × rows.
    const auto rowHas = [&filled](qint32 row, qint32 c0, qint32 c1) {
        for (qint32 c = qMax(0, c0); c <= c1; ++c) {
            if (filled(row, c)) {
                return true;
            }
        }
        return false;
    };
    const auto colHas = [&filled](qint32 col, qint32 r0, qint32 r1) {
        for (qint32 r = qMax(0, r0); r <= r1; ++r) {
            if (filled(r, col)) {
                return true;
            }
        }
        return false;
    };
    CellRange region{at, at};
    for (bool grew = true; grew;) {
        grew = false;
        while (rowHas(region.bottomRight.row + 1, region.topLeft.col - 1, region.bottomRight.col + 1)) {
            ++region.bottomRight.row;
            grew = true;
        }
        while (rowHas(region.topLeft.row - 1, region.topLeft.col - 1, region.bottomRight.col + 1)) {
            --region.topLeft.row;
            grew = true;
        }
        while (colHas(region.bottomRight.col + 1, region.topLeft.row - 1, region.bottomRight.row + 1)) {
            ++region.bottomRight.col;
            grew = true;
        }
        while (colHas(region.topLeft.col - 1, region.topLeft.row - 1, region.bottomRight.row + 1)) {
            --region.topLeft.col;
            grew = true;
        }
    }
    return region;
}

// ---- Consolidate --------------------------------------------------------------

namespace {

struct Accumulator {
    QList<double> numbers;
    int nonBlank = 0;
};

Value summarize(ConsolidateFunction function, const Accumulator &acc)
{
    const QList<double> &n = acc.numbers;
    const auto mean = [&n] {
        double sum = 0;
        for (const double v : n) {
            sum += v;
        }
        return sum / double(n.size());
    };
    const auto sumOfSquares = [&n, &mean] {
        const double m = mean();
        double total = 0;
        for (const double v : n) {
            total += (v - m) * (v - m);
        }
        return total;
    };
    switch (function) {
    case ConsolidateFunction::Sum: {
        double sum = 0;
        for (const double v : n) {
            sum += v;
        }
        return sum;
    }
    case ConsolidateFunction::Count:
        return double(acc.nonBlank);
    case ConsolidateFunction::CountNums:
        return double(n.size());
    case ConsolidateFunction::Average:
        return n.isEmpty() ? Value(Err::Div0) : Value(mean());
    case ConsolidateFunction::Max:
        return n.isEmpty() ? 0.0 : *std::max_element(n.cbegin(), n.cend());
    case ConsolidateFunction::Min:
        return n.isEmpty() ? 0.0 : *std::min_element(n.cbegin(), n.cend());
    case ConsolidateFunction::Product: {
        if (n.isEmpty()) {
            return 0.0;
        }
        double product = 1;
        for (const double v : n) {
            product *= v;
        }
        return product;
    }
    case ConsolidateFunction::StdDev:
        return n.size() < 2 ? Value(Err::Div0) : Value(std::sqrt(sumOfSquares() / double(n.size() - 1)));
    case ConsolidateFunction::StdDevP:
        return n.isEmpty() ? Value(Err::Div0) : Value(std::sqrt(sumOfSquares() / double(n.size())));
    case ConsolidateFunction::Var:
        return n.size() < 2 ? Value(Err::Div0) : Value(sumOfSquares() / double(n.size() - 1));
    case ConsolidateFunction::VarP:
        return n.isEmpty() ? Value(Err::Div0) : Value(sumOfSquares() / double(n.size()));
    }
    return {};
}

// Labels in first-seen order, matched without regard to case; the label
// keeps the value it was first seen with (a year stays a number).
struct Labels {
    QList<Value> values;
    QHash<QString, int> index;
    int indexOf(const Value &label)
    {
        const QString key = toText(label).toCaseFolded();
        if (const auto found = index.constFind(key); found != index.constEnd()) {
            return found.value();
        }
        index.insert(key, int(values.size()));
        values.append(label);
        return int(values.size()) - 1;
    }
};

} // namespace

ConsolidateResult consolidate(const Workbook &book, const ConsolidateOptions &options)
{
    ConsolidateResult result;
    if (options.sources.isEmpty()) {
        result.error = QStringLiteral("Consolidate needs at least one source reference.");
        return result;
    }
    for (const ConsolidateSource &source : options.sources) {
        if (source.sheet < 0 || source.sheet >= book.sheetCount() || !source.range.isValid()) {
            result.error = QStringLiteral("Reference isn't valid.");
            return result;
        }
    }
    Labels rowLabels;
    Labels colLabels;
    QHash<quint64, Accumulator> cells; // (row key << 32 | col key)
    int rows = 0;
    int cols = 0;
    for (const ConsolidateSource &source : options.sources) {
        // A whole-column reference means its data, as in Excel.
        const CellRange r = clippedToContent(book.sheetAt(source.sheet), source.range);
        if (!r.isValid()) {
            continue; // an empty source adds nothing
        }
        const qint32 dataTop = r.topLeft.row + (options.topRow ? 1 : 0);
        const qint32 dataLeft = r.topLeft.col + (options.leftColumn ? 1 : 0);
        // A label is listed when it is written or when data sits under (or
        // beside) a blank one; blank labels share one row or column.
        const auto labelKey = [&book, &source](Labels &labels, qint32 row, qint32 col, bool force) {
            const Value label = book.cellValue(source.sheet, row, col);
            return isBlank(label) && !force ? -1 : labels.indexOf(label);
        };
        QList<int> colKeys;
        for (qint32 c = dataLeft; c <= r.bottomRight.col; ++c) {
            colKeys.append(options.topRow ? labelKey(colLabels, r.topLeft.row, c, false) : int(c - dataLeft));
        }
        for (qint32 row = dataTop; row <= r.bottomRight.row; ++row) {
            int rowKey = options.leftColumn ? labelKey(rowLabels, row, r.topLeft.col, false) : int(row - dataTop);
            for (qint32 c = dataLeft; c <= r.bottomRight.col; ++c) {
                const Value value = book.cellValue(source.sheet, row, c);
                if (isBlank(value)) {
                    continue;
                }
                if (rowKey < 0) {
                    rowKey = labelKey(rowLabels, row, r.topLeft.col, true);
                }
                int &colKey = colKeys[c - dataLeft];
                if (colKey < 0) {
                    colKey = labelKey(colLabels, r.topLeft.row, c, true);
                }
                Accumulator &acc = cells[(quint64(quint32(rowKey)) << 32) | quint32(colKey)];
                ++acc.nonBlank;
                if (const auto *number = std::get_if<double>(&value)) {
                    acc.numbers.append(*number);
                }
            }
        }
        rows = qMax(rows, int(r.bottomRight.row - dataTop + 1));
        cols = qMax(cols, int(r.bottomRight.col - dataLeft + 1));
    }
    const int dataRows = options.leftColumn ? int(rowLabels.values.size()) : rows;
    const int dataCols = options.topRow ? int(colLabels.values.size()) : cols;
    const int top = options.topRow ? 1 : 0;
    const int left = options.leftColumn ? 1 : 0;
    result.grid = QList<QList<Value>>(dataRows + top, QList<Value>(dataCols + left));
    for (int j = 0; options.topRow && j < dataCols; ++j) {
        result.grid[0][j + left] = colLabels.values.at(j);
    }
    for (int i = 0; options.leftColumn && i < dataRows; ++i) {
        result.grid[i + top][0] = rowLabels.values.at(i);
    }
    for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
        const int row = int(it.key() >> 32);
        const int col = int(it.key() & 0xffffffffu);
        result.grid[row + top][col + left] = summarize(options.function, it.value());
    }
    return result;
}

} // namespace QindaOffice::Calc
