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

// The formatting a matching rule contributes. Fields left unset leave the
// cell's own style alone, so two rules can layer (later rules win).
struct CondFormat {
    std::optional<Rgb> background;
    std::optional<Rgb> foreground;
    std::optional<bool> bold;
    std::optional<bool> italic;
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
    };

    CellRange range;
    Op op = Op::GreaterThan;
    double first = 0;   // GreaterThan/LessThan/EqualTo/NotEqualTo/Between low
    double second = 0;  // Between high
    QString text;       // ContainsText
    CondFormat format;
    bool operator==(const CondFormatRule &) const = default;
};

// True when `value` satisfies `rule`. Text comparisons are case-insensitive
// (what a user typing a rule expects); numeric comparisons coerce through
// toNumber, so the text "12" compares as a number like everywhere else.
[[nodiscard]] bool condRuleMatches(const CondFormatRule &rule, const Value &value);

// --- Data validation ------------------------------------------------------

struct ValidationRule {
    enum class Kind { WholeNumber, Decimal, List, TextLength, NotEmpty };

    CellRange range;
    Kind kind = Kind::WholeNumber;
    double min = 0;
    double max = 0;
    QStringList list;   // Kind::List
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

} // namespace QindaOffice::Calc
