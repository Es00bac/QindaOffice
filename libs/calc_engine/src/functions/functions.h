// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Function registry (plan section 7.2): QHash<QString, FunctionSpec> keyed
// by upper-case name, one registration function per category file. Every
// function declares argument bounds and volatility; range arguments arrive
// as RangeView. A range in a single-value slot is read the way the formula's
// ArrayMode says (M69, D-078: implicit intersection or lifting, eval_lift.h);
// before M69 it was always #VALUE!.

#include "../value.h"

#include <QDate>
#include <QHash>

#include <functional>
#include <memory>

namespace QindaOffice::Calc {

struct Node;
struct TableSpec;
class Workbook;

// A lazy range argument: resolved through the eval host on demand.
struct RangeView {
    qint32 sheet = 0;
    CellRange range;
    [[nodiscard]] bool isValid() const { return range.isValid(); }
};

// A materialized array: the result of array arithmetic in an argument
// (SUMPRODUCT(range, (1+x)^range) — M38). Only ever lives inside argument
// evaluation — Value itself has no array case, so cell storage, snapshots
// and number formats are untouched.
struct ArrayValue {
    qint32 rows = 0;
    qint32 cols = 0;
    QVector<Value> cells; // row-major, rows*cols entries

    [[nodiscard]] bool isSingleCell() const { return rows == 1 && cols == 1; }
};

using Arg = std::variant<Value, RangeView, ArrayValue>;

// Abstract cell access so eval stays workbook-implementation-agnostic.
class EvalHost {
public:
    virtual ~EvalHost() = default;
    [[nodiscard]] virtual Value cellValue(qint32 sheet, qint32 row, qint32 col) const = 0;
    // Sparse read: every non-blank cell in the range (unordered). Lets
    // aggregations over whole rows/columns skip empty space.
    [[nodiscard]] virtual QVector<QPair<CellAddr, Value>> nonBlankCellsIn(
        qint32 sheet, const CellRange &range) const = 0;
    [[nodiscard]] virtual qint32 sheetIndex(const QString &name) const = 0;
    [[nodiscard]] virtual QString sheetName(qint32 index) const = 0;
    [[nodiscard]] virtual qint32 sheetCount() const = 0;
    // Named cell/range lookup; nullopt when the name is unknown.
    [[nodiscard]] virtual std::optional<RangeRef> namedRange(const QString &name) const = 0;
    // A defined name bound to a formula, a constant or one sheet (D-069):
    // the parsed body to evaluate in the name's place when it is used on
    // `sheet`; null when namedRange answers instead (or nobody does).
    [[nodiscard]] virtual std::shared_ptr<const Node> nameFormula(const QString &name, qint32 sheet) const
    {
        Q_UNUSED(name);
        Q_UNUSED(sheet);
        return {};
    }
    // The spill of the dynamic-array anchor at (sheet, anchor), for the
    // A1# spill reference (ANCHORARRAY, M66/D-075); nullopt when the cell
    // spills nothing (or is blocked).
    [[nodiscard]] virtual std::optional<CellRange> spillExtent(qint32 sheet, const CellAddr &anchor) const
    {
        Q_UNUSED(sheet);
        Q_UNUSED(anchor);
        return std::nullopt;
    }
    // The text of the formula in a cell, without '='; empty for a value or
    // an empty cell (ISFORMULA, FORMULATEXT — M61).
    [[nodiscard]] virtual QString cellFormula(qint32 sheet, qint32 row, qint32 col) const
    {
        Q_UNUSED(sheet);
        Q_UNUSED(row);
        Q_UNUSED(col);
        return {};
    }
    // Excel tables (M69, D-078) for structured references: a table by name
    // (case-insensitive; `sheet` receives its sheet) and the table holding a
    // cell; nullptr when there is none. Borrowed: valid until the next
    // change to that sheet's tables.
    [[nodiscard]] virtual const TableSpec *tableNamed(const QString &name, qint32 *sheet) const
    {
        Q_UNUSED(name);
        Q_UNUSED(sheet);
        return nullptr;
    }
    [[nodiscard]] virtual const TableSpec *tableAt(qint32 sheet, const CellAddr &addr) const
    {
        Q_UNUSED(sheet);
        Q_UNUSED(addr);
        return nullptr;
    }
};

// What a formula does with a range or an array where one value is wanted
// (M69, D-078; eval_lift.cpp).
//   Legacy  — a formula Excel stored without the dynamic-array mark: Excel's
//             implicit intersection (the cell in the formula's own row or
//             column — what Excel 365 shows as '@'); an array gives its
//             first element.
//   Dynamic — a dynamic-array formula (typed here, or cm in the file) and a
//             Ctrl+Shift+Enter array formula: the function is applied to
//             each element and the results form an array ("lifting").
enum class ArrayMode : quint8 { Legacy, Dynamic };

struct EvalContext {
    const EvalHost *host = nullptr;
    qint32 sheet = 0;
    CellAddr here; // the cell being evaluated (ROW()/COLUMN() with no args)
    // LET bindings in scope (upper-case name → value or range), consulted
    // before the workbook's named ranges.
    QHash<QString, Arg> locals;
    // Defined names being expanded around this evaluation (D-069): a name
    // whose body refers back to itself stops at a fixed depth as #NAME?
    // instead of recursing until the stack runs out.
    int nameDepth = 0;
    ArrayMode mode = ArrayMode::Legacy;
};

struct FunctionSpec {
    int minArgs = 0;
    int maxArgs = 0;
    bool isVolatile = false;
    // ROW/COLUMN and friends need the reference itself, not the cell value:
    // single-cell refs stay one-cell RangeViews when this is set.
    bool preserveRefArgs = false;
    // M69 (D-078): the parameters Excel evaluates as arrays even in a
    // formula without the dynamic-array mark (bit i = parameter i, bit 63
    // every later one): SUMPRODUCT(LEN(A1:A3)), LOOKUP(2,1/(A1:A9<>""),…)
    // and INDEX(…,0) work without Ctrl+Shift+Enter in every Excel.
    quint64 arrayParams = 0;
    [[nodiscard]] bool takesArrayAt(int index) const { return ((arrayParams >> qMin(index, 63)) & 1) != 0; }
    std::function<Arg(EvalContext &, const QVector<Arg> &)> fn;

    FunctionSpec() = default;
    FunctionSpec(int min, int max, bool volatileFn, std::function<Arg(EvalContext &, const QVector<Arg> &)> f,
                 bool preserveRefs = false)
        : minArgs(min)
        , maxArgs(max)
        , isVolatile(volatileFn)
        , preserveRefArgs(preserveRefs)
        , fn(std::move(f))
    {
    }
};

[[nodiscard]] const QHash<QString, FunctionSpec> &functionRegistry();

// Argument helpers shared by all categories.
// The one value an argument holds where a function wants one (M69, D-078):
// a value is itself, a one-cell range or array its cell. A range or array
// of more cells, inside a function call, is recorded for lifting or
// intersection and answers its first cell meanwhile (eval_lift.h);
// anywhere else it is intersected (Legacy) or #VALUE! (Dynamic).
[[nodiscard]] Value scalarOrValue(const Arg &arg);
[[nodiscard]] std::optional<double> scalarNumber(const Arg &arg);
// Ordered full-grid read (grid alignment required, e.g. SUMPRODUCT, MATCH).
// Returns the first error cell's value, or an empty Value.
[[nodiscard]] Value flatRangeValues(const EvalContext &ctx, const RangeView &view,
                                    QVector<Value> *out);
// Sparse unordered read for aggregations (SUM/COUNT/*IF over big ranges).
[[nodiscard]] Value sparseRangeValues(const EvalContext &ctx, const RangeView &view,
                                      QVector<Value> *out);

// Criteria matcher for *IF/*IFS functions (plan section 7.2): supports
// ">5", "<>x", ">=", "<=", "=", plain equality and * ? wildcards.
[[nodiscard]] bool criteriaMatches(const Value &cell, const QString &criteria);

// Excel serial dates: serial 0 = 1899-12-30 (plan section 7.2).
[[nodiscard]] double serialFromDate(const QDate &date);
[[nodiscard]] QDate dateFromSerial(double serial);

// Registration entry points, one per category file.
void registerMathFunctions(QHash<QString, FunctionSpec> &registry);
void registerMathExtraFunctions(QHash<QString, FunctionSpec> &registry);
void registerStatisticalFunctions(QHash<QString, FunctionSpec> &registry);
void registerRegressionFunctions(QHash<QString, FunctionSpec> &registry);
void registerConditionalAggFunctions(QHash<QString, FunctionSpec> &registry);
void registerTextFunctions(QHash<QString, FunctionSpec> &registry);
void registerTextJoinFunctions(QHash<QString, FunctionSpec> &registry);
void registerLogicalFunctions(QHash<QString, FunctionSpec> &registry);
void registerLookupFunctions(QHash<QString, FunctionSpec> &registry);
void registerDateFunctions(QHash<QString, FunctionSpec> &registry);
void registerDateExtraFunctions(QHash<QString, FunctionSpec> &registry);
void registerInfoFunctions(QHash<QString, FunctionSpec> &registry);
void registerSubtotalFunctions(QHash<QString, FunctionSpec> &registry);
void registerFinancialFunctions(QHash<QString, FunctionSpec> &registry);
void registerFinancialExtraFunctions(QHash<QString, FunctionSpec> &registry);
// M61 (D-069): functions Excel workbooks use that the engine lacked. They
// register after the files above — MODE, NETWORKDAYS and WORKDAY are
// replaced by corrected versions, and the .INC aliases copy older specs.
void registerFinancialMoreFunctions(QHash<QString, FunctionSpec> &registry);
void registerStatisticalMoreFunctions(QHash<QString, FunctionSpec> &registry);
void registerMathMoreFunctions(QHash<QString, FunctionSpec> &registry);
void registerLookupMoreFunctions(QHash<QString, FunctionSpec> &registry);
void registerTextMoreFunctions(QHash<QString, FunctionSpec> &registry);
void registerDateMoreFunctions(QHash<QString, FunctionSpec> &registry);
// M66 (D-075): the dynamic-array functions — FILTER, SORT, SORTBY, UNIQUE,
// SEQUENCE, RANDARRAY — and ANCHORARRAY behind the A1# spill reference.
void registerDynamicArrayFunctions(QHash<QString, FunctionSpec> &registry);
// M69 (D-078): SINGLE (the '@' operator), INDEX as a reference, array
// functions (TRANSPOSE, MMULT, the shaping family CHOOSECOLS … WRAPCOLS,
// MDETERM, MINVERSE, MUNIT), and the commonly used functions the engine
// lacked: statistics (AGGREGATE, FREQUENCY, PERCENTRANK, KURT, SKEW, the
// binomial, Poisson and exponential distributions, …), engineering (base
// conversions, BIT*, DELTA, GESTEP), DOLLAR, and the database functions.
void registerArrayMoreFunctions(QHash<QString, FunctionSpec> &registry);
void registerArrayShapeFunctions(QHash<QString, FunctionSpec> &registry);
void registerStatisticalM69Functions(QHash<QString, FunctionSpec> &registry);
void registerEngineeringFunctions(QHash<QString, FunctionSpec> &registry);
void registerDatabaseFunctions(QHash<QString, FunctionSpec> &registry);

} // namespace QindaOffice::Calc
