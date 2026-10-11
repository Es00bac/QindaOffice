<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Office equations (M92/M92b, D-100/D-107)

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
successful equation. Font data must be installed through Portage. Raw font caches are thread-local,
as Qt requires. Layout freezes glyph outlines; an immutable layout can then
be painted on a different render thread and under arbitrary transforms.
This also avoids scale-dependent glyph loss in the Show canvas.
Overlapping extender outlines use winding fill so stretched brackets and
other assembled glyphs retain continuous strokes at their seams.

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

## Editing in Write and Show (M92b)

Insert → Equation opens the shared professional editor. Edit Equation or a
double-click opens an existing formula. Type at the caret, use arrows to
move, and Tab/Shift+Tab to visit a structure's slots. The gallery exposes all
23 library templates; insertion wraps the current selection. Draft Undo and
Redo also cover UnicodeMath/LaTeX conversion. Copy/paste between professional
editors retains native OMML; plain-text clipboard consumers receive its
linear spelling. Apply makes one document undo step; Cancel changes nothing.
An untouched draft returns the exact original XML fragment.

The linear input is a conversion surface, not a second preservation format.
Build explicitly replaces the draft and is undoable. Unknown Office properties
without a linear equivalent may be approximated by conversion; structured
editing continues to preserve them. Missing math fonts and invalid source
are visible errors. The editor is a shared `office_quick` component over the
Core/Gui `EquationEditor`; neither `office_math` nor the file-format libraries
depend on Qt Quick.

Write stores one custom inline text object carrying native OMML in the
existing preservation properties. Its specialized Qt text layout gives each
formula the library's real ascent and descent, including deep fractions.
The registered object painter draws vectors through `EquationLayout` on the
page and in print/PDF. Rich headers/footers and their clones receive the same
layout before geometry observers are attached. DOCX import suspends layout
through the story, Word-metric and list-formatting passes, then restores the
caller's layout policy. This avoids repeated whole-table reflow while loading
large tables. This adapter uses Qt GuiPrivate's
text layout, version-locked with the suite's stock Qt 6 build. Existing picture,
paragraph and table layout paths delegate to Qt unchanged.

Show appends an equation shape kind to the persisted enumeration. Its native
fragment and surrounding size survive shape clipboard JSON and flat ODP
extensions. PPTX writes `a14:m` with native `m:oMath` or `m:oMathPara`, inside
Markup Compatibility with a generated picture fallback. Untouched source
shapes are carried; edited math replaces only the native formula and refreshes
the compatibility picture. Screen, thumbnails, player, print and PDF use
one vector renderer. A standalone native equation is editable. Compound
PowerPoint text boxes containing ordinary text and one or more equations
remain preserved as a whole until the text-run model supports inline math;
editing their surrounding text cannot silently discard the equations.

At narrow widths Show uses a separate compact dock workspace: Slides and
Slide share a tabbed inspector above the full-width canvas. Resizing back
restores the wide workspace and its saved panel arrangement.

## Verification

The four headless tests cover Word-shaped OMML and its schema, the font
metrics/layout and caret mapping, 72 UnicodeMath/LaTeX round trips, all 23
structure templates and editor ownership/history. Pixel sheets include the
quadratic formula, Euler identity, integral, binomial, matrix, cases,
Maxwell equations, Taylor series and the supported structure families at
1× and 2×. Exact qualification is recorded in
[verification.md](verification.md#m92-equation-library-gates-d-100).
The [M92b app gates](verification.md#m92b-equation-app-gates-d-107) cover
native editing, document history, schemas, PDF, window fixtures, squeeze,
the import-layout performance regression, and the reviewed app grabs.

The format parsers are not a full TeX engine, and conversion to linear text can approximate OMML
features with no equivalent spelling. Visual comparison uses synthetic
Word-shaped samples; real Word/PowerPoint application checks remain manual.
