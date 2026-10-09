<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Visio interchange (QindaDiagram, `diagram_engine`)

M65 / D-073. QindaDiagram opens and saves Microsoft Visio drawings natively —
no LibreOffice in between — and a saved file opens in Visio with what the
user did not change exactly as Visio wrote it (the D-068 preservation
model, as for Word).

## Formats

| Format | Open | Save |
| --- | --- | --- |
| `.vsdx` Visio drawing | native (`visio_read*.cpp`) | native — File ▸ Save As ▸ Visio Drawing |
| `.vsdm` macro-enabled drawing | native | native; the source's VBA project is carried, never run |
| `.vstx` / `.vstm` template | native | native (template content types) |
| `.vsd`, `.vss`, `.vst`, `.vdx`, `.vsx`, `.vtx` (Visio 2003–2010) | refused with what to do: *Save As ▸ Visio Drawing (*.vsdx)* in Visio | refused the same way |
| `.qdiagram`, `.drawio` | as before (D-050) | `.qdiagram` |

The open and save dialogs list every format (`DiagramHost::filterString`),
the desktop entry declares the four Visio MIME types plus
`application/vnd.visio`, and the launcher routes them to QindaDiagram. A
drawing opened from Visio saves back to the same Visio file; crash recovery
journals the model as JSON and finds the source package again by its
content hash, so a recovered drawing still saves with its preservation.

## Reading (`visio_read*.cpp`, `visio_sheet.*`, `visio_geometry.*`, `visio_text.*`)

- **Parts**: `visio/document.xml` (style sheets, colours, fonts),
  `pages/pages.xml` and each page, `masters/masters.xml` and each master,
  the theme. Visio's namespace is the default one and relationship ids
  are the literal `r:id`, as Visio and LibreOffice expect.
- **The ShapeSheet** (`visio_sheet`): a cell's value is the shape's own,
  else its master shape's (a group's members inherit through the group's
  master), else its style chain (`LineStyle` / `FillStyle` / `TextStyle`);
  sections merge row by row by `IX`, and `Del="1"` removes an inherited row.
  The cached `V` value is what is read — formulas are kept, not evaluated.
  `V="Themed"` resolves through the theme's colours and the shape's
  `QuickStyle…` indices.
- **Shapes**: position from `PinX/PinY/LocPin/Angle/FlipX/FlipY` (flips
  before rotation, Visio's y-up inches to the page's pixels at its drawing
  scale); geometry rows (`MoveTo`, `LineTo`, `ArcTo`, `EllipticalArcTo`,
  `Ellipse`, the `Rel…` rows, `PolylineTo`, `NURBSTo`, `SplineStart/Knot`,
  `NoFill`/`NoLine`/`NoShow`) become the model's custom outlines
  (`Shape::paths`, `shape_path.h`) and are drawn exactly; fill (pattern,
  colours, transparency), line (weight, colour, dashes, caps, arrows),
  text (font, size, colour, bold/italic/underline, alignment, the text
  block's own transform as `Shape::textBox`), shape data (`Property`
  rows) and connection points (`Connection` rows as `Shape::ports`).
- **Groups** flatten to the model's one level; members keep their
  composed transforms, and a group's own text or geometry becomes a member.
- **Connectors** (1-D shapes) keep their end points and their glue: the
  page's `<Connects>` and the `BeginX/EndX` formulas
  (`PAR(PNT(Sheet.N!Connections.Xk…))` to a point, `_WALKGLUE` to the
  whole shape); route style (right-angle, straight, curved) and arrows.
- **Pictures** keep their bytes (PNG, JPEG, GIF, BMP, TIFF). EMF/WMF
  pictures and embedded OLE objects are kept in the file and shown as
  frames; the status bar says how many.
- **Pages**: names, sizes and drawing scale; background pages show as
  ordinary pages (and stay background pages on save).

The status bar reports what is approximate: metafile pictures, embedded
objects, shapes kept but not shown (guides, hidden layers, ink), shapes
whose text has several formats (shown in one until edited), background
pages.

## Writing (`visio_write*.cpp`)

Each item is written in one of three modes, decided by comparing it with
the model item the reader made (`Source`, `visio_source.h`):

- **verbatim** — unchanged: its `<Shape>` element as read, byte-equal
  attributes and children;
- **merged** — changed: the source element with the cells the model maps
  rewritten (`setCell` puts new cells before the sections, Visio's order);
  every other cell, section, formula, layer membership and hyperlink stays;
- **fresh** — new or from another source: explicit geometry, fill, line,
  text with its character and paragraph rows, shape data, connection
  points; connectors with `BeginX/EndX` glue formulas, `BegTrigger` /
  `EndTrigger`, `ObjType`, route style and the page's `<Connects>` rows.

A moved group moves as one (its members' source elements ride along); new
shapes take IDs above every ID the page used; `NameU` stays unique; the
source's z-order is kept and new shapes go on top. Masters, the theme,
the VBA project (macro kinds), solutions and every other part travel
through `RelationshipCopier`; `windows.xml` is kept when every page it
names is still written. A drawing with no source gets Visio's standard
style sheets (No Style, Text Only, None, Normal, Guide, Connector), a
`windows.xml`, document properties and `docProps/custom.xml` with
`RecalcDocument`, so Visio recomputes what we could not.

## Known limits

- One text format per shape (the model's one run); a shape whose text has
  several keeps them until its text is edited.
- Connector routes are recomputed by QindaDiagram's router; Visio's own
  waypoints are not kept on an edited connector.
- One group level; deeper groups flatten.
- Layers, hyperlinks and group sheets' data are kept on unchanged and
  edited shapes but are not shown or editable.
- Per-section colours of a multi-geometry shape and glyph fills are not
  written for fresh or edited shapes.
- Theme effects, gradients and variations are approximated by the theme's
  plain colours; a fresh document carries no theme part.
- The Visio 2003–2010 binary and XML formats are not read.

## Gates

`tests/diagram_engine`: `tst_diagram_visio_read` (masters, style chains,
groups, glue, text, geometry rows, pictures, pages, themed colours, legacy
refusal — on hand-built packages, `visio_fixture.h`),
`tst_diagram_visio_write` (a fresh drawing's package, template and macro
kinds, DrawingDocument open/save/recovery, and the **LibreOffice check**:
the export converted to PDF by headless LibreOffice, its text read back
with `pdftotext`; skipped when either is missing),
`tst_diagram_visio_roundtrip` (unchanged drawing written as it was, edits
rewrite only what changed, a moved group, a resized page; `QQO_VISIO_CORPUS`
lists real `.vsdx` files to read, write, re-read and compare — aggregate
counts only). `tests/office_ooxml/tst_ooxml` covers `xml:space` and braced
values. `tests/qindadiagram`: `visioDrawingsOpenAndSave` (host) and
`visioDrawingRenders` (the window; `QQO_VISIO_UI_FILE` swaps in a real
drawing for a look).
