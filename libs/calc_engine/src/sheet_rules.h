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

#include <QHash>
#include <QStringList>

#include <memory>
#include <optional>

namespace QindaOffice::Calc {

// --- Conditional formatting -----------------------------------------------

// A data bar's resolved display: the bar colour and the fraction of the cell
// width it covers (0..1, computed over the range's numeric values). M78
// (D-087): Excel 2010's bars — negative values drawn from the axis in their
// own colour, a solid or gradient fill, a border, the direction.
struct DataBarFill {
    Rgb color{0, 0, 0};
    double fraction = 0;    // the bar's length, 0..1 of the cell width
    // M78: where the bar starts (0..1 of the cell width: 0 = the left edge;
    // the axis position when the range holds negative values) and whether
    // it runs leftwards from there (a negative value).
    double origin = 0;
    bool negative = false;
    bool gradient = true;
    std::optional<Rgb> border;
    std::optional<Rgb> axisColor; // drawn when the range holds negative values
    bool rightToLeft = false;
    bool operator==(const DataBarFill &) const = default;
};

// M78 (D-087): an icon set's icon for one cell — Excel's set name
// ("3TrafficLights1", "4Rating", "5Quarters", "3Stars", …) and the band
// the value falls in, 0 being the LOWEST band (the set's first cfvo), as
// in the file; `reverse` is already applied.
struct IconPaint {
    QString set;
    int index = 0;
    bool operator==(const IconPaint &) const = default;
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
    // M78 (D-087): the dxf's other font flags, a colour scale's colour (as
    // `background`), an icon, and showValue="0" on a bar or an icon set.
    std::optional<bool> underline;
    std::optional<bool> strikeout;
    std::optional<IconPaint> icon;
    bool hideValue = false;
    bool operator==(const CondFormat &) const = default;
};

// M78 (D-087): where a colour scale, data bar or icon set puts a threshold
// (Excel's cfvo).
struct CfThreshold {
    enum class Type { Min, Max, Number, Percent, Percentile, Formula, AutoMin, AutoMax };
    Type type = Type::Min;
    // Number / Percent / Percentile: the number as text; Formula: the
    // formula in the engine's spelling, without '=' (relative references
    // are relative to the rule's anchor). Empty for Min, Max and Auto*.
    QString value;
    bool gte = true; // icon sets: a value at the threshold is in its band (false: only above it)
    bool operator==(const CfThreshold &) const = default;
};

// M78 (D-087): Excel 2010's data-bar settings (the x14:dataBar extension).
// Defaults are what an Excel 2007 bar means (gradient, 10–90 %).
struct DataBarStyle {
    enum class Axis { Automatic, Middle, None };
    enum class Direction { Context, LeftToRight, RightToLeft };
    int minLength = 10;  // percent of the cell the smallest value's bar takes
    int maxLength = 90;  // … and the largest
    bool gradient = true;
    std::optional<Rgb> border;         // the bar's border colour (border="1")
    Rgb negativeColor{255, 0, 0};
    std::optional<Rgb> negativeBorder; // unset: the positive border's colour
    bool negativeSameAsPositive = false;
    Rgb axisColor{0, 0, 0};
    Axis axis = Axis::Automatic;
    Direction direction = Direction::Context;
    bool x14 = false; // the rule has (and is written with) its Excel 2010 extension
    bool operator==(const DataBarStyle &) const = default;
};

struct CondFormatRule {
    // AGENT-GUARD: append only — the recovery payload stores the number.
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
        // M78 (D-087): every other kind Excel writes.
        NotBetween,        // cellIs notBetween
        NotContainsText,   // `text`
        BeginsWith,        // `text`
        EndsWith,          // `text`
        ContainsErrors,
        NotContainsErrors,
        Top10,             // rank, percent, bottom
        AboveAverage,      // aboveAverage, equalAverage, stdDev
        Duplicate,         // duplicateValues
        Unique,            // uniqueValues
        TimePeriod,        // period
        ColorScale,        // thresholds + colors
        IconSet,           // iconSet + thresholds, reverse, showValue
    };
    // Excel's timePeriod values (A Date Occurring).
    enum class Period { Today, Yesterday, Tomorrow, Last7Days, ThisWeek, LastWeek, NextWeek, ThisMonth, LastMonth, NextMonth };

    // The first range of the rule's sqref: formulas (Expression, cellIs
    // operands, cfvo formulas) are relative to its top-left cell.
    CellRange range;
    // M78 (D-087): the rest of the sqref ("A1:A5 C1:C5" — one rule over
    // both: statistics such as Top 10, averages and colour-scale extremes
    // are taken over every range together, as in Excel).
    QList<CellRange> moreRanges;
    Op op = Op::GreaterThan;
    double first = 0;   // (Greater|Less)Than(OrEqual)/EqualTo/NotEqualTo/(Not)Between low
    double second = 0;  // (Not)Between high
    QString text;       // ContainsText, NotContainsText, BeginsWith, EndsWith
    // Expression: as read/typed, relative to range top-left. M78: also a
    // cellIs rule's first operand when it is not a plain number
    // ("$B$1", "\"Yes\"", "AVERAGE($A:$A)"); `first` is then unused.
    QString formula;
    NodePtr ast;        // parsed `formula`, shared with the undo snapshots
    // M78: a cellIs rule's second operand (Between / NotBetween) when it is
    // not a plain number.
    QString formula2;
    NodePtr ast2;
    Rgb barColor{99, 142, 198}; // DataBar (default: Excel's stock blue)
    bool stopIfTrue = false;
    CondFormat format;

    // ---- M78 (D-087) ----
    int rank = 10;              // Top10: how many items (or percent)
    bool percent = false;       // Top10: rank is a percentage of the values
    bool bottom = false;        // Top10: the lowest, not the highest
    bool aboveAverage = true;   // AboveAverage: above (false: below)
    bool equalAverage = false;  // AboveAverage: the average itself counts
    int stdDev = 0;             // AboveAverage: 1–3 standard deviations away (0: the average)
    Period period = Period::Today;
    // ColorScale (2 or 3), DataBar (2: the bar's ends), IconSet (one per
    // icon, lowest first).
    QList<CfThreshold> thresholds;
    QList<Rgb> colors;          // ColorScale: one per threshold
    QString iconSet = QStringLiteral("3TrafficLights1");
    bool reverse = false;       // IconSet: icons in reverse order
    bool showValue = true;      // DataBar / IconSet: false shows only the bar or icon
    DataBarStyle bar;           // DataBar: Excel 2010's settings
    // Bookkeeping for the file (calc_io, D-087): the rule's priority as
    // read (0: a rule made in QindaCalc), the source package's dxf its
    // format came from (-1: none), and the {GUID} tying an Excel 2007 rule
    // to its Excel 2010 twin. A save keeps them while they still hold.
    int priority = 0;
    int dxfId = -1;
    QString x14Id;
    bool operator==(const CondFormatRule &) const = default;

    // Every range of the sqref.
    [[nodiscard]] QList<CellRange> ranges() const
    {
        QList<CellRange> all{range};
        all += moreRanges;
        return all;
    }
    [[nodiscard]] bool covers(const CellAddr &addr) const
    {
        if (range.contains(addr)) {
            return true;
        }
        for (const CellRange &more : moreRanges) {
            if (more.contains(addr)) {
                return true;
            }
        }
        return false;
    }
};

// True when `value` satisfies `rule` (the classic ops — Expression/DataBar
// need the workbook, see sheet_rules_eval). Text comparisons are
// case-insensitive (what a user typing a rule expects); numeric comparisons
// coerce through toNumber, so the text "12" compares as a number like
// everywhere else.
[[nodiscard]] bool condRuleMatches(const CondFormatRule &rule, const Value &value);

class EvalHost;
struct CfRangeStats;

// M78 (D-087): what a recalculation invalidates — per rule, the statistics
// of its ranges' values (extremes, the sorted numbers, the average and
// standard deviation, how often each value occurs). Keyed by the rule's
// ranges and kind; Workbook clears it on every recalculation.
struct CondFormatCache {
    QHash<quint64, std::shared_ptr<const CfRangeStats>> stats;
    void clear() { stats.clear(); }
};

// Host-aware matching and merging (sheet_rules_eval.cpp): Expression rules
// evaluate their formula shifted to `addr` (the stored text is relative to
// the range's top-left, exactly like a shared formula's master), DataBar
// rules resolve their fraction over the range's numeric values, and the
// merge walks in list order with first-wins-per-field and stopIfTrue.
// M78: every kind of CondFormatRule::Op, over the rule's whole sqref.
[[nodiscard]] bool condRuleMatchesHost(const CondFormatRule &rule, const CellAddr &addr,
                                       const EvalHost &host, qint32 sheet, CondFormatCache &cache);
[[nodiscard]] CondFormat condFormatForHost(const QList<CondFormatRule> &rules,
                                           qint32 sheet, const CellAddr &addr,
                                           const EvalHost &host, CondFormatCache &cache);

// M78 (D-087): how many icons an icon set has ("3Arrows" → 3, "4Rating" →
// 4, "5Quarters" → 5; Excel 2010's "NoIcons" → 0); 3 for a name it does
// not know. IconPaint::index runs from 0 to this − 1.
[[nodiscard]] int iconSetSize(const QString &set);

// Prepares a rule's formulas: parses `formula` (Expression, and a cellIs
// operand that is not a number) into `ast` and `formula2` into `ast2`.
// Returns false (asts null) when a formula the rule needs does not parse;
// callers then keep the rule as the file wrote it (calc_io) or drop it.
[[nodiscard]] bool prepareCondRule(CondFormatRule &rule);

// --- Data validation ------------------------------------------------------

struct ValidationRule {
    // AGENT-GUARD: append only — the recovery payload stores the number.
    enum class Kind {
        WholeNumber,
        Decimal,
        List,
        TextLength,
        NotEmpty, // QindaCalc's own: written as Excel's custom LEN(TRIM(cell))>0
        // M78 (D-087)
        Date,     // operands are date serials (or formulas)
        Time,     // operands are fractions of a day (or formulas)
        Custom,   // `formula1` must be TRUE for the cell's value
        Any,      // "Any value": no check, only the input message
    };
    // M78 (D-087): Excel's operators (an absent one is between).
    enum class Operator { Between, NotBetween, Equal, NotEqual, GreaterThan, LessThan, GreaterThanOrEqual, LessThanOrEqual };
    enum class Style { Stop, Warning, Information };

    CellRange range;
    // M78: the rest of the sqref (one rule over several ranges).
    QList<CellRange> moreRanges;
    Kind kind = Kind::WholeNumber;
    double min = 0;     // the first operand when it is a number
    double max = 0;     // the second operand (Between / NotBetween)
    QStringList list;   // Kind::List
    // Kind::List read from a file whose list names a range or a name
    // ("Lists!$A$1:$A$3", no '='): the live source the file keeps. Entries
    // are still checked against `list` (the source's values at load); a
    // save writes the source back. Empty for a typed list (M61, D-069).
    QString listSource;
    QString message;    // shown when input is refused; a default is used when empty

    // ---- M78 (D-087) ----
    Operator op = Operator::Between;
    // An operand that is not a plain number — a cell ("$C$1"), a name, a
    // formula ("TODAY()") — in the engine's spelling without '=', relative
    // to the range's top-left; Custom: its formula. Empty: `min` / `max`.
    QString formula1;
    QString formula2;
    NodePtr ast1;
    NodePtr ast2;
    Style style = Style::Stop;
    QString errorTitle;
    QString promptTitle;
    QString prompt;
    bool showError = true;   // Excel's showErrorMessage (a rule without it accepts anything)
    bool showInput = true;   // Excel's showInputMessage
    bool allowBlank = true;  // "Ignore blank"
    bool inCellDropdown = true; // List: the cell's dropdown (Excel's inverted showDropDown)
    // Read from (and written back to) the worksheet's Excel 2010 extension
    // (x14:dataValidations: a list on another sheet, Excel 2007's limit).
    bool x14 = false;
    bool operator==(const ValidationRule &) const = default;

    [[nodiscard]] bool covers(const CellAddr &addr) const
    {
        if (range.contains(addr)) {
            return true;
        }
        for (const CellRange &more : moreRanges) {
            if (more.contains(addr)) {
                return true;
            }
        }
        return false;
    }
};

// True when `input` (the text the user typed, before parsing) is acceptable.
// An empty input always passes except for NotEmpty: clearing a cell must
// never be blocked, or a mistyped value could not be taken back.
[[nodiscard]] bool validationAccepts(const ValidationRule &rule, const QString &input);
// Human-readable reason, for the refusal message.
[[nodiscard]] QString validationDescription(const ValidationRule &rule);

// M78 (D-087): the rule's verdict on `value` at `addr` with the workbook in
// hand — operands that are formulas evaluated there, Custom formulas, dates
// and times, every operator. A blank passes while allowBlank (NotEmpty
// excepted). Data ▸ Data Validation ▸ Circle Invalid Data asks this of
// every cell a rule covers; typed input asks it of the parsed value.
[[nodiscard]] bool validationAcceptsValue(const ValidationRule &rule, const CellAddr &addr, const Value &value,
                                          const EvalHost &host, qint32 sheet);
// Parses `formula1` / `formula2` into their ASTs; false when one does not parse.
[[nodiscard]] bool prepareValidationRule(ValidationRule &rule);

// --- Rules QindaCalc keeps as Excel wrote them (M78, D-087) ----------------

// A conditional-format block or a validation the engine does not model —
// a custom icon, a rule kind Excel may add, a formula the parser cannot
// read — kept as the file wrote it, so a save gives it back byte for byte
// instead of dropping it. Its ranges are moved by structural edits like
// every rule (the writer then rewrites only the sqref); the formulas inside
// it are not.
struct KeptRule {
    enum class Kind {
        CondFormat,     // a <conditionalFormatting> element (its kept cfRules)
        CondFormatX14,  // an <x14:conditionalFormatting> element of the worksheet extLst
        Validation,     // a <dataValidation> element
        ValidationX14,  // an <x14:dataValidation> element of the worksheet extLst
    };
    Kind kind = Kind::CondFormat;
    QList<CellRange> ranges; // its sqref, as moved by structural edits
    QString xml;             // the element as read (its sqref inside)
    QList<int> priorities;   // conditional formats: its rules' priorities, in order
    QString reason;          // what QindaCalc lacks to model it (reports, open notes)
    bool operator==(const KeptRule &) const = default;
};

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
