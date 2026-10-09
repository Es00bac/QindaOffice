// SPDX-License-Identifier: GPL-3.0-or-later
// Show Values As (pivot_compute.h; M74, D-083): % of Grand Total, % of
// Column / Row Total, % Of, % of Parent Row / Column / Total, Difference
// From, % Difference From, Running Total In, % Running Total In, Rank
// Smallest / Largest, Index.
//
// AGENT-NOTE: Excel's rules where a calculation has no answer, written
// down here because the help pages do not say them all: a cell above the
// base field's level (a higher subtotal, the grand total) shows nothing
// for the calculations that need a base field; the base item's own cells
// show nothing for Difference From and % Difference From and 100% for
// % Of; a base item with no data in that row or column gives #N/A; a zero
// denominator #DIV/0!; (previous) of the first item and (next) of the last
// compare the item with itself.

#include "pivot_compute.h"

#include <optional>

namespace QindaOffice::Calc {

namespace {

struct ShowAs {
    const PivotResult &result;
    const PivotDataField &data;
    qsizetype d;
    bool onRows = false; // the base field's axis
    qint32 level = -1;   // its position among that axis's fields

    [[nodiscard]] const PivotAxis &baseAxis() const { return onRows ? result.rows : result.cols; }

    // The raw value of a cell; nullopt when no record reached it.
    [[nodiscard]] std::optional<Value> raw(qint32 row, qint32 col) const
    {
        const auto it = result.raw.constFind(PivotResult::key(row, col));
        if (it == result.raw.constEnd() || d >= it->size()) {
            return std::nullopt;
        }
        return it->at(d);
    }

    [[nodiscard]] static Value ratio(const Value &v, const Value &base)
    {
        if (isErr(v)) {
            return v;
        }
        if (isErr(base)) {
            return base;
        }
        const auto *a = std::get_if<double>(&v);
        const auto *b = std::get_if<double>(&base);
        if (a == nullptr || b == nullptr) {
            return Value{};
        }
        return *b == 0 ? Value(Err::Div0) : Value(*a / *b);
    }

    // The node on the base axis with the base field's item replaced.
    [[nodiscard]] qint32 swapped(qint32 node, qint32 item) const
    {
        QVector<qint32> path = baseAxis().nodes.at(node).path;
        path[level] = item;
        return baseAxis().find(path);
    }

    // The value of the same cell with the base axis node replaced.
    [[nodiscard]] std::optional<Value> at(qint32 row, qint32 col, qint32 baseNode) const
    {
        return onRows ? raw(baseNode, col) : raw(row, baseNode);
    }

    // The item's siblings at the base level (same path above it), in display order.
    [[nodiscard]] QList<qint32> siblings(qint32 node) const
    {
        const PivotAxis &axis = baseAxis();
        qint32 ancestor = node;
        while (axis.nodes.at(ancestor).level > level) {
            ancestor = axis.nodes.at(ancestor).parent;
        }
        QList<qint32> items;
        for (const qint32 child : axis.nodes.at(axis.nodes.at(ancestor).parent).children) {
            items.append(axis.nodes.at(child).item);
        }
        return items;
    }

    [[nodiscard]] Value shown(qint32 row, qint32 col, const Value &v) const
    {
        const PivotShowAs as = data.showAs;
        switch (as) {
        case PivotShowAs::Normal: return v;
        case PivotShowAs::PercentOfTotal: return ratio(v, raw(0, 0).value_or(Value{}));
        case PivotShowAs::PercentOfColumn: return ratio(v, raw(0, col).value_or(Value{}));
        case PivotShowAs::PercentOfRow: return ratio(v, raw(row, 0).value_or(Value{}));
        case PivotShowAs::PercentOfParentRow:
            return ratio(v, raw(row == 0 ? 0 : result.rows.nodes.at(row).parent, col).value_or(Value{}));
        case PivotShowAs::PercentOfParentColumn:
            return ratio(v, raw(row, col == 0 ? 0 : result.cols.nodes.at(col).parent).value_or(Value{}));
        case PivotShowAs::Index: {
            const Value grand = raw(0, 0).value_or(Value{});
            const Value rowTotal = raw(row, 0).value_or(Value{});
            const Value colTotal = raw(0, col).value_or(Value{});
            const Value numerator = isNumber(v) && isNumber(grand)
                ? Value(std::get<double>(v) * std::get<double>(grand))
                : (isErr(v) ? v : grand);
            const Value denominator = isNumber(rowTotal) && isNumber(colTotal)
                ? Value(std::get<double>(rowTotal) * std::get<double>(colTotal))
                : (isErr(rowTotal) ? rowTotal : colTotal);
            return ratio(numerator, denominator);
        }
        default:
            break;
        }
        if (level < 0) {
            return Err::NA; // the base field is on neither axis
        }
        const qint32 node = onRows ? row : col;
        const PivotAxisNode &n = baseAxis().nodes.at(node);
        if (n.path.size() < level + 1) {
            return Value{}; // above the base field: nothing to compare
        }
        const qint32 item = n.path.at(level);
        if (as == PivotShowAs::PercentOfParent) {
            QVector<qint32> parentPath = n.path.mid(0, level + 1);
            return ratio(v, at(row, col, baseAxis().find(parentPath)).value_or(Value{}));
        }
        const QList<qint32> items = siblings(node);
        const qsizetype position = items.indexOf(item);
        if (as == PivotShowAs::RunningTotal || as == PivotShowAs::PercentRunningTotal || as == PivotShowAs::RankAscending
            || as == PivotShowAs::RankDescending) {
            double running = 0;
            double whole = 0;
            QList<double> others;
            for (qsizetype i = 0; i < items.size(); ++i) {
                const qint32 other = swapped(node, items.at(i));
                const Value value = other >= 0 ? at(row, col, other).value_or(Value{}) : Value{};
                if (isErr(value)) {
                    return value;
                }
                const double number = isNumber(value) ? std::get<double>(value) : 0.0;
                whole += number;
                running += i <= position ? number : 0.0;
                if (isNumber(value)) {
                    others.append(number);
                }
            }
            if (as == PivotShowAs::RunningTotal) {
                return running;
            }
            if (as == PivotShowAs::PercentRunningTotal) {
                return whole == 0 ? Value(Err::Div0) : Value(running / whole);
            }
            if (!isNumber(v)) {
                return Value{};
            }
            const double mine = std::get<double>(v);
            qint32 rank = 1;
            for (const double other : std::as_const(others)) {
                rank += (as == PivotShowAs::RankDescending ? other > mine : other < mine) ? 1 : 0;
            }
            return double(rank);
        }
        // % Of, Difference From, % Difference From.
        qint32 baseItem = data.baseItem;
        if (baseItem == kPivotPreviousItem || baseItem == kPivotNextItem) {
            const qsizetype other = baseItem == kPivotPreviousItem ? position - 1 : position + 1;
            baseItem = other >= 0 && other < items.size() ? items.at(other) : item;
        }
        if (baseItem == item) {
            return as == PivotShowAs::PercentOf ? Value(1.0) : Value{};
        }
        const qint32 baseNode = swapped(node, baseItem);
        const std::optional<Value> base = baseNode >= 0 ? at(row, col, baseNode) : std::nullopt;
        if (!base.has_value() || isBlank(*base)) {
            return Err::NA;
        }
        if (as == PivotShowAs::PercentOf) {
            return ratio(v, *base);
        }
        if (isErr(v) || isErr(*base)) {
            return isErr(v) ? v : *base;
        }
        if (!isNumber(v) || !isNumber(*base)) {
            return Value{};
        }
        const double difference = std::get<double>(v) - std::get<double>(*base);
        if (as == PivotShowAs::Difference) {
            return difference;
        }
        return std::get<double>(*base) == 0 ? Value(Err::Div0) : Value(difference / std::get<double>(*base));
    }
};

} // namespace

void applyPivotShowAs(PivotResult &result, const PivotCache &cache, const PivotTable &pivot)
{
    Q_UNUSED(cache);
    result.values = result.raw;
    for (qsizetype d = 0; d < pivot.dataFields.size(); ++d) {
        const PivotDataField &data = pivot.dataFields.at(d);
        if (data.showAs == PivotShowAs::Normal) {
            continue;
        }
        ShowAs show{result, data, d};
        if (const qsizetype r = result.rows.fields.indexOf(data.baseField); r >= 0) {
            show.onRows = true;
            show.level = qint32(r);
        } else if (const qsizetype c = result.cols.fields.indexOf(data.baseField); c >= 0) {
            show.level = qint32(c);
        }
        for (auto it = result.raw.constBegin(); it != result.raw.constEnd(); ++it) {
            if (d >= it->size()) {
                continue;
            }
            const qint32 row = qint32(it.key() >> 32);
            const qint32 col = qint32(it.key() & 0xffffffffu);
            result.values[it.key()][d] = show.shown(row, col, it->at(d));
        }
    }
}

} // namespace QindaOffice::Calc
