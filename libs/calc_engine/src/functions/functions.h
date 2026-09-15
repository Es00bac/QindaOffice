// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Function registry (plan section 7.2): QHash<QString, FunctionSpec> keyed
// by upper-case name, one registration function per category file. Every
// function declares argument bounds and volatility; range arguments arrive
// as RangeView and a range in scalar position is #VALUE! (no implicit
// intersection).

#include "../value.h"

#include <QDate>

#include <functional>

namespace QindaOffice::Calc {

struct Node;
class Workbook;

// A lazy range argument: resolved through the eval host on demand.
struct RangeView {
    qint32 sheet = 0;
    CellRange range;
    [[nodiscard]] bool isValid() const { return range.isValid(); }
};

using Arg = std::variant<Value, RangeView>;

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
};

struct EvalContext {
    const EvalHost *host = nullptr;
    qint32 sheet = 0;
    CellAddr here; // the cell being evaluated (ROW()/COLUMN() with no args)
};

struct FunctionSpec {
    int minArgs = 0;
    int maxArgs = 0;
    bool isVolatile = false;
    // ROW/COLUMN and friends need the reference itself, not the cell value:
    // single-cell refs stay one-cell RangeViews when this is set.
    bool preserveRefArgs = false;
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
[[nodiscard]] Value scalarOrValue(const Arg &arg);          // range in scalar slot -> #VALUE!
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
void registerStatisticalFunctions(QHash<QString, FunctionSpec> &registry);
void registerTextFunctions(QHash<QString, FunctionSpec> &registry);
void registerLogicalFunctions(QHash<QString, FunctionSpec> &registry);
void registerLookupFunctions(QHash<QString, FunctionSpec> &registry);
void registerDateFunctions(QHash<QString, FunctionSpec> &registry);
void registerInfoFunctions(QHash<QString, FunctionSpec> &registry);
void registerFinancialFunctions(QHash<QString, FunctionSpec> &registry);

} // namespace QindaOffice::Calc
