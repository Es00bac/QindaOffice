<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Word charts, SmartArt and drawing shapes

M93 / D-101 adds native graphic workflows to QindaWrite. Insert ▸ Chart,
SmartArt or Shape creates an ordinary DrawingML object in the current
story. Format ▸ Edit Graphic, the Insert ribbon or a double-click opens its
editor. Apply makes one Undo step; Cancel leaves the object unchanged.
Delete, save/reopen, crash recovery, print and PDF use the document's normal
paths. An editor refuses to overwrite a graphic changed while it was open.

## Charts

Chart data is a tab-separated table: a header row names the series, the
first column contains categories, and the remaining columns contain
numbers. Paste a rectangular range from a spreadsheet or edit its text.
Blank values are gaps; scatter charts require numeric X values in the first
column. Validation requires a header and 1–256 rows with
1–32 series; malformed or nonfinite numeric values leave the chart intact.

The supported kinds are clustered/stacked/percent-stacked columns and
bars, lines and stacked lines, ordinary/stacked/percent-stacked areas,
pies, doughnuts and scatter charts. A combo chart edits one plot's kind
at a time and retains its series membership and outer category levels.
Imported point colours, data labels, chart styles and unedited markup are
preserved by the shared Show chart patcher. Charts without usable cached
series, ChartEx and unsupported plot kinds keep the previous preserved
placeholder and warning.

Saving edited data updates the chart's formulas and caches **and** its
embedded `.xlsx`, through `calc_io`. A chart linked to an external workbook
retains that link; the external file is never opened or modified. Its next
external refresh can therefore replace local cache edits. New charts get
an embedded workbook. A workbook-write failure fails the document save.

## SmartArt

Imported SmartArt displays its cached DrawingML drawing with its shape
geometry, fills and text. The editor presents a separate multiline field
for each data node. An edit updates the data model and matching cached
drawing text, while retaining the producer's layout, style and colours.
Presentation point aliases (`presOf` / `presAssocID`) join the two forms.
An unchanged graphic and all its related parts are carried verbatim.

Insert SmartArt creates a three-node vertical list with native diagram
data, layout, style, colours and a cached drawing. Node text and overall
size are editable. Changing diagram topology, choosing arbitrary SmartArt
layouts and editing connections are outside this milestone. A source
without a cached drawing shows a simple list preview; its original layout
and data remain in the saved file. The editor is bounded to 256 nodes.

## Shapes

Wordprocessing DrawingML shapes and text boxes display through the shared
shape renderer. Edit text, fill, outline colour/width and size; choose
rectangle, rounded rectangle, ellipse, triangle, diamond, right arrow or
line. A changed property patches its source XML; unrelated effects and
extensions remain intact. Changing text keeps the first run's appearance,
so mixed formatting inside changed text is flattened to that appearance.

Legacy VML, drawing groups and unsupported custom structures keep their
preserved representation. Imported preset/custom geometries the renderer
already understands remain visible; the editor does not expose their
individual adjustment handles. Floating objects use the existing Word
picture layout approximation, and preserve the source anchor on save.

## Model and contracts

`office_docx/docx_graphics.h` owns format properties 160–163: kind, current
content, import snapshot and source part. A graphic stays one image object
in `QTextDocument`. Each edited rendering gets a content-derived image URL,
so Undo/Redo cannot display another revision's pixels. Screen, copied
document layouts and PDF use that same high-resolution rendering.

The public `show_model/chart_interchange.h` API reads, maps, paints and
writes charts for both Word and Show; `drawing_interchange.h` adapts Word
shapes and cached diagram drawings. Word does not include the private
PowerPoint reader/writer headers. All remain Core/Gui libraries with no
application or QtQuick dependency. An independent relationship copier
detaches an edited graphic from another reference to the same source part.

Native graphics must not enter Picture commands: an empty image format is
not a picture merely because Qt considers its format valid. Text typed
beside a graphic starts from the paragraph's text format, so it cannot
inherit the object's preserved XML identity.

## Verification

`tst_docx_graphics` covers all chart kinds, native insert/read/write,
ECMA schema checks including diagram parts, embedded workbook values,
SmartArt data/drawing text updates, exact unchanged part preservation,
shape edits, distinct Undo resources and atomic invalid edits. A headless
LibreOffice conversion verifies chart labels, SmartArt node text and shape
text in an independently rendered PDF; its pixels are reviewed too.
`tst_write_graphics` drives the real window at 1280 and 520 pixels, applies
the editors, tests stale dialogs and ordinary text after objects, saves and
reopens, recovers a snapshot and renders PDF through Poppler. Existing Show
chart/combo, Word reader/writer, picture/wrap, ribbon, menu and fixture gates
cover the shared paths. Actual run evidence is recorded in
[verification.md](verification.md).

The headless gates use synthetic documents. Microsoft Word desktop
comparison, SmartArt layout regeneration in Word and live printing remain
manual checks; no owner's documents or live application session are opened
by these tests.
