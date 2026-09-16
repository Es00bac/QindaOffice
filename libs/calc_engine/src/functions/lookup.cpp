// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include "../parser.h"

namespace QindaOffice::Calc {

void registerLookupFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("CHOOSE"),
             FunctionSpec{1, 254, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto index = scalarNumber(args.at(0));
                              if (!index) return Err::Value;
                              const int i = int(*index);
                              if (i < 1 || i > args.size() - 1) return Err::Value;
                              return scalarOrValue(args.at(i));
                          }});
    FunctionSpec rowSpec{0, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              if (args.isEmpty()) {
                                  return double(ctx.here.row + 1);
                              }
                              const auto *view = std::get_if<RangeView>(&args.at(0));
                              if (view) return double(view->range.topLeft.row + 1);
                              return 1.0;
                          }};
    rowSpec.preserveRefArgs = true;
    r.insert(QStringLiteral("ROW"), rowSpec);
    FunctionSpec columnSpec{
        0, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        if (args.isEmpty()) {
            return double(ctx.here.col + 1);
        }
        const auto *view = std::get_if<RangeView>(&args.at(0));
        if (view) return double(view->range.topLeft.col + 1);
        return 1.0;
        }};
    columnSpec.preserveRefArgs = true;
    r.insert(QStringLiteral("COLUMN"), columnSpec);
    FunctionSpec rowsSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto *view = std::get_if<RangeView>(&args.at(0));
                              if (!view) return Err::Value;
                              return double(view->range.bottomRight.row - view->range.topLeft.row + 1);
                          }};
    rowsSpec.preserveRefArgs = true;
    r.insert(QStringLiteral("ROWS"), rowsSpec);
    FunctionSpec columnsSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto *view = std::get_if<RangeView>(&args.at(0));
                              if (!view) return Err::Value;
                              return double(view->range.bottomRight.col - view->range.topLeft.col + 1);
                          }};
    columnsSpec.preserveRefArgs = true;
    r.insert(QStringLiteral("COLUMNS"), columnsSpec);
    r.insert(QStringLiteral("INDEX"),
             FunctionSpec{1, 4, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              RangeView view;
                              if (std::holds_alternative<RangeView>(args.at(0))) {
                                  view = std::get<RangeView>(args.at(0));
                              } else {
                                  view = {ctx.sheet, {{0, 0}, {0, 0}}};
                              }
                              const auto rowNum = args.size() >= 2 ? scalarNumber(args.at(1))
                                                                   : std::optional<double>();
                              const auto colNum = args.size() >= 3 ? scalarNumber(args.at(2))
                                                                    : std::optional<double>();
                              qint32 row = view.range.topLeft.row;
                              qint32 col = view.range.topLeft.col;
                              if (rowNum.has_value()) {
                                  if (*rowNum < 1) return Err::Value;
                                  row = view.range.topLeft.row + qint32(*rowNum) - 1;
                              }
                              if (colNum.has_value()) {
                                  if (*colNum < 1) return Err::Value;
                                  col = view.range.topLeft.col + qint32(*colNum) - 1;
                              }
                              if (row > view.range.bottomRight.row
                                  || col > view.range.bottomRight.col) {
                                  return Err::Ref;
                              }
                              return ctx.host->cellValue(view.sheet, row, col);
                          }});
    r.insert(QStringLiteral("MATCH"),
             FunctionSpec{2, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const Value needle = scalarOrValue(args.at(0));
                              const auto *view = std::get_if<RangeView>(&args.at(1));
                              if (!view) return Err::NA;
                              int matchType = 1;
                              if (args.size() > 2) {
                                  const auto m = scalarNumber(args.at(2));
                                  if (!m) return Err::Value;
                                  matchType = int(*m);
                              }
                              QVector<Value> values;
                              const Value err = flatRangeValues(ctx, *view, &values);
                              if (isErr(err)) return err;
                              if (matchType == 0) {
                                  for (int i = 0; i < values.size(); ++i) {
                                      if (compareValues(values.at(i), needle) == 0) {
                                          return double(i + 1);
                                      }
                                  }
                                  return Err::NA;
                              }
                              // Ordered match: last element <= (type 1) / >= (type -1) needle.
                              int best = -1;
                              for (int i = 0; i < values.size(); ++i) {
                                  const int cmp = compareValues(values.at(i), needle);
                                  if ((matchType > 0 && cmp <= 0) || (matchType < 0 && cmp >= 0)) {
                                      best = i;
                                  }
                              }
                              return best < 0 ? Value(Err::NA) : Value(double(best + 1));
                          }});
    const auto lookup2d = [](bool vertical) {
        return FunctionSpec{
            3, 4, false,
            [vertical](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                const Value needle = scalarOrValue(args.at(0));
                const auto *tableView = std::get_if<RangeView>(&args.at(1));
                if (!tableView) return Err::Value;
                const auto indexNum = scalarNumber(args.at(2));
                if (!indexNum || *indexNum < 1) return Err::Value;
                bool exact = false;
                if (args.size() > 3) {
                    const auto mode = scalarNumber(args.at(3));
                    if (!mode) return Err::Value;
                    exact = std::abs(*mode) < 0.5;
                }
                const CellRange &t = tableView->range;
                const qint32 keyCol = vertical ? t.topLeft.col : t.topLeft.row;
                const qint32 firstRow = t.topLeft.row;
                const qint32 lastRow = t.bottomRight.row;
                const qint32 firstCol = t.topLeft.col;
                const qint32 lastCol = t.bottomRight.col;
                const qint32 span = vertical ? (lastRow - firstRow + 1) : (lastCol - firstCol + 1);
                qint32 hit = -1;
                for (qint32 i = 0; i < span; ++i) {
                    const Value key = vertical
                        ? ctx.host->cellValue(tableView->sheet, firstRow + i, keyCol)
                        : ctx.host->cellValue(tableView->sheet, keyCol, firstCol + i);
                    if (compareValues(key, needle) == 0) {
                        hit = i;
                        break;
                    }
                    if (!exact) {
                        const int cmp = compareValues(key, needle);
                        if (cmp > 0) break;
                        hit = i;
                    }
                }
                if (hit < 0) return Err::NA;
                const qint32 outRow = vertical ? (firstRow + hit)
                                               : qBound(firstRow, firstRow + qint32(*indexNum) - 1, lastRow);
                const qint32 outCol = vertical
                    ? qBound(firstCol, firstCol + qint32(*indexNum) - 1, lastCol)
                    : (firstCol + hit);
                return ctx.host->cellValue(tableView->sheet, outRow, outCol);
            }};
    };
    r.insert(QStringLiteral("VLOOKUP"), lookup2d(true));
    r.insert(QStringLiteral("HLOOKUP"), lookup2d(false));
    r.insert(QStringLiteral("XLOOKUP"),
             FunctionSpec{3, 6, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const Value needle = scalarOrValue(args.at(0));
                              const auto *lookupView = std::get_if<RangeView>(&args.at(1));
                              const auto *returnView = std::get_if<RangeView>(&args.at(2));
                              if (!lookupView || !returnView) return Err::Value;
                              QVector<Value> keys;
                              const Value err = flatRangeValues(ctx, *lookupView, &keys);
                              if (isErr(err)) return err;
                              int matchMode = 0;
                              qint32 searchFrom = 0;
                              qint32 step = 1;
                              if (args.size() > 4) {
                                  const auto m = scalarNumber(args.at(4));
                                  if (m) matchMode = int(*m);
                              }
                              if (args.size() > 5) {
                                  const auto s = scalarNumber(args.at(5));
                                  if (s && *s < 0) {
                                      searchFrom = keys.size() - 1;
                                      step = -1;
                                  }
                              }
                              int found = -1;
                              if (matchMode == -1) {
                                  // For match_mode -1: find the *largest* value <= needle (last such)
                                  // Need to scan all and keep the last matching position
                                  for (int i = searchFrom; i >= 0 && i < keys.size(); i += step) {
                                      const int cmp = compareValues(keys.at(i), needle);
                                      if (cmp <= 0) {
                                          found = i;  // keep last match
                                      } else if (cmp > 0) {
                                          // Since we're searching in order (searchFrom=0, step=1 for ascending),
                                          // once we hit a value > needle, no later values will match
                                          break;
                                      }
                                  }
                              } else {
                                  for (int i = searchFrom; i >= 0 && i < keys.size(); i += step) {
                                      const int cmp = compareValues(keys.at(i), needle);
                                      if (cmp == 0) {
                                          found = i;
                                          break;
                                      }
                                  }
                              }
                              if (found < 0) {
                                  if (args.size() > 3) {
                                      return scalarOrValue(args.at(3)); // if_not_found
                                  }
                                  return Err::NA;
                              }
                              const CellRange &r = returnView->range;
                              const qint32 rowSpan = r.bottomRight.row - r.topLeft.row;
                              const qint32 colSpan = r.bottomRight.col - r.topLeft.col;
                              const qint32 offset = qBound(qint32(0), qint32(found),
                                                           qMax(rowSpan, colSpan));
                              return rowSpan >= colSpan
                                  ? ctx.host->cellValue(returnView->sheet,
                                                        r.topLeft.row + offset, r.topLeft.col)
                                  : ctx.host->cellValue(returnView->sheet, r.topLeft.row,
                                                        r.topLeft.col + offset);
                          }});
    r.insert(QStringLiteral("OFFSET"),
             FunctionSpec{3, 5, true, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              const auto *base = std::get_if<RangeView>(&args.at(0));
                              if (!base) return Arg(Err::Value);
                              const auto dr = scalarNumber(args.at(1));
                              const auto dc = scalarNumber(args.at(2));
                              if (!dr || !dc) return Arg(Err::Value);
                              RangeView out = *base;
                              out.range.topLeft.row += qint32(*dr);
                              out.range.topLeft.col += qint32(*dc);
                              out.range.bottomRight.row += qint32(*dr);
                              out.range.bottomRight.col += qint32(*dc);
                              if (args.size() > 3) {
                                  const auto h = scalarNumber(args.at(3));
                                  if (!h || *h < 1) return Arg(Err::Value);
                                  out.range.bottomRight.row =
                                      out.range.topLeft.row + qint32(*h) - 1;
                              }
                              if (args.size() > 4) {
                                  const auto w = scalarNumber(args.at(4));
                                  if (!w || *w < 1) return Arg(Err::Value);
                                  out.range.bottomRight.col =
                                      out.range.topLeft.col + qint32(*w) - 1;
                              }
                              return out.isValid() ? Arg(out) : Arg(Err::Ref);
                          }});
    r.insert(QStringLiteral("INDIRECT"),
             FunctionSpec{1, 2, true, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              const QString text = toText(scalarOrValue(args.at(0)));
                              ParseResult parsed = parseFormula(text);
                              if (!parsed.error.isEmpty() || !parsed.root
                                  || parsed.root->kind != Node::Kind::Ref) {
                                  return Arg(Err::Ref);
                              }
                              RangeRef ref = parsed.root->ref;
                              if (ref.sheet.isEmpty()) {
                                  ref.sheet = ctx.host->sheetName(ctx.sheet);
                              }
                              const qint32 sheet = ctx.host->sheetIndex(ref.sheet);
                              if (sheet < 0) return Arg(Err::Ref);
                              return Arg(RangeView{sheet, ref.range});
                          }});
    r.insert(QStringLiteral("ADDRESS"),
             FunctionSpec{2, 5, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const auto row = scalarNumber(args.at(0));
                              const auto col = scalarNumber(args.at(1));
                              if (!row || !col) return Err::Value;
                              int type = 1;
                              if (args.size() > 2) {
                                  const auto t = scalarNumber(args.at(2));
                                  if (t) type = int(*t);
                              }
                              // Excel/LO omit the sheet prefix unless the
                              // caller names a sheet explicitly.
                              const QString sheetName = args.size() > 3
                                  ? toText(scalarOrValue(args.at(3)))
                                  : QString();
                              // Excel: 1=$C$2, 2=C$2 (row abs), 3=$C2 (col abs), 4=C2.
                              const bool absRow = type == 1 || type == 2;
                              const bool absCol = type == 1 || type == 3;
                              QString out = sheetName.isEmpty()
                                  ? QString()
                                  : sheetName + QStringLiteral("!");
                              out += addrText({qint32(*row) - 1, qint32(*col) - 1}, absRow, absCol);
                              return out;
                          }});
    r.insert(QStringLiteral("LOOKUP"),
             FunctionSpec{2, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const Value needle = scalarOrValue(args.at(0));
                              const auto *lookupView = std::get_if<RangeView>(&args.at(1));
                              if (!lookupView) return Err::Value;
                              const RangeView returnView =
                                  args.size() > 2 && std::holds_alternative<RangeView>(args.at(2))
                                  ? std::get<RangeView>(args.at(2))
                                  : *lookupView;
                              QVector<Value> keys;
                              const Value err = flatRangeValues(ctx, *lookupView, &keys);
                              if (isErr(err)) return err;
                              int best = -1;
                              for (int i = 0; i < keys.size(); ++i) {
                                  if (compareValues(keys.at(i), needle) <= 0) best = i;
                                  else break;
                              }
                              if (best < 0) return Err::NA;
                              const qint32 offset = best;
                              const qint32 r = returnView.range.topLeft.row
                                  + qMin(offset, returnView.range.bottomRight.row
                                                      - returnView.range.topLeft.row);
                              const qint32 c = returnView.range.topLeft.col
                                  + qMin(offset, returnView.range.bottomRight.col
                                                      - returnView.range.topLeft.col);
                              return ctx.host->cellValue(returnView.sheet, r, c);
                          }});
}

} // namespace QindaOffice::Calc
