// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Rewriting structured references across a workbook (M69, D-078): what a
// column rename, a table rename, a deleted column or a deleted table does
// to every formula that names them — cells, defined names, and the tables'
// own calculated column formulas.
//
// AGENT-NOTE: one walk serves every edit; the edit is the `fix` callback.
// A reference is handed over with the name of the table it means — its own
// qualifier, or the table holding the formula's cell for an unqualified
// [@Sales] — because only the walk knows which cell a formula sits in.
// Callers decide the dependency refresh: structural edits reindex
// everything anyway; table edits call Workbook::refreshStructuredRefs.

#include "parser.h"

#include <functional>

namespace QindaOffice::Calc {

class Workbook;
struct TableSpec;

// Returns the node to put in place of a structured reference, or null to
// keep it. `tableName` is the table the reference means (empty: none).
using StructRefFix = std::function<NodePtr(const StructRef &ref, const QString &tableName)>;

// `node` with every structured reference passed through `fix`; the same
// pointer when nothing changed.
[[nodiscard]] NodePtr fixStructRefs(const NodePtr &node, const QString &containingTable, const StructRefFix &fix);

// Runs `fix` over every formula of the workbook. Changed formulas get
// their text printed again (nodeText). Returns true when anything changed.
bool rewriteStructRefs(Workbook &book, const StructRefFix &fix);

// True when the formula names any structured reference.
[[nodiscard]] bool containsStructRef(const NodePtr &node);

// The table covering a cell on one sheet (nullptr when none) and its name
// — the containing table an unqualified reference there means.
[[nodiscard]] QString containingTableName(const Workbook &book, qint32 sheet, const CellAddr &addr);

} // namespace QindaOffice::Calc
