<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaCalc: Solver, sparklines and everyday Excel tools

M90 / D-098 adds Data ▸ Solver, Insert ▸ Sparklines, contextual Sparkline
and Table Design commands, PivotTable calculated fields and date filters,
visibility-sensitive totals, kept-rule edits and printed legacy assets.

## Solver

Choose an objective (maximize, minimize or a target value), variable
cells, and <=, >=, =, integer, binary or all-different constraints. Simplex
LP checks linearity; GRG Nonlinear is a local bounded augmented-Lagrangian
search; Evolutionary uses differential evolution. Options include time,
iterations, precision, convergence, integer tolerance, non-negativity and
method settings. Work runs on a private worker workbook with progress and
Cancel. Keep or Restore decides the variable values; an Answer Report can
be inserted. Keeping the solution, model and report is one undo step.
Excel's hidden sheet-scoped `solver_*` names store the parameters.

The methods are independent implementations. Sampled linearity checks can
miss specially constructed nonlinear expressions; a local nonlinear result
is not a certificate of global optimality. Models are bounded by the
limits in `solver.h` and invalid references/protected cells are refused.

## Sparklines and table design

Line, Column and Win/Loss sparklines store ranges, point highlights,
markers, theme or explicit colours, individual/group/custom vertical
limits, date axes, RTL direction, gaps/zero/connect and hidden-data policy.
The Sparkline tab supports edit, group, ungroup and clear. Screen and paper
share the renderer. `x14:sparklineGroups` read and write through calc_io;
source and location ranges move with structural edits. Undo and recovery
carry complete groups.

Table Design renames tables and all structured references, defined names,
calculated columns and pivot sources that use them. Resize validates
headers, overlap, merges and arrays, preserves retained columns and fills
calculated columns into new rows. Turn off Total Row before changing the
row extent of a table with a totals row.

## Totals and PivotTables

SUBTOTAL always omits filtered rows and nested subtotal/aggregate calls;
101–111 also omit hidden rows. AGGREGATE options 0–7 control hidden rows,
errors and nesting. Materialized arrays have no row visibility metadata.
Hide, Show, outline and filter edits recalculate affected workbook results.

PivotTable Analyze ▸ Calculated Field adds or modifies formulas referring
to source fields (quote names containing spaces). Formulas evaluate on
field sums, so `Sales/Qty` in the grand total divides total sales by total
quantity. A change updates all pivots sharing the cache in one undo step.
Cell references, unknown fields, cycles and unsupported formula forms are
refused. Cache formulas survive refresh, Excel save and recovery. Calculated
items, OLAP and Data Model pivots remain outside this implementation.

The Filter dialog adds Date: between, before, after, this/last/next month,
quarter or year, and year to date. Imported date filters also include
individual months/quarters and today/yesterday/tomorrow/week rules. Dynamic
periods use the local calendar date when the pivot is recomputed. Existing
value and Top 10 filters remain available.

## Rules and printing

Conditional-formatting/data-validation ranges and modelled formulas move
with row and column changes; retained XML formulas now move as well.
Unparseable kept formulas remain intact and are flagged on save.

Header/footer `&G` renders embedded pictures referenced by legacyDrawingHF;
images stay in the retained package for lossless save/recovery. Only
embedded relationships are read. Page Setup's notes-at-end mode wraps and
paginates current note text. As-displayed mode renders visible legacy VML
notes; hover-only notes created here have no persistent displayed state.
Printed notes use VML anchors and box sizes; legacy images honour crop
fractions. Notes with no anchor fall back to the cell's right edge. Modern
threaded comment layouts remain open. First/even VML image variants require manual
Excel corpus comparison.

## Verification

Synthetic test files contain no owner records. Run the calc_engine,
calc_io and qindacalc ctests headlessly with
`QQO_OOXML_SCHEMA_DIR=/home/cabewse/scratch/qqo/ecma-xsd` to activate the
ECMA schema gate. See [verification.md](verification.md) for exact results,
reviewed grabs and manual checks. No live desktop application is launched
by these tests.
