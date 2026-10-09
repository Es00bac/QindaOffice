// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Formula text as Excel stores it versus the engine's spelling (M61, D-069).
//
// Excel writes every function added after Excel 2007 with a prefix —
// _xlfn.IFS(…), _xlfn._xlws.SORT(…) — and LET/LAMBDA parameters as
// _xlpm.name. A reader that keeps the prefix evaluates #NAME?; a writer that
// drops it makes Excel show #NAME? until the user retypes the formula.
//
// AGENT-CONTRACT: the read side strips a prefix ONLY where the table below
// says Excel uses exactly that prefix, and the write side adds it back from
// the same table — so a prefixed formula round-trips byte for byte, and an
// unknown future function (not in the table) keeps its prefix in the
// engine's text and is written back verbatim. Both sides work token by
// token over the engine's lexer: string literals and quoted sheet names are
// never touched, and everything else keeps its original spelling.

#include <QString>

namespace QindaOffice::Calc::IO {

// The prefix Excel stores in front of a function name ("_xlfn." or
// "_xlfn._xlws."), or empty when the function needs none. `upperName` is
// the bare, upper-case function name.
[[nodiscard]] QString futureFunctionPrefix(const QString &upperName);

// The engine spelling of a formula read from a file (no leading '='): the
// documented prefixes dropped, _xlpm. dropped from LET/LAMBDA parameters.
// M69 (D-078): structured references in the formula bar's spelling —
// Table1[[#This Row],[Sales]] is Table1[@Sales], and [@Sales] when the
// formula sits in `containingTable` (the table holding its cell; empty:
// none) — and _xlfn.SINGLE(x) as Excel 365 shows it, @x.
[[nodiscard]] QString formulaFromFile(const QString &text, const QString &containingTable = {});

// The file spelling of an engine formula: known function names upper-cased
// and prefixed where Excel requires it, LET/LAMBDA parameters given _xlpm.,
// and ';' — which the engine accepts as an argument separator on input —
// written as ',' (inside an array constant it separates rows and stays).
// M69 (D-078): structured references as Excel stores them — qualified with
// `containingTable` where the formula left the table out, [#This Row]
// spelled out (an unqualified reference outside any table is #REF!) — and
// '@' as _xlfn.SINGLE(…), the only spelling a file has for it.
[[nodiscard]] QString formulaForFile(const QString &text, const QString &containingTable = {});

// A formula's text with its relative references moved by (dr, dc), token by
// token. Shared-formula cells whose master the engine cannot parse are
// rebuilt with this, so they keep a formula of their own on save.
[[nodiscard]] QString shiftedFormulaText(const QString &text, qint32 dr, qint32 dc);

} // namespace QindaOffice::Calc::IO
