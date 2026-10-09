// SPDX-License-Identifier: GPL-3.0-or-later
// Reading a PivotTable back (pivot_edit.h; M74, D-083): GETPIVOTDATA,
// Show Details (the records behind a cell) and what a cell of the output
// belongs to (the app's Field Settings, Sort, Filter and Group act on it).
//
// AGENT-NOTE: GETPIVOTDATA answers only what the pivot shows, as Excel
// does: the named fields must be the leading fields of their axis (Region
// alone names Region's subtotal; Product alone, under Region, names
// nothing), the cell must be on screen (a subtotal turned off, a hidden
// item, an empty cross cell are #REF!), and it returns the shown value
// (Show Values As applied). A data field is named by its caption ("Sum of
// Sales") or its source field ("Sales").

#include "pivot_edit.h"

#include "functions/functions.h"
#include "pivot_cache.h"
#include "pivot_compute.h"
#include "sheet.h"
#include "workbook.h"

namespace QindaOffice::Calc {

namespace {

qint32 fieldNamed(const PivotCache &cache, const PivotTable &pivot, const QString &name)
{
    const QString wanted = name.trimmed();
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        if (pivotFieldCaption(cache, pivot, qint32(f)).compare(wanted, Qt::CaseInsensitive) == 0
            || cache.fields.at(f).name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return qint32(f);
        }
    }
    return -1;
}

qint32 itemFor(const PivotCache &cache, qint32 field, const Value &value)
{
    const QList<Value> items = pivotDisplayItems(cache, field);
    for (qsizetype i = 0; i < items.size(); ++i) {
        const Value &item = items.at(i);
        if (isNumber(value) && isNumber(item)) {
            if (qAbs(std::get<double>(value) - std::get<double>(item)) < 1e-9) {
                return qint32(i);
            }
        } else if (isText(value)) {
            const QString text = std::get<QString>(value).trimmed();
            if (pivotItemCaption(cache, field, qint32(i)).compare(text, Qt::CaseInsensitive) == 0
                || (!isBlank(item) && toText(item).compare(text, Qt::CaseInsensitive) == 0)) {
                return qint32(i);
            }
        } else if (isBool(value) && isBool(item) && std::get<bool>(value) == std::get<bool>(item)) {
            return qint32(i);
        }
    }
    return -1;
}

qint32 dataFieldNamed(const PivotCache &cache, const PivotTable &pivot, const QString &name)
{
    const QString wanted = name.trimmed();
    for (qsizetype d = 0; d < pivot.dataFields.size(); ++d) {
        if (pivot.dataFields.at(d).name.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
            return qint32(d);
        }
    }
    for (qsizetype d = 0; d < pivot.dataFields.size(); ++d) {
        const qint32 f = pivot.dataFields.at(d).field;
        if (f >= 0 && f < cache.fields.size() && cache.fields.at(f).name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return qint32(d);
        }
    }
    return -1;
}

// The path of named items over an axis's leading fields; false when a
// named field is not among them, or leaves a gap.
bool axisPath(const PivotAxis &axis, QHash<qint32, qint32> &given, QVector<qint32> *path)
{
    for (const qint32 f : axis.fields) {
        if (!given.contains(f)) {
            break;
        }
        path->append(given.take(f));
    }
    for (const qint32 f : axis.fields) {
        if (given.contains(f)) {
            return false;
        }
    }
    return true;
}

// True when some line of the axis shows values for `node` (and data field `d`
// when Σ Values is on this axis).
bool shown(const QList<PivotLine> &lines, const PivotAxis &axis, qint32 node, qint32 d)
{
    for (const PivotLine &line : lines) {
        const bool values = (line.type == PivotLine::Type::Data && line.hasValues)
            || (line.type == PivotLine::Type::Subtotal && !line.custom) || line.type == PivotLine::Type::Grand;
        if (values && line.node == node && (axis.valuesAt < 0 || line.dataIndex == d)) {
            return true;
        }
    }
    return false;
}

struct Located {
    const PivotTable *pivot = nullptr;
    const PivotCache *cache = nullptr;
    const PivotRender *render = nullptr;
    const PivotResult *result = nullptr;
};

Located locate(const Workbook &book, const PivotRef &ref)
{
    Located at;
    at.pivot = pivotOf(book, ref);
    if (at.pivot == nullptr || !at.pivot->modelled || !at.pivot->render || !at.pivot->result) {
        return {};
    }
    at.cache = pivotCacheOf(book, at.pivot->cacheId);
    at.render = at.pivot->render.get();
    at.result = at.pivot->result.get();
    return at.cache != nullptr ? at : Located{};
}

} // namespace

Value pivotDataValue(const Workbook &book, qint32 sheet, const CellAddr &addr, const QString &dataField,
                     const QList<QPair<QString, Value>> &pairs)
{
    const Located at = locate(book, pivotAt(book, sheet, addr));
    if (at.pivot == nullptr || at.render->empty) {
        return Err::Ref;
    }
    const qint32 d = dataFieldNamed(*at.cache, *at.pivot, dataField);
    if (d < 0) {
        return Err::Ref;
    }
    QHash<qint32, qint32> given;
    for (const auto &[name, value] : pairs) {
        const qint32 f = fieldNamed(*at.cache, *at.pivot, name);
        const qint32 item = f >= 0 ? itemFor(*at.cache, f, value) : -1;
        if (item < 0) {
            return Err::Ref;
        }
        if (at.pivot->pageFields.contains(f)) {
            const PivotFieldSettings &settings = at.pivot->fields.at(f);
            if (settings.multipleSelection || settings.pageItem != item) {
                return Err::Ref; // a filter answers only for the item it shows
            }
            continue;
        }
        given.insert(f, item);
    }
    QVector<qint32> rowPath;
    QVector<qint32> colPath;
    if (!axisPath(at.result->rows, given, &rowPath) || !axisPath(at.result->cols, given, &colPath) || !given.isEmpty()) {
        return Err::Ref;
    }
    const qint32 row = at.result->rows.find(rowPath);
    const qint32 col = at.result->cols.find(colPath);
    if (row < 0 || col < 0 || !shown(at.render->rowLines, at.result->rows, row, d)
        || !shown(at.render->colLines, at.result->cols, col, d)) {
        return Err::Ref;
    }
    const QVector<Value> *values = at.result->valuesAt(row, col);
    if (values == nullptr || d >= values->size()) {
        return Err::Ref;
    }
    return values->at(d);
}

PivotCellInfo pivotCellInfo(const Workbook &book, const PivotRef &ref, const CellAddr &addr)
{
    PivotCellInfo info;
    const Located at = locate(book, ref);
    if (at.pivot == nullptr || !at.render->extent.contains(addr) || at.render->empty) {
        return info;
    }
    const PivotRender &render = *at.render;
    const PivotResult &result = *at.result;
    if (addr.row < render.body.topLeft.row) {
        const qint32 i = addr.row - (render.body.topLeft.row - render.pageRows - 1);
        if (i >= 0 && i < at.pivot->pageFields.size()) {
            info.page = true;
            info.field = at.pivot->pageFields.at(i);
        }
        return info;
    }
    const qint32 ro = addr.row - render.body.topLeft.row - render.firstDataRow;
    const qint32 co = addr.col - render.body.topLeft.col - render.firstDataCol;
    const bool compact = at.pivot->options.form == PivotForm::Compact;
    // The cache field or Σ Values at an axis position.
    const auto fieldAt = [](const PivotAxis &axis, qint32 position) {
        if (axis.valuesAt >= 0 && position == axis.valuesAt) {
            return kPivotValuesField;
        }
        const qint32 k = position - (axis.valuesAt >= 0 && position > axis.valuesAt ? 1 : 0);
        return k >= 0 && k < axis.fields.size() ? axis.fields.at(k) : -1;
    };
    const auto labelOf = [&](const PivotLine &line, const PivotAxis &axis, qint32 position, bool rows) {
        info.rows = rows;
        if (line.type != PivotLine::Type::Data || position < 0 || position >= line.depth) {
            info.field = line.type == PivotLine::Type::Subtotal ? fieldAt(axis, line.depth - 1) : -1;
            return;
        }
        info.field = fieldAt(axis, position);
        info.item = line.path.at(position);
        info.label = info.field != kPivotValuesField;
        if (info.field == kPivotValuesField) {
            info.dataField = info.item;
            info.item = -1;
        }
    };
    if (ro >= 0 && ro < render.rowLines.size() && co >= 0 && co < render.colLines.size()) {
        const PivotLine &rowLine = render.rowLines.at(ro);
        const PivotLine &colLine = render.colLines.at(co);
        info.value = true;
        info.dataField = result.rows.valuesAt >= 0 ? rowLine.dataIndex
            : result.cols.valuesAt >= 0            ? colLine.dataIndex
                                                   : (result.dataCount > 0 ? 0 : -1);
        info.field = rowLine.depth > 0 ? fieldAt(result.rows, rowLine.depth - 1) : -1;
        return info;
    }
    if (ro >= 0 && ro < render.rowLines.size() && co < 0) {
        const PivotLine &line = render.rowLines.at(ro);
        labelOf(line, result.rows, compact ? line.depth - 1 : co + render.firstDataCol, true);
        return info;
    }
    if (ro < 0 && co >= 0 && co < render.colLines.size()) {
        const qint32 position = addr.row - render.body.topLeft.row - render.firstHeaderRow;
        labelOf(render.colLines.at(co), result.cols, position, false);
        if (position < 0 && result.dataCount == 1) {
            info.dataField = 0; // the caption row's data field name
        }
        return info;
    }
    // The header corner: a row field's caption (compact: the first one).
    if (ro < 0 && co < 0) {
        const qint32 position = compact ? 0 : co + render.firstDataCol;
        info.field = fieldAt(result.rows, position);
        info.rows = true;
        if (info.field < 0 && result.dataCount == 1 && addr.row == render.body.topLeft.row) {
            info.dataField = 0;
        }
    }
    return info;
}

std::optional<PivotDetails> pivotDetails(const Workbook &book, const PivotRef &ref, const CellAddr &addr)
{
    const Located at = locate(book, ref);
    if (at.pivot == nullptr || at.render->empty) {
        return std::nullopt;
    }
    const PivotRender &render = *at.render;
    const PivotResult &result = *at.result;
    const qint32 ro = addr.row - render.body.topLeft.row - render.firstDataRow;
    const qint32 co = addr.col - render.body.topLeft.col - render.firstDataCol;
    const bool onRow = ro >= 0 && ro < render.rowLines.size();
    const bool onCol = co >= 0 && co < render.colLines.size();
    if (!onRow && !onCol) {
        return std::nullopt;
    }
    const qint32 rowNode = onRow ? render.rowLines.at(ro).node : 0;
    const qint32 colNode = onCol ? render.colLines.at(co).node : 0;
    if ((onRow && render.rowLines.at(ro).type == PivotLine::Type::Blank)) {
        return std::nullopt;
    }
    const QVector<qint32> rowPath = result.rows.nodes.at(rowNode).path;
    const QVector<qint32> colPath = result.cols.nodes.at(colNode).path;
    QList<QVector<qint32>> rowItems;
    for (qsizetype k = 0; k < rowPath.size(); ++k) {
        rowItems.append(pivotRecordItems(*at.cache, result.rows.fields.at(k)));
    }
    QList<QVector<qint32>> colItems;
    for (qsizetype k = 0; k < colPath.size(); ++k) {
        colItems.append(pivotRecordItems(*at.cache, result.cols.fields.at(k)));
    }
    PivotDetails details;
    QList<qint32> columns;
    for (qsizetype f = 0; f < at.cache->fields.size(); ++f) {
        if (at.cache->fields.at(f).database) {
            columns.append(qint32(f));
            details.headers.append(at.cache->fields.at(f).name);
            details.formats.append(at.cache->fields.at(f).numberFormat);
        }
    }
    for (qint32 r = 0; r < at.cache->recordCount; ++r) {
        if (r >= result.included.size() || !result.included.at(r)) {
            continue;
        }
        bool match = true;
        for (qsizetype k = 0; match && k < rowPath.size(); ++k) {
            match = rowItems.at(k).value(r, -1) == rowPath.at(k);
        }
        for (qsizetype k = 0; match && k < colPath.size(); ++k) {
            match = colItems.at(k).value(r, -1) == colPath.at(k);
        }
        if (!match) {
            continue;
        }
        QList<Value> values;
        for (const qint32 f : std::as_const(columns)) {
            values.append(pivotRecordValue(*at.cache, f, r));
        }
        details.rows.append(values);
    }
    return details;
}

Value Workbook::pivotData(qint32 sheet, const CellAddr &addr, const QString &dataField,
                          const QList<QPair<QString, Value>> &pairs) const
{
    return pivotDataValue(*this, sheet, addr, dataField, pairs);
}

void registerPivotFunctions(QHash<QString, FunctionSpec> &registry)
{
    // GETPIVOTDATA(data_field, pivot_table, [field1, item1], …): references
    // are kept (preserveRefArgs) — the second argument names the pivot by a
    // cell in it, and any argument may be a cell holding the text.
    registry.insert(
        QStringLiteral("GETPIVOTDATA"),
        FunctionSpec{2, 254, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                         const auto valueOf = [&ctx](const Arg &arg) -> Value {
                             if (const auto *view = std::get_if<RangeView>(&arg)) {
                                 return view->isValid() ? ctx.host->cellValue(view->sheet, view->range.topLeft.row,
                                                                              view->range.topLeft.col)
                                                        : Value(Err::Ref);
                             }
                             return scalarOrValue(arg);
                         };
                         const Value data = valueOf(args.at(0));
                         if (isErr(data)) {
                             return data;
                         }
                         const auto *table = std::get_if<RangeView>(&args.at(1));
                         if (table == nullptr || !table->isValid() || args.size() % 2 != 0) {
                             return Value(Err::Ref);
                         }
                         QList<QPair<QString, Value>> pairs;
                         for (qsizetype i = 2; i + 1 < args.size(); i += 2) {
                             const Value field = valueOf(args.at(i));
                             const Value item = valueOf(args.at(i + 1));
                             if (isErr(field) || isErr(item)) {
                                 return isErr(field) ? field : item;
                             }
                             pairs.append({toText(field), item});
                         }
                         return ctx.host->pivotData(table->sheet, table->range.topLeft, toText(data), pairs);
                     },
                     true});
}

} // namespace QindaOffice::Calc
