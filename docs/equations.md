<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Office equations (M92, D-100)

`office_math` owns the equation model, Office Math Markup Language (OMML)
interchange, UnicodeMath and LaTeX input, OpenType MATH layout, and an
editable draft with history. It links only Qt Core/Gui and `office_ooxml`;
applications can use the same code for editing, accessibility and output.

## Model and preservation

`Equation` has one or more `Argument` lines. Nodes cover runs, fractions,
scripts and prescripts, radicals, large operators, delimiters, functions,
accents, bars, boxes, equation arrays, matrices, group characters, limits
and phantoms. Properties are retained as XML and accessed through typed
helpers. Unrecognized structures, attributes and children stay in the model
and writer. Malformed known structures are kept whole instead of guessed.

A normal copy shares nodes for reading; `clone()` gives a separately owned
mutable tree. `readOmmlFragment()` consumes an `oMath` or `oMathPara` fragment;
`writeOmmlFragment()` returns its self-contained XML. Unknown content is
preserved even when surrounding recognized nodes are edited. Namespace
prefixes and insignificant formatting may differ on a write.

## Layout and fonts

`EquationLayout(equation, options)` returns width, ascent, descent, a box
hierarchy, slot geometry and caret hit testing. Paint at a baseline origin
with `paint()`. `renderEquation()` returns a transparent image, its device
pixel ratio, the baseline origin and the same logical metrics. Hosts should
use these metrics rather than stretching the image to a guessed line size.

The requested math font defaults to Cambria Math. If it is unavailable or
has no MATH table, the library tries installed math fonts, including DejaVu
Math TeX Gyre and Noto Sans Math. The caller can inspect `isValid()` and
`fontFamily()`; a missing math font is a visible host error, not a blank
successful equation. Font data must be installed through Portage.

The font's MATH table supplies fraction and script spacing, radical degree
placement, italic corrections, accent attachments, and glyph variants and
assemblies for growing delimiters and operators. Unicode mathematical
alphabets represent script, fraktur, double-struck, sans serif and monospace
runs. The surrounding run provides size and color. Font/layout work requires
a `QGuiApplication`; no Qt Quick or desktop archives are needed.

## Input and editing API

`fromLinear()` / `toLinear()` implement a UnicodeMath subset, including
fractions, roots, scripts, large operators, functions, matrices, equation
arrays, delimiters and accents. `applyAutoCorrect()` expands supported Word
math keywords. `fromLatex()` / `toLatex()` accept common mathematical LaTeX
commands, groups, environments, alphabets and operators. This is a formula
parser; it does not execute TeX or expand document macros. Unknown commands
remain text. OMML is the preservation format: the linear formats cannot
express every Office property or extension.

`EquationEditor` clones its input and exposes an owned draft. Positions use
a line index, a path of node/argument indices, and a Unicode code-point atom
offset. A structure is one atom in its parent and has its own editable slots.
Selections are within one slot. Cross-slot or invalid selections fail with
no content/history change. `Structure` supplies the equation gallery
constructs; inserting over a selection places that content in the construct's
main argument. Matrix and array slots have horizontal/vertical navigation;
hidden radical degrees and operator limits are skipped.

Typing, insertion, replacement and deletion each make one undoable change.
Undo/redo restore content and selection; a new edit clears redo. Arrow and
slot navigation do not create history. Runs containing preserved non-text
XML can be replaced whole; partial edits are refused to prevent losing their
unknown children. All editor calls/signals use its QObject thread, and the
returned draft is read-only to callers. `reset()` loads a new draft and
clears history.

## Verification and remaining integration

The four headless tests cover Word-shaped OMML and its schema, the font
metrics/layout and caret mapping, 72 UnicodeMath/LaTeX round trips, all 23
structure templates and editor ownership/history. Pixel sheets include the
quadratic formula, Euler identity, integral, binomial, matrix, cases,
Maxwell equations, Taylor series and the supported structure families at
1× and 2×. Exact qualification is recorded in
[verification.md](verification.md#m92-equation-library-gates-d-100).

This milestone is the reusable library. QindaWrite's inline object/editor
and QindaShow's equation objects are a following integration phase. Their
existing preservation behavior remains until those adapters land. It is
not a full TeX engine, and conversion to linear text can approximate OMML
features with no equivalent spelling. Visual comparison uses synthetic
Word-shaped samples; real Word/PowerPoint application checks remain manual.
