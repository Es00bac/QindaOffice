// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to calc_engine's conditional formatting and data validation
// (M78, D-087): the statistics a rule's values are measured against
// (CondFormatCache), the helpers that turn them into thresholds, colours,
// bars and icons (cond_format_stats.cpp), and the value-level tests every
// path shares — the classic one without a workbook (sheet_rules.cpp), the
// host-aware one (sheet_rules_eval.cpp) and validation
// (validation_eval.cpp) — so they can never drift apart.
//
// AGENT-NOTE: Excel's semantics, worked from the formulas Excel itself
// writes for each rule kind (it stores them in the file, e.g. containsText
// is NOT(ISERROR(SEARCH("x",A1)))): a blank compares as 0, "" or FALSE, text
// compares without case and sorts after every number, an error matches
// nothing but "contains errors" / "does not contain text".

#include "sheet_rules.h"

#include <QHash>
#include <QVector>

#include <memory>
#include <optional>

namespace QindaOffice::Calc {

class EvalHost;

// The statistics of the values a rule covers, over its whole sqref, built
// for what the rule's kind needs and cached until the next recalculation.
struct CfRangeStats {
    double min = 0;
    double max = 0;
    bool hasNumbers = false;
    QVector<double> numbers; // the numeric values (dates included), sorted ascending
    double mean = 0;
    double stdDev = 0;       // sample standard deviation (STDEV), as Excel's averages rules
    QHash<QString, int> counts;                // duplicateKey → occurrences (Duplicate / Unique)
    QVector<std::optional<double>> thresholds; // the rule's cfvo values, resolved
    double cut = 0;          // Top10: the value a cell must reach (or not exceed)
    bool cutValid = false;
};

// The statistics for `rule` on `sheet`, from the cache or worked out now.
[[nodiscard]] std::shared_ptr<const CfRangeStats> cfStatsFor(const CondFormatRule &rule, qint32 sheet,
                                                             const EvalHost &host, CondFormatCache &cache);

// How a value counts for duplicateValues / uniqueValues: numbers by value,
// text without case, booleans apart; empty for blanks and errors (which
// count for neither).
[[nodiscard]] QString duplicateKey(const Value &value);

// A formula of a rule (stored relative to `anchor`) evaluated at `addr`.
[[nodiscard]] Value evalShifted(const NodePtr &ast, const CellAddr &anchor, const CellAddr &addr,
                                const EvalHost &host, qint32 sheet);

// The colour, bar and icon a numeric value gets.
[[nodiscard]] std::optional<Rgb> colorScaleColor(const CondFormatRule &rule, double value, const CfRangeStats &stats);
[[nodiscard]] std::optional<DataBarFill> dataBarFill(const CondFormatRule &rule, double value,
                                                     const CfRangeStats &stats);
[[nodiscard]] std::optional<IconPaint> iconPaintFor(const CondFormatRule &rule, double value,
                                                    const CfRangeStats &stats);

// ---- value-level tests (sheet_rules.cpp) ----
// cellIs: `value` against the operands (Between / NotBetween take both,
// in either order). Excel's comparison: errors never match.
[[nodiscard]] bool cellIsMatches(CondFormatRule::Op op, const Value &value, const Value &first, const Value &second);
// containsText, notContainsText, beginsWith, endsWith, containsBlanks,
// notContainsBlanks, containsErrors, notContainsErrors — the formulas Excel
// writes for them, worked natively.
[[nodiscard]] bool textRuleMatches(CondFormatRule::Op op, const Value &value, const QString &text);
// timePeriod against `today` (a date serial).
[[nodiscard]] bool timePeriodMatches(CondFormatRule::Period period, const Value &value, double today);
// True for the kinds a value alone cannot decide (statistics, formulas,
// colour scales, bars, icons): the classic path skips them.
[[nodiscard]] bool condRuleNeedsHost(const CondFormatRule &rule);
// The cellIs kinds.
[[nodiscard]] bool isCellIsOp(CondFormatRule::Op op);

} // namespace QindaOffice::Calc
