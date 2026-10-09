// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-sheet rules that are NOT cell content: conditional formatting, data
// validation, the autofilter and cell comments.
//
// They live in calc_engine rather than in the app for the same reason the
// structural edits do — every one of them has to be evaluated identically
// by the grid, the printer and the xlsx bridge, and every one of them has
// to survive undo and the crash-recovery payload. Plain data with free
// functions; no Qt Widgets, so all of it is testable offscreen.
//
// AGENT-CONTRACT: rules address cells by RANGE, so structure.cpp moves them
// with the rows and columns they cover, exactly as it moves merges.

#include "cell.h"
#include "value.h"

#include <QStringList>

#include <optional>

namespace QindaOffice::Calc {

// --- Conditional formatting -----------------------------------------------

// A data bar's resolved display: the bar colour and the fraction of the cell
// width it covers (0..1, computed over the range's numeric values).
struct DataBarFill {
    Rgb color{0, 0, 0};
    double fraction = 0;
    bool operator==(const DataBarFill &) const = default;
};

// The formatting a matching rule contributes. Fields left unset leave the
// cell's own style alone, so two rules can layer: evaluation walks the rule
// list in order (highest precedence first, Excel's model) and the FIRST
// matching rule to set a field wins it — a stopIfTrue match ends the walk.
struct CondFormat {
    std::optional<Rgb> background;
    std::optional<Rgb> foreground;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<DataBarFill> dataBar;
    bool operator==(const CondFormat &) const = default;
};

struct CondFormatRule {
    enum class Op {
        GreaterThan,
        LessThan,
        Between,
        EqualTo,
        NotEqualTo,
        ContainsText,
        IsEmpty,
        NotEmpty,
        GreaterThanOrEqual,
        LessThanOrEqual,
        Expression,  // `formula` (relative to range top-left) evaluates truthy
        DataBar,     // bar over the value's share of the range min..max
    };

    CellRange range;
    Op op = Op::GreaterThan;
    double first = 0;   // (Greater|Less)Than(OrEqual)/EqualTo/NotEqualTo/Between low
    double second = 0;  // Between high
    QString text;       // ContainsText
    QString formula;    // Expression: as read/typed, relative to range top-left
    NodePtr ast;        // parsed `formula`, shared with the undo snapshots
    Rgb barColor{99, 142, 198}; // DataBar (default: Excel's stock blue)
    bool stopIfTrue = false;
    CondFormat format;
    bool operator==(const CondFormatRule &) const = default;
};

// True when `value` satisfies `rule` (the classic ops — Expression/DataBar
// need the workbook, see sheet_rules_eval). Text comparisons are
// case-insensitive (what a user typing a rule expects); numeric comparisons
// coerce through toNumber, so the text "12" compares as a number like
// everywhere else.
[[nodiscard]] bool condRuleMatches(const CondFormatRule &rule, const Value &value);

class EvalHost;

// Host-aware matching and merging (sheet_rules_eval.cpp): Expression rules
// evaluate their formula shifted to `addr` (the stored text is relative to
// the range's top-left, exactly like a shared formula's master), DataBar
// rules resolve their fraction over the range's numeric values, and the
// merge walks in list order with first-wins-per-field and stopIfTrue.
[[nodiscard]] bool condRuleMatchesHost(const CondFormatRule &rule, const CellAddr &addr,
                                       const EvalHost &host, qint32 sheet);
[[nodiscard]] CondFormat condFormatForHost(const QList<CondFormatRule> &rules,
                                           qint32 sheet, const CellAddr &addr,
                                           const EvalHost &host,
                                           QHash<quint64, QPair<double, double>> &extentCache);

// Prepares an Expression rule: parses `formula` into `ast` (returns false
// and leaves ast null when the formula does not parse; callers then drop or
// text-ify the rule rather than keeping one that can never match).
[[nodiscard]] bool prepareCondRule(CondFormatRule &rule);

// --- Data validation ------------------------------------------------------

struct ValidationRule {
    enum class Kind { WholeNumber, Decimal, List, TextLength, NotEmpty };

    CellRange range;
    Kind kind = Kind::WholeNumber;
    double min = 0;
    double max = 0;
    QStringList list;   // Kind::List
    // Kind::List read from a file whose list names a range or a name
    // ("Lists!$A$1:$A$3", no '='): the live source the file keeps. Entries
    // are still checked against `list` (the source's values at load); a
    // save writes the source back. Empty for a typed list (M61, D-069).
    QString listSource;
    QString message;    // shown when input is refused; a default is used when empty
    bool operator==(const ValidationRule &) const = default;
};

// True when `input` (the text the user typed, before parsing) is acceptable.
// An empty input always passes except for NotEmpty: clearing a cell must
// never be blocked, or a mistyped value could not be taken back.
[[nodiscard]] bool validationAccepts(const ValidationRule &rule, const QString &input);
// Human-readable reason, for the refusal message.
[[nodiscard]] QString validationDescription(const ValidationRule &rule);

// --- Autofilter -----------------------------------------------------------

// One filter over a block whose FIRST row is the header. `allowed` maps an
// absolute column index to the set of display strings kept; a column with no
// entry is unfiltered. Empty `allowed` therefore means "show everything".
struct AutoFilter {
    CellRange range;
    QHash<qint32, QStringList> allowed;
    bool active = false;
    bool operator==(const AutoFilter &) const = default;
};

// --- Cell comments --------------------------------------------------------

struct CellComment {
    QString author;
    QString text;
    bool operator==(const CellComment &) const = default;
};

// --- Excel tables (ListObjects) -------------------------------------------

// A structured table over a range: banded rows and a styled header supplied
// by its tableStyle name, column captions, and (in files) its own filter
// and totals row. Table-scoped filter criteria are NOT imported (the
// sheet's AutoFilter stays singular — D-036); the filter buttons are.
// M69 (D-078): structured references resolve against this record
// (structured_ref.h) and table_edit.h keeps it in step with the cells —
// the totals row holds ordinary formulas (SUBTOTAL(109,[Sales])), and
// calc_io derives the table part's totalsRowFunction from them.
struct TableSpec {
    QString name;        // displayName (unique workbook-wide in a valid file)
    CellRange range;     // header row first, body rows after
    bool headerRow = true;
    bool totalsRow = false;
    QString styleName;   // e.g. "TableStyleMedium2"; empty = Medium2
    bool rowStripes = true;
    bool colStripes = false;
    // AGENT-CONTRACT: index i names the table's column i. table_edit.h
    // keeps these equal to the header cells (renaming references when a
    // header is retyped); an entry missing or empty falls back to the
    // header cell's text (show_model's chart tables leave it empty).
    QStringList columnNames;
    // M66 (D-075): the table part it was read from; a query table (Power
    // Query output) hangs from that part and is carried with the table.
    // Empty for a table made here.
    QString sourcePart;
    // M69 (D-078): each column's calculated column formula in the engine's
    // spelling, relative to the first data row ("[@Price]*[@Qty]"); empty
    // when the column is not calculated. Index-aligned like columnNames;
    // missing entries are empty. New rows of the table get it.
    QStringList calculatedColumns;
    bool autoFilter = true; // the header shows Excel's filter buttons (<autoFilter> in the part)
    bool operator==(const TableSpec &) const = default;
};

} // namespace QindaOffice::Calc
