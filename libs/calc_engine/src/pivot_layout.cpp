// SPDX-License-Identifier: GPL-3.0-or-later
// A computed PivotTable laid out in cells (pivot_compute.h; M74, D-083):
// the filter rows, the header rows, the label columns and the values, and
// the lines Excel stores as rowItems / colItems.
//
// AGENT-NOTE: Excel opens a PivotTable without a refresh only when its
// location ref, firstHeaderRow/firstDataRow/firstDataCol and the
// rowItems / colItems lines agree with each other: body rows =
// firstDataRow + rowItems, body columns = firstDataCol + colItems. Both
// come from here, so they cannot disagree. The shapes, from Excel's own
// files: a caption row ("Sum of Sales" | "Column Labels") only when a real
// field is on Columns; firstHeaderRow is the first column-label row (1
// with no column field, by Excel's convention); one line `<i/>` stands for
// an axis with no field; compact and outline give a group its own line
// (r = its depth), tabular puts a group's label on its first item's line
// (r = what the line shares with the one before); columns always lay out
// the tabular way; the field right above Σ Values has no subtotal.
//
// AGENT-GUARD: an empty PivotTable (nothing on any area) keeps Excel's
// placeholder: a 3 x 18 body and no lines — what Excel itself writes.

#include "pivot_compute.h"

#include <QObject>

namespace QindaOffice::Calc {

namespace {

struct Ctx {
    qint32 node = 0;
    qint32 data = -1;
    qint32 depth = 0;
    QVector<qint32> path;
};

struct AxisGen {
    const PivotCache &cache;
    const PivotTable &pivot;
    const PivotResult &result;
    const PivotAxis &axis;
    bool rowsAxis = true;
    bool tabular = true;
    QList<PivotLine> lines;
    QVector<qint32> previous;

    [[nodiscard]] qint32 positions() const { return qint32(axis.fields.size()) + (axis.valuesAt >= 0 ? 1 : 0); }
    [[nodiscard]] bool isValues(qint32 position) const { return axis.valuesAt >= 0 && position == axis.valuesAt; }
    [[nodiscard]] qint32 fieldAt(qint32 position) const
    {
        if (isValues(position)) {
            return kPivotValuesField;
        }
        return axis.fields.at(position - (axis.valuesAt >= 0 && position > axis.valuesAt ? 1 : 0));
    }

    [[nodiscard]] QList<Ctx> children(const Ctx &ctx) const
    {
        QList<Ctx> out;
        if (ctx.depth >= positions()) {
            return out;
        }
        if (isValues(ctx.depth)) {
            for (qint32 d = 0; d < result.dataCount; ++d) {
                Ctx child{ctx.node, d, ctx.depth + 1, ctx.path};
                child.path.append(d);
                out.append(child);
            }
            return out;
        }
        for (const qint32 node : axis.nodes.at(ctx.node).children) {
            Ctx child{node, ctx.data, ctx.depth + 1, ctx.path};
            child.path.append(axis.nodes.at(node).item);
            out.append(child);
        }
        return out;
    }

    // Excel's <x v>: a position in the field's item order (Σ Values: the data field).
    [[nodiscard]] qint32 xOf(qint32 position, qint32 item) const
    {
        const qint32 f = fieldAt(position);
        if (f == kPivotValuesField) {
            return item;
        }
        const qsizetype at = pivot.fields.at(f).order.indexOf(item);
        return at >= 0 ? qint32(at) : item;
    }

    [[nodiscard]] const PivotFieldSettings *settingsAt(qint32 position) const
    {
        const qint32 f = fieldAt(position);
        return f == kPivotValuesField ? nullptr : &pivot.fields.at(f);
    }

    [[nodiscard]] bool hasSubtotal(const Ctx &ctx) const
    {
        const qint32 position = ctx.depth - 1;
        const PivotFieldSettings *settings = settingsAt(position);
        if (settings == nullptr || (axis.valuesAt >= 0 && position == axis.valuesAt - 1)) {
            return false; // Σ Values has none; nor does the field right above it
        }
        return settings->subtotals || !settings->customSubtotals.isEmpty();
    }

    void push(PivotLine line)
    {
        previous = line.path;
        lines.append(line);
    }

    void emitData(const Ctx &ctx, bool group, bool hasValues)
    {
        PivotLine line;
        line.depth = ctx.depth;
        line.path = ctx.path;
        line.node = ctx.node;
        line.dataIndex = qMax(0, ctx.data);
        line.group = group;
        line.hasValues = hasValues;
        qint32 shared = 0;
        while (shared < ctx.depth - 1 && shared < previous.size() && previous.at(shared) == ctx.path.at(shared)) {
            ++shared;
        }
        line.repeat = tabular ? shared : ctx.depth - 1;
        for (qint32 j = line.repeat; j < ctx.depth; ++j) {
            line.items.append(xOf(j, ctx.path.at(j)));
        }
        push(line);
    }

    void emitSubtotals(const Ctx &ctx)
    {
        const PivotFieldSettings *settings = settingsAt(ctx.depth - 1);
        const QList<PivotFunction> functions = settings->customSubtotals.isEmpty() ? QList<PivotFunction>{PivotFunction::Sum}
                                                                                    : settings->customSubtotals;
        QList<qint32> dataIndexes{qMax(0, ctx.data)};
        if (axis.valuesAt >= ctx.depth) { // Σ Values below: one subtotal per data field
            dataIndexes.clear();
            for (qint32 d = 0; d < result.dataCount; ++d) {
                dataIndexes.append(d);
            }
        }
        for (const PivotFunction function : functions) {
            for (const qint32 d : dataIndexes) {
                PivotLine line;
                line.type = PivotLine::Type::Subtotal;
                line.custom = !settings->customSubtotals.isEmpty();
                line.function = function;
                line.depth = ctx.depth;
                line.path = ctx.path;
                line.node = ctx.node;
                line.dataIndex = d;
                line.repeat = ctx.depth - 1;
                line.items.append(xOf(ctx.depth - 1, ctx.path.last()));
                push(line);
            }
        }
    }

    void emitBlank(const Ctx &ctx)
    {
        PivotLine line;
        line.type = PivotLine::Type::Blank;
        line.depth = ctx.depth;
        line.path = ctx.path;
        line.node = ctx.node;
        line.repeat = ctx.depth - 1;
        line.hasValues = false;
        line.items.append(xOf(ctx.depth - 1, ctx.path.last()));
        lines.append(line); // a blank line does not count as the line before
    }

    [[nodiscard]] bool blankAfter(const Ctx &ctx) const
    {
        const PivotFieldSettings *settings = settingsAt(ctx.depth - 1);
        return rowsAxis && settings != nullptr && settings->insertBlankRow;
    }

    void generateOutline(const Ctx &ctx)
    {
        for (const Ctx &child : children(ctx)) {
            if (children(child).isEmpty()) {
                emitData(child, false, true);
            } else {
                const PivotFieldSettings *settings = settingsAt(child.depth - 1);
                const bool subtotal = hasSubtotal(child);
                const bool top = subtotal && settings != nullptr && settings->subtotalTop
                    && settings->customSubtotals.isEmpty() && axis.valuesAt < child.depth;
                emitData(child, true, top);
                generateOutline(child);
                if (subtotal && !top) {
                    emitSubtotals(child);
                }
            }
            if (blankAfter(child)) {
                emitBlank(child);
            }
        }
    }

    void generateTabular(const Ctx &ctx)
    {
        for (const Ctx &child : children(ctx)) {
            if (children(child).isEmpty()) {
                emitData(child, false, true);
            } else {
                generateTabular(child);
                if (hasSubtotal(child)) {
                    emitSubtotals(child);
                }
            }
            if (blankAfter(child)) {
                emitBlank(child);
            }
        }
    }

    void generate(bool grandTotals)
    {
        if (positions() == 0) {
            PivotLine whole; // `<i/>`: the axis has no field
            lines.append(whole);
            return;
        }
        if (tabular) {
            generateTabular(Ctx{});
        } else {
            generateOutline(Ctx{});
        }
        if (!grandTotals || axis.fields.isEmpty()) {
            return;
        }
        const qint32 grands = axis.valuesAt >= 0 ? result.dataCount : 1;
        for (qint32 d = 0; d < grands; ++d) {
            PivotLine line;
            line.type = PivotLine::Type::Grand;
            line.dataIndex = d;
            line.items.append(d);
            lines.append(line);
        }
    }
};

} // namespace

PivotRender layoutPivot(const PivotCache &cache, const PivotTable &pivot, const PivotResult &result,
                        const CellAddr &location)
{
    PivotRender render;
    const PivotOptions &options = pivot.options;
    const bool compact = options.form == PivotForm::Compact;
    AxisGen rows{cache, pivot, result, result.rows, true, options.form == PivotForm::Tabular, {}, {}};
    AxisGen cols{cache, pivot, result, result.cols, false, true, {}, {}};
    const qint32 rowPositions = rows.positions();
    const qint32 colPositions = cols.positions();
    const qint32 dataCount = result.dataCount;
    QList<qint32> pages;
    for (const qint32 f : pivot.pageFields) {
        if (f >= 0 && f < cache.fields.size() && f < pivot.fields.size()) {
            pages.append(f);
        }
    }
    CellAddr top = location;
    if (!pages.isEmpty()) {
        top.row = qMax(top.row, qint32(pages.size()) + 1); // the filters and a blank row fit above
    }
    render.pageRows = qint32(pages.size());
    const auto put = [&render](const CellAddr &at, const Value &value, PivotCellKind kind, const QString &format = {},
                               quint8 indent = 0, bool caption = false) {
        PivotCell cell;
        cell.value = value;
        cell.kind = kind;
        cell.numberFormat = format;
        cell.indent = indent;
        cell.caption = caption || kind == PivotCellKind::Header || kind == PivotCellKind::PageLabel;
        render.cells.insert(at.key(), cell);
    };
    for (qsizetype i = 0; i < pages.size(); ++i) {
        const qint32 f = pages.at(i);
        const PivotFieldSettings &settings = pivot.fields.at(f);
        const CellAddr label{top.row - qint32(pages.size()) - 1 + qint32(i), top.col};
        put(label, pivotFieldCaption(cache, pivot, f), PivotCellKind::PageLabel);
        Value shown = QObject::tr("(All)");
        if (settings.multipleSelection && !settings.hidden.isEmpty()) {
            shown = QObject::tr("(Multiple Items)");
        } else if (!settings.multipleSelection && settings.pageItem >= 0) {
            shown = pivotItemCaption(cache, f, settings.pageItem);
        }
        put({label.row, label.col + 1}, shown, PivotCellKind::PageValue, {}, 0,
            settings.multipleSelection || settings.pageItem < 0);
    }
    const CellAddr extentTop{pages.isEmpty() ? top.row : top.row - qint32(pages.size()) - 1, top.col};
    if (rowPositions == 0 && colPositions == 0 && dataCount == 0) {
        render.empty = true;
        render.firstDataCol = 0; // Excel's placeholder: A3:C20, first data column 0
        render.body = {top, {top.row + 17, top.col + 2}};
        render.extent = {extentTop, {render.body.bottomRight.row, qMax(render.body.bottomRight.col, top.col + 1)}};
        return render;
    }
    rows.generate(options.rowGrandTotals);
    if (colPositions > 0 || dataCount > 0) {
        cols.generate(options.colGrandTotals);
    }
    render.rowLines = rows.lines;
    render.colLines = cols.lines;
    const qint32 labelCols = rowPositions > 0 ? (compact ? 1 : rowPositions) : (result.cols.fields.isEmpty() ? 0 : 1);
    const qint32 captionRow = result.cols.fields.isEmpty() ? 0 : 1;
    render.firstHeaderRow = colPositions > 0 ? captionRow : 1;
    render.firstDataRow = colPositions > 0 ? captionRow + colPositions : 1;
    render.firstDataCol = labelCols;
    const qint32 bodyRows = render.firstDataRow + qint32(render.rowLines.size());
    const qint32 bodyCols = qMax(1, render.firstDataCol + qint32(render.colLines.size()));
    render.body = {top, {top.row + bodyRows - 1, top.col + bodyCols - 1}};
    render.extent = {extentTop, {render.body.bottomRight.row, qMax(render.body.bottomRight.col,
                                                                    pages.isEmpty() ? top.col : top.col + 1)}};
    const auto at = [&top](qint32 row, qint32 col) { return CellAddr{top.row + row, top.col + col}; };
    const auto dataName = [&pivot](qint32 d) {
        return d >= 0 && d < pivot.dataFields.size() ? pivot.dataFields.at(d).name : QString();
    };
    // A label as the cell holds it: the item's own value (numbers keep their format).
    const auto label = [&](const AxisGen &gen, qint32 position, qint32 item, QString *format) -> Value {
        const qint32 f = gen.fieldAt(position);
        if (f == kPivotValuesField) {
            return dataName(item);
        }
        const QList<Value> items = pivotDisplayItems(cache, f);
        const Value value = item >= 0 && item < items.size() ? items.at(item) : Value{};
        if (isBlank(value)) {
            return QObject::tr("(blank)");
        }
        if (isNumber(value) && !cache.fields.at(f).grouped()) {
            *format = cache.fields.at(f).numberFormat;
        }
        return value;
    };
    const auto captionOf = [&](const AxisGen &gen, qint32 position, qint32 item) {
        const qint32 f = gen.fieldAt(position);
        return f == kPivotValuesField ? dataName(item) : pivotItemCaption(cache, f, item);
    };
    const auto lineText = [&](const AxisGen &gen, const PivotLine &line) -> QString {
        if (line.type == PivotLine::Type::Grand) {
            if (gen.axis.valuesAt >= 0) {
                return QObject::tr("Total %1").arg(dataName(line.dataIndex));
            }
            return options.grandTotalCaption.isEmpty() ? QObject::tr("Grand Total") : options.grandTotalCaption;
        }
        const QString item = captionOf(gen, line.depth - 1, line.path.last());
        if (line.custom) {
            return QStringLiteral("%1 %2").arg(item, pivotFunctionName(line.function));
        }
        if (gen.axis.valuesAt >= line.depth) {
            return QStringLiteral("%1 %2").arg(item, dataName(line.dataIndex));
        }
        return QObject::tr("%1 Total").arg(item);
    };
    // Header rows: the caption row, the column labels, the row field headers.
    if (captionRow == 1 && dataCount == 1 && labelCols > 0) {
        put(at(0, 0), dataName(0), PivotCellKind::Header);
    }
    if (captionRow == 1 && options.showHeaders) {
        if (compact) {
            put(at(0, render.firstDataCol),
                options.colHeaderCaption.isEmpty() ? QObject::tr("Column Labels") : options.colHeaderCaption,
                PivotCellKind::Header);
        } else {
            for (qint32 j = 0; j < colPositions; ++j) {
                put(at(0, render.firstDataCol + j), pivotFieldCaption(cache, pivot, cols.fieldAt(j)), PivotCellKind::Header);
            }
        }
    }
    if (colPositions == 0 && dataCount == 1) {
        put(at(0, render.firstDataCol), dataName(0), PivotCellKind::Header);
    }
    const qint32 cornerRow = render.firstDataRow - 1;
    if (rowPositions > 0 && options.showHeaders) {
        if (compact) {
            put(at(cornerRow, 0), options.rowHeaderCaption.isEmpty() ? QObject::tr("Row Labels") : options.rowHeaderCaption,
                PivotCellKind::Header);
        } else {
            for (qint32 j = 0; j < rowPositions; ++j) {
                put(at(cornerRow, j), pivotFieldCaption(cache, pivot, rows.fieldAt(j)), PivotCellKind::Header);
            }
        }
    }
    for (qsizetype ci = 0; ci < render.colLines.size(); ++ci) {
        const PivotLine &line = render.colLines.at(ci);
        const qint32 col = render.firstDataCol + qint32(ci);
        if (line.type == PivotLine::Type::Grand) {
            put(at(render.firstHeaderRow, col), lineText(cols, line), PivotCellKind::ColumnLabel, {}, 0, true);
        } else if (line.type == PivotLine::Type::Subtotal) {
            put(at(render.firstHeaderRow + line.depth - 1, col), lineText(cols, line), PivotCellKind::ColumnLabel, {}, 0,
                true);
        } else {
            for (qint32 j = line.repeat; j < line.depth; ++j) {
                QString format;
                const Value value = label(cols, j, line.path.at(j), &format);
                put(at(render.firstHeaderRow + j, col), value, PivotCellKind::ColumnLabel, format);
            }
        }
    }
    // Label columns and values, line by line.
    for (qsizetype ri = 0; ri < render.rowLines.size(); ++ri) {
        const PivotLine &line = render.rowLines.at(ri);
        const qint32 row = render.firstDataRow + qint32(ri);
        if (line.type == PivotLine::Type::Grand) {
            put(at(row, 0), lineText(rows, line), PivotCellKind::RowLabel, {}, 0, true);
        } else if (line.type == PivotLine::Type::Subtotal) {
            const qint32 k = line.depth - 1;
            put(at(row, compact ? 0 : k), lineText(rows, line), PivotCellKind::RowLabel, {}, quint8(compact ? k : 0),
                true);
        } else if (line.type == PivotLine::Type::Data && line.depth > 0) {
            const qint32 first = compact ? line.depth - 1 : line.repeat;
            for (qint32 j = 0; j < line.depth && !compact; ++j) {
                const PivotFieldSettings *settings = rows.settingsAt(j);
                if (j < first && (settings == nullptr || !settings->repeatLabels)) {
                    continue; // shown on an earlier line
                }
                QString format;
                const Value value = label(rows, j, line.path.at(j), &format);
                put(at(row, j), value, PivotCellKind::RowLabel, format);
            }
            if (compact) {
                QString format;
                const Value value = label(rows, first, line.path.at(first), &format);
                put(at(row, 0), value, PivotCellKind::RowLabel, format, quint8(first));
            }
        }
        if (line.type == PivotLine::Type::Blank || !line.hasValues || dataCount == 0) {
            continue;
        }
        for (qsizetype ci = 0; ci < render.colLines.size(); ++ci) {
            const PivotLine &colLine = render.colLines.at(ci);
            const qint32 d = result.rows.valuesAt >= 0 ? line.dataIndex
                : result.cols.valuesAt >= 0            ? colLine.dataIndex
                                                       : 0;
            const quint64 key = PivotResult::key(line.node, colLine.node);
            Value value;
            bool found = false;
            if (line.custom || colLine.custom) {
                const auto acc = result.accs.constFind(key);
                if (acc != result.accs.constEnd() && d < acc->size()) {
                    value = pivotFinalize(acc->at(d), line.custom ? line.function : colLine.function);
                    found = true;
                }
            } else if (const QVector<Value> *values = result.valuesAt(line.node, colLine.node); values != nullptr
                       && d < values->size()) {
                value = values->at(d);
                found = true;
            }
            const CellAddr cell = at(row, render.firstDataCol + qint32(ci));
            if (!found) {
                if (options.showMissing && !options.missingCaption.isEmpty()) {
                    put(cell, options.missingCaption, PivotCellKind::Data, {}, 0, true);
                }
                continue;
            }
            put(cell, value, PivotCellKind::Data, d < pivot.dataFields.size() ? pivot.dataFields.at(d).numberFormat
                                                                                : QString());
        }
    }
    return render;
}

} // namespace QindaOffice::Calc
