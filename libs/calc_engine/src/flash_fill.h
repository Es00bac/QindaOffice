// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ Flash Fill (Ctrl+E) (M78, D-087): fill a column from a few typed
// examples by finding one program that turns every example row's source
// cells into its example. The programs are concatenations of pieces —
// literal text, or a token of a source cell (a word, a run of letters or
// digits, a field between delimiters, a fixed prefix or suffix) in upper,
// lower, proper or its own case — which covers Excel's everyday patterns:
// split ("Smith" from "John Smith"), join ("John Smith" from "John" and
// "Smith", with separators), case ("JOHN"), extract (the digits, the
// domain of an e-mail address, the first three letters) and their mixes
// ("J. Smith", "smith.john@example.com").
//
// What it cannot infer it says, in `failure`: arithmetic, dates rewritten
// in another order, lookups, anything that depends on the value of
// another row, and patterns the examples leave ambiguous. The ranking and
// the ambiguity rule are flash_fill.cpp's AGENT-NOTE. Qt Core only.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Calc {

struct FlashFillResult {
    bool ok = false;
    // One per row of `sources`: an example row keeps its example, the
    // others get the program's output (empty where a row's sources do not
    // have the pieces the program takes, as Excel leaves them).
    QStringList outputs;
    // How many rows the fill changed (examples not counted).
    int filled = 0;
    // What was inferred, for the status line: "the second word of column
    // A, then “, ”, then the first word".
    QString pattern;
    // Why nothing was filled, when ok is false.
    QString failure;
    // When the examples fit two patterns that give different results: the
    // first row (index into `sources`) where they differ — where one more
    // example settles it. -1 otherwise.
    int ambiguousRow = -1;
};

// `sources[row]` are the texts of the data columns beside the target
// column (Excel reads the current region's columns), `examples[row]` the
// texts the user typed into the target column so far. Every row without an
// example and with at least one non-empty source gets an output. At least
// one example is needed.
[[nodiscard]] FlashFillResult flashFill(const QList<QStringList> &sources, const QHash<int, QString> &examples,
                                        const QStringList &columnNames = {});

} // namespace QindaOffice::Calc
