// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel structured references (M69, D-078): Table1[Sales], [@Sales],
// Table1[[#This Row],[Sales]], Table1[#All], [#Headers], [#Data],
// [#Totals], Table1[[Jan]:[Mar]] and their combinations — a reference to
// part of an Excel table by its name and its column names rather than by
// address, so it follows the table when it grows, moves or loses a column.
//
// AGENT-NOTE: two spellings exist and both are Excel's. The formula bar
// (Display) writes this row as '@' and leaves out the table name when the
// formula sits inside the table it names: [@Sales], SUBTOTAL(109,[Sales]).
// The file (<f>, calculatedColumnFormula, totalsRowFormula) always names
// the table and spells this row out: Table1[[#This Row],[Sales]]. The
// engine stores the Display spelling (Cell::formula, nodeText); calc_io
// converts at the package boundary (xlsx_formula.h), which is the only
// place that knows which table holds the cell being written.
//
// AGENT-CONTRACT: the TableSpec list of each sheet (sheet_rules.h) is the
// one source of truth a reference resolves against: its range, header and
// totals rows, and its column names (TableSpec::columnNames, kept in step
// with the header cells by table_edit.h). Nothing here caches a table.

#include "value.h"

#include <QString>

#include <optional>

namespace QindaOffice::Calc {

struct TableSpec;
class EvalHost;

// The item specifiers, as bits. None set means the data rows (Excel's
// default); the ones written are kept, so Table1[#Data] prints back as
// written. Valid sets are one item, #Headers+#Data and #Data+#Totals.
namespace TableItem {
constexpr quint8 Data = 1;
constexpr quint8 Headers = 2;
constexpr quint8 Totals = 4;
constexpr quint8 All = 8;
constexpr quint8 ThisRow = 16;
} // namespace TableItem

struct StructRef {
    QString table;        // as written; empty: the table holding the formula
    quint8 items = 0;     // TableItem bits
    QString firstColumn;  // empty: every column
    QString lastColumn;   // the end of a column range [A]:[B]; empty: one column
    bool emptyBrackets = false; // spelled Table1[] (the data, every column)
    bool operator==(const StructRef &) const = default;
};

// Parses one structured-reference token as the lexer hands it over (the
// optional table name and its balanced brackets). nullopt when Excel would
// refuse it: an unknown #keyword, a keyword combination it does not allow,
// a malformed column list.
[[nodiscard]] std::optional<StructRef> parseStructRef(const QString &text);

enum class StructRefSpelling {
    Display, // the formula bar's: '@', the table name only where it is written
    File,    // Excel's <f>: always qualified, [#This Row] spelled out
};

// The text of a reference. File spelling qualifies an unqualified
// reference with `containingTable` (the table holding the formula's cell);
// with none to name it prints #REF!, which is what such a reference is.
[[nodiscard]] QString structRefText(const StructRef &ref, StructRefSpelling spelling,
                                    const QString &containingTable = {});

// Column names are matched without regard to case, as in Excel. -1 when
// the table has no such column. Missing entries of columnNames fall back
// to the header cell's text (a table built in code, D-076's chart tables).
[[nodiscard]] qint32 tableColumnIndex(const TableSpec &table, const QString &name, const EvalHost &host,
                                      qint32 sheet);

// The cells a reference reaches for a formula at (sheet, here), or the
// error Excel gives: #REF! for a table, column or row part that does not
// exist, #VALUE! for [#This Row] used outside the table's data rows.
struct StructTarget {
    qint32 sheet = -1;
    CellRange range;
    Err error = Err::Ref;
    [[nodiscard]] bool ok() const { return sheet >= 0 && range.isValid(); }
};
[[nodiscard]] StructTarget resolveStructRef(const StructRef &ref, const EvalHost &host, qint32 sheet,
                                            const CellAddr &here);

// The data rows of a table (no header, no totals row); invalid when it has
// none.
[[nodiscard]] CellRange tableDataRows(const TableSpec &table);

} // namespace QindaOffice::Calc
