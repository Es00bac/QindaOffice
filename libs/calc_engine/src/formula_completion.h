// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the formula bar offers while a formula is typed (M89, D-097) —
// Excel's Formula AutoComplete: the functions, defined names and tables
// that begin with the name at the cursor, and inside a structured
// reference (`Table1[Am`, `[@Am`) the table's columns and its special items
// (#All, #Data, #Headers, #Totals, #This Row). QindaTK's TextField lists
// them (its host-fed `suggestions`, QindaTK D-314); choosing one replaces
// the text from `from` to `to` with the entry's `insert`.
//
// AGENT-CONTRACT (with apps/qindacalc FormulaCompleter and CalcWindow.qml):
//  * Only a formula ("=…") is completed, never inside a "string" or a
//    'quoted sheet name', nor right after a sheet qualifier ("Sheet1!A").
//  * A function inserts "NAME(" and names its arguments as Excel's help does
//    (functionSignature; data/function_signatures.txt), a name or a table
//    inserts itself, a column inserts "Column]" — "[Unit Price]]" after "@"
//    when the name needs brackets — closing the reference.
//  * Defined names are the workbook's and the formula sheet's own, hidden
//    ones left out; a column list is the table's, or (a bare "[" or "[@")
//    the table holding the cell being edited.

#include "workbook.h"

#include <QList>
#include <QString>

namespace QindaOffice::Calc {

struct Completion {
    QString kind;   // function | name | table | column | item
    QString label;  // "SUM", "Sales", "Table1", "Amount", "#This Row"
    QString detail; // "SUM(number1, [number2], …)", "=Sheet1!$A$2:$A$9", "Table · Sheet1!A1:D9"
    QString insert; // what replaces [from, to)
};

struct CompletionContext {
    int from = -1; // the part being typed: [from, to) of the text
    int to = -1;
    QList<Completion> entries;
};

[[nodiscard]] CompletionContext formulaCompletions(const Workbook &book, qint32 sheet, const CellAddr &at,
                                                   const QString &text, int cursor, int limit = 40);

// "SUM(number1, [number2], …)"; from the arity for a function without a
// line in the signature data; empty for a name the engine does not know.
[[nodiscard]] QString functionSignature(const QString &name);

} // namespace QindaOffice::Calc
