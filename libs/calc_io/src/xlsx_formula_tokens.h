// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to xlsx_formula.cpp (M69, D-078): the token-level rewrites
// between the formula bar's spelling and Excel's file spelling for what
// M69 added — structured references and the '@' operator. Like the rest of
// xlsx_formula they splice tokens and copy everything else verbatim, so a
// formula keeps its author's spelling around them.

#include <QString>

namespace QindaOffice::Calc::IO {

// Table1[[#This Row],[Sales]] → Table1[@Sales]; the table's name dropped
// where it is `containingTable`.
[[nodiscard]] QString structRefsForDisplay(const QString &text, const QString &containingTable);

// [@Sales] → Table1[[#This Row],[Sales]] (qualified with
// `containingTable`; #REF! where there is none to name).
[[nodiscard]] QString structRefsForFile(const QString &text, const QString &containingTable);

// SINGLE(A1:A3) → @A1:A3, SINGLE(A1+B1) → @(A1+B1).
[[nodiscard]] QString singleAsAt(const QString &text);

// @A1:A3 → SINGLE(A1:A3): the operand is the range operand after '@' (a
// reference, a name, a call, a bracketed expression, joined by ':').
[[nodiscard]] QString atAsSingle(const QString &text);

} // namespace QindaOffice::Calc::IO
