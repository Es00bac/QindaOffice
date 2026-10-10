<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Visio interchange (QindaDiagram, `diagram_engine`)

M65 / D-073. QindaDiagram opens and saves Microsoft Visio drawings natively —
no LibreOffice in between — and a saved file opens in Visio with what the
user did not change exactly as Visio wrote it (the D-068 preservation
model, as for Word). M87 / D-095 adds what Visio's everyday tools store —
layers, hyperlinks, comments, containers and swimlanes, background pages,
print setup, Auto Size, line-jump styles and external data
([below](#what-m87-carries)).

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
- **Pages**: names, sizes and drawing scale; background pages are
  background pages, drawn behind the pages that name them (M87).

The status bar reports what is approximate: metafile pictures, embedded
objects, shapes kept but not shown (guides, ink), shapes whose text has
several formats (shown in one until edited).

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

## What M87 carries

Each row is read into the model and written from it (`visio_read_m87.cpp`,
`visio_data.cpp`, `visio_write_m87.cpp`); an item or page whose M87 values
are as read keeps its source cells as they were (verbatim or merged,
above), and only a changed value rewrites its cells.

| Feature | Where Visio keeps it | Notes |
| --- | --- | --- |
| Layers | PageSheet `Layer` section: a row per layer (`IX` from 0) with all eleven cells — `Name`, `Color` (255 none, 0–23 Visio's palette, else `#rrggbb`), `Status`, `Visible`, `Print`, `Active`, `Lock`, `Snap`, `Glue`, `NameUniv`, `ColorTrans`; a shape's `LayerMember` (`"0;2"`) | Snap is kept, not honoured |
| Hyperlinks | a shape's `Hyperlink` section, named rows `Row_1` … (`Description`, `Address`, `SubAddress`, `ExtraInfo`, `Frame`, `NewWindow`, `Default`, `Invisible`, `SortKey`) | a page of the drawing is the `SubAddress` |
| Comments | `visio/comments.xml` (`application/vnd.ms-visio.comments+xml`, related from the document part): `AuthorList`, `CommentList` with `PageID`, `ShapeID`, `Date`, `EditDate`, `Done`, `AutoCommentType` | Visio's comments part has no threads: a reply goes to Visio as a further comment on the same shape and reads back as a comment of its own; Visio 365's threaded comment parts are kept while comments are unchanged and dropped when they are rewritten |
| Containers, swimlanes, phases | `User.msvStructureType` (`Container`), `User.msvShapeCategories` (`Swimlane`, `Phase`, `CFF Container`; `DoNotContain` on lanes), `User.msvSDContainerResize` (0 none, 1 expand, 2 fit), `User.msvSDContainerMargin` (inches); `Relationships` — `SUM(DEPENDSON(1,Sheet.n!SheetRef(),…))` on a container naming its members, `SUM(DEPENDSON(4,…))` on a member naming its containers | membership stays geometric in the model (D-050); the Relationships are written from it |
| Background pages | the page's `Background="1"` and `BackPage="<page ID>"` | chains and cycles as in the app |
| Print setup | PageSheet `OnPage` (1 = fit to `PagesX` × `PagesY` sheets, else `ScaleX` / `ScaleY` is the zoom), `CenterX` / `CenterY`, `PaperKind` (Windows' DMPAPER: letter 1, tabloid 3, legal 5, A3 8, A4 9, A5 11), `PrintPageOrientation` | any other paper writes no `PaperKind` (Visio takes the printer's) |
| Auto Size | PageSheet `DrawingResizeType` (1 on, 2 off) | |
| Line jumps | PageSheet `LineJumpCode` (0 none, 1 horizontal, 2 vertical, 3–5 Visio's routing-order rules = last routed) and `LineJumpStyle` (0–1 arc, 2 gap, 3 square) | Visio's many-sided styles (4–9) show as square |
| External data | `visio/data/recordsets.xml` (`DataRecordSets`: a `DataRecordSet` per table with its `DataColumns`, `PrimaryKey` and `RowMap` rows) and `visio/data/recordsetN.xml` (the rows as an ADO persisted rowset, `_Visio_RowID_` first); a linked shape's values as Shape Data rows `_VisDM_<column>` with `DataLinked` | written without a `DataConnection`: Visio shows the rows and links but refreshes them only once linked to the file again; QindaDiagram keeps the file name and refreshes itself. `RowMap` follows [MS-VSDX] (no public sample has one) |

While the recordsets and links are as read, the package's own data parts
travel verbatim; any change rewrites them from the model. A fresh drawing
writes every M87 property the same way, so a drawing made in QindaDiagram
reaches Visio with its layers, links, comments, pools, background pages,
print setup and data.

## Known limits

- One text format per shape (the model's one run); a shape whose text has
  several keeps them until its text is edited.
- Connector routes are recomputed by QindaDiagram's router; Visio's own
  waypoints are not kept on an edited connector.
- One group level; deeper groups flatten.
- Group sheets' data are kept on unchanged and edited shapes but are not
  shown or editable.
- M87's limits (D-095): layer Snap is not honoured; many-sided line jumps
  show as square; replies reach Visio as comments, not threads; recordsets
  carry no DataConnection; Visio's lists and cross-functional flowchart
  frames (title band, lane orientation switch) are kept, not modelled.
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
drawing for a look). M87: `tst_diagram_m87_visio` (Visio's own layer,
hyperlink, comment, container, background-page, print, Auto Size, jump and
data parts read; untouched parts byte for byte while an edit rewrites only
what it touches; a fresh drawing with all of it written, schema-checked
where the shared OPC rules apply and read back equal; **LibreOffice**
renders an edited drawing — a lane inserted into the pool, a comment, its
background page behind it — and `pdftotext` finds the labels).
