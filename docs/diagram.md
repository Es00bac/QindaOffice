<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaDiagram: `diagram_engine` and the app

Flowcharts, org charts, network diagrams and everyday business drawings
(M50, decision [D-050](decisions.md#d-050-qindadiagram-a-drawing-model-of-its-own-on-the-toolkits-canvas-stack)).
Microsoft Visio is the reference for *scope* — stencils, glued connectors,
containers and swimlanes, shape data, pages, auto-layout — not for look.
Since M65 its file format too: `.vsdx`, `.vsdm`, `.vstx` and `.vstm` open
and save natively, with what the user did not change kept as Visio wrote it
([visio-interchange.md](visio-interchange.md), decision
[D-073](decisions.md#d-073-visio-interchange-for-qindadiagram)). Since M87
Visio's everyday tools — swimlanes and containers that behave, layers,
links, comments, external data, AutoConnect, more Re-Layout styles,
background pages, printing across sheets
([below](#visios-everyday-tools-m87), decision
[D-095](decisions.md#d-095-qindadiagram-visios-everyday-drawing-tools)).

| Target | Depends on | What it is |
| --- | --- | --- |
| `diagram_engine` | Qt Core/Gui/Svg, `office_core`, `office_ooxml` (M65), `calc_io` (M87, private: workbooks for external data) | The drawing model, stencils, routing, the one painter, edits, layout, file format, importers, `DrawingDocument`. No Quick. |
| `qindadiagram_app` | `office_quick`, `diagram_engine`, QindaTK | The host, the service, the canvas adapter and page view, the QML window. |
| `qindadiagram` | `qindadiagram_app` | The binary (`org.qindaqt.QindaDiagram`). |

## The drawing

A **drawing** (`Drawing`) is a list of **pages**; a page has a size, a
background colour, a grid setting, a line-jump setting, **shapes** and
**connectors**. One drawing is one `.qdiagram` file is one window
(rule 1).

- **Units**: one page unit is 1/96 inch (a CSS pixel — QindaTK's canvas
  and SVG unit). Paper sizes: letter, legal, tabloid, A5, A4, A3 in either
  orientation, or any custom size (0.5–200 in).
- **Shapes** carry a stencil id (`type`), an unrotated `rect`, a rotation
  (degrees, clockwise, about the centre), flips, text with one-run
  formatting (face, size, bold/italic/underline, colour, horizontal and
  vertical alignment), a style (fill, optional gradient end, line colour,
  width and pattern, corner radius, shadow, opacity), **shape data**
  (ordered key/value fields), an optional group id, locked and
  aspect-locked flags and, for pictures and org cards, the author's
  encoded image bytes. A shape read from Visio (M65) may carry its own
  outline instead of a stencil's — `paths` (`shape_path.h`: closed or open
  subpaths of lines, arcs and curves in the shape's 0–1 frame, each with
  fill/stroke flags; the hidden `path` stencil draws them) — and its own
  text box and connection points.
- **Connectors** carry two **endpoints** — glued to a shape's connection
  point (`port`), glued to its outline (`port = -1`, dynamic glue), or free
  at a page point — a routing (right-angle, straight, curved), line colour,
  width and pattern, start/end arrowheads (none, filled, open, diamond,
  dot), a label with its position along the route, data and a group id.
- **Z-order**: shapes paint in list order, then every connector above
  every shape; containers are inserted at the back. Icon captions (text
  below network icons and the actor) are painted last on a plate of the
  page colour, so no connector crosses a word.
- **Ids** (`s…`, `c…`, `p…`, `g…`) come from one counter per drawing and
  are never reused, even for a hand-edited file.

### Stencils

49 stencils in seven categories, all drawn by the engine (no image assets):

| Category | Stencils |
| --- | --- |
| Basic shapes | rectangle, rounded rectangle, ellipse, circle, diamond, triangle, hexagon, text, note |
| Flowchart | process, decision, start/end, data, document, predefined process, manual input, on-page connector, off-page reference, database, preparation |
| Org chart | person card (name, title, photo placeholder or photo), executive, vacancy, team container |
| Network | server, desktop, laptop, router, switch, firewall, cloud, database server, printer, phone — Lucide-style 2-unit round strokes on a tinted tile, caption below |
| Containers & swimlanes | container, horizontal lane, vertical lane, phase |
| UML | class (compartments separated by `--` lines), actor, use case, package |
| Arrows & callouts | block arrows (right, left, up, down, double), chevron, callout, speech bubble |

A stencil id is persisted in every shape — never rename one. An unknown
id (a newer file) draws as a rectangle and keeps its id.

### Behaviour a Visio user expects

- **Glue**: a connector end dropped within 10 view pixels of a connection
  point glues to it; elsewhere on a shape it glues to the outline (dynamic
  glue, which picks the side facing the other end). Moving a shape drags
  its connectors; deleting a shape leaves its connectors with free ends
  where they were.
- **Containers** (container, lanes, phase, team) carry the shapes lying
  inside them when they move; membership is geometric (inside and above),
  never stored. Since M87 they also grow round what is dropped in them,
  and lanes form pools ([Containers and swimlanes](#containers-and-swimlanes)).
- **Groups** are one level: grouping members of groups regroups them;
  selecting any member selects the group.
- **Routing** is a pure function of the page (`routing.h`): orthogonal
  routes leave along each end's normal, choose the cheapest of the L, Z
  and U candidates and detours around every shape (length, bends and
  crossings); when every candidate crosses a shape, a cheapest-path search
  on the visibility grid round the shapes finds one that does not (M87).
  Line jumps (per page) hop a later connector over each earlier one it
  crosses — or the horizontal or the vertical one of the two — as an arc,
  a gap or a square (M87).
- **Auto-layout** (`layout.h`): flowchart top-to-bottom or left-to-right
  (cycles broken, longest-path ranks, barycentre ordering, each shape
  pulled under its predecessors) and org chart trees (parents centred over
  children; a manager whose four or more reports are all leaves hangs them
  in a column beside a trunk); since M87 also bottom-to-top, right-to-left,
  compact tree, radial and circular, with the spacing between ranks and
  between neighbours. Selection or whole page; connectors re-glue to the
  facing connection points (radial and circular glue to the outlines and
  draw straight, as Visio does).
- **Themes** (`themes.h`) are document data: a fill/line pair per role
  (primary, decision, terminal, data, neutral, accent, container, note),
  connector and text colours and a font. New shapes take the drawing's
  theme; Design ▸ Themes restyles every shape and connector (sizes,
  patterns and shadows stay). Six themes: Qinda, Slate, Forest, Sunset,
  Ocean, Print (black and white).

## Visio's everyday tools (M87)

D-095. Each tool below is kept in `.qdiagram`, travels through `.vsdx` as
Visio writes it ([visio-interchange.md](visio-interchange.md#what-m87-carries))
and is one undo step per command.

### Containers and swimlanes

- **Membership stays geometric** (D-050). After every edit the service
  runs `Ops::settle(page, before)` in the same undo step: a container
  expands round the members it had and the shapes dropped in it this edit
  (a member dragged half out pulls the edge with it; a stranger lying
  across the edge does not), or fits them exactly, or stays put — Visio's
  *Automatic resize* (Expand as needed, the default; Always fit; No
  automatic resize, a phase's default), with a margin (an eighth of an
  inch unless set) and room for the header band.
- **Container commands**: Insert ▸ Container wraps the selection (behind
  it, fitted to it); Fit to Contents; Disband Container (the container
  goes, the members stay); Select Contents; Delete takes the members too
  (a connector between two that go goes with them).
- **Pools**: lanes of one orientation, unrotated, the same length and
  touching edge to edge are one pool — found, never stored, so M50
  drawings, Visio's own swimlanes and hand-placed lanes behave alike.
  Insert ▸ Swimlane adds after the selected lane (or at the end of the
  nearest pool), Swimlane Before before it; the lanes after it move down
  with their shapes. Deleting a lane closes the gap. Resizing a lane's
  depth pushes the lanes after it; making one longer makes all of them
  longer; dragging a lane along the stack moves it to that place with its
  shapes. A phase spans the pool across, clear of the lane headers, and
  keeps spanning it as lanes come and go.
- **Contextual Container tab** (a container or lane selected): Fit to
  Contents, Automatic resize, Select Contents, Disband Container,
  Swimlane, Swimlane Before, Phase; the inspector's container section
  holds the same resize choice and the margin.

### AutoConnect

With View ▸ AutoConnect on (the default), hovering a shape shows four
arrows; clicking one (or Arrange ▸ AutoConnect ▸ Add and Connect Above /
Right / Below / Left) connects to the nearest shape lying in that
direction, else adds the **quick shape** — the stencil last chosen in the
Shapes panel, else one like the source — 48 units beyond, clear of other
shapes, and connects to it (`Ops::autoConnect`). The new shape is selected
so the next click carries on.

### Layers

Edit ▸ Layers ▸ Layer Properties lists the page's layers (name, Visible,
Print, Lock, Active, Glue, colour, how many items) and adds, renames and
removes them; Assign to Layer puts the selection on the layers ticked.
Visio's rules (`layers.h`, the only place they live): an item on no layer
behaves as before; on layers it is hidden only when every one of them is
hidden, kept off paper and out of exports when none prints, locked when
any is locked, glued to when one takes glue; the first coloured layer
paints its lines and text. Hidden and locked items cannot be picked,
moved or selected with Select All. New shapes and connectors go on the
active layers. Snap is kept for Visio but not honoured (QindaTK's canvas
snaps to every shape).

### Links and comments

- **Insert ▸ Link** (Ctrl+K, the suite's `insertHyperlinkAction`): several
  links per shape or connector — a web address or file, or a page of this
  drawing — each with a description. Open Link (or Ctrl+click) goes to the
  page, or hands the address to the desktop (`QDesktopServices`, the portal
  in a QindaQt session). The PDF export carries no live links (Qt's PDF
  engine writes no link annotations).
- **Comments** (Review ▸ New Comment, Ctrl+Alt+M): on the selected item or
  the page, with the author's name and initials as Write and Show give
  them; replies, edits, Done (a whole thread) and Delete in the **Comments
  pane** (Review ▸ Comments Pane; the inspector's drawer below 900 px);
  a marker on the page for each commented item; Previous / Next walk every
  thread of the drawing, page by page, and wrap. Markers are screen-only: comments never print.

### Pages: backgrounds, Auto Size, printing

- **Background pages**: Insert ▸ Background Page makes one and shows it
  behind the current page; Page Setup marks a page as a background or
  chooses the background behind it. A background is drawn under its pages
  on screen, in print and in exports, and can have a background of its own
  (drawn farthest first; a cycle is refused). Design ▸ Borders & Titles
  puts a border and a title block on the page's background (made when
  there is none).
- **Auto Size** (Design ▸ Auto Size, Page Setup): the page grows and
  shrinks by whole sheets of its print paper so everything fits; what lies
  left of or above the page moves in by whole sheets.
- **Print setup** (File ▸ Page Setup ▸ Print): the printer paper and its
  orientation, then either a print zoom (100 % prints actual size across as
  many sheets as it takes) or fit to N sheets across by M down, and
  centring. View ▸ Page Breaks shows where the sheets meet. Print, Print
  Preview and Export PDF follow it (`print_tiles.h`: a sheet's printable
  area is the paper less the host's print margin, sheets left to right then
  top to bottom). Untouched, a page prints as M50 printed it.
- **Line jumps**: Format ▸ Connector ▸ Line Jump Style (Design ▸ Line
  jumps on the ribbon) chooses arc, gap or square and which line jumps:
  the last routed, the horizontal or the vertical one.

### Re-Layout

Arrange ▸ Auto-Layout (and Design ▸ Re-layout): flowchart top to bottom,
bottom to top, left to right, right to left; org chart tree; compact tree
(children under their parent, indented, as a file tree reads); radial (the
root in the middle, each level on a ring, every subtree a wedge as wide as
its leaves); circular (every shape on one ring, neighbours together).
Layout Options sets the style and the two spacings, in inches or
centimetres; the selection is arranged, else the page.

### External data

- **Data ▸ Link Data to Shapes** reads an Excel workbook (`.xlsx`,
  `.xlsm`; through calc_io, so cells read as Excel shows them; a sheet is
  chosen when there are several; `.xls` is refused with what to do) or a
  CSV file (RFC 4180, the first row naming the columns; blank rows
  skipped), at most 64 MB, 256 columns and 20 000 rows. It shows the first
  rows, asks how a row is known (a key column, or its position) and can
  link at once every shape whose text (or a shape data field) equals a
  column. The table is kept in the drawing (`DataRecordset`), so the
  drawing opens with its data anywhere.
- A linked shape holds one shape-data field per column, in column order,
  beside its own fields (`data_link.h` AGENT-CONTRACT).
- **External Data** (Data ▸ External Data): the rows with how many shapes
  each feeds; link a row to the selection, link automatically by a column,
  make a shape per row (the quick shape, text from a chosen column),
  refresh, remove the data (its shapes are unlinked, their values kept).
- **Refresh All** reads every source file again: linked fields are
  rewritten, rows that went are counted (their shapes keep their values),
  the key column is found by its name wherever it moved. **Unlink from Row** keeps the
  values. Data graphics are not drawn yet.

### Commands, menus and ribbon (M87)

40 new commands (their ids in D-095) plus Insert ▸ Link: Edit ▸ Layers;
View ▸ Page Breaks, AutoConnect; Insert ▸ Background Page, Swimlane
Before, Link, Open Link; Format ▸ Connector ▸ Line Jump Style; Arrange ▸
Container, AutoConnect, the Auto-Layout styles and Layout Options;
Design ▸ Auto Size, Borders & Titles; new **Data** and **Review** menus.
The ribbon gains Data and Review tabs, the contextual Container tab,
Home ▸ Layers, Insert ▸ Links, Design ▸ Line jumps, and every new command
somewhere on it (the ribbon gate).

## The `.qdiagram` file

UTF-8 JSON, pretty-printed: `{"format": "qindadiagram", "version": 1,
"title", "theme", "serial", "pages": […]}`; a page `{id, name, width,
height, background, grid, gridSpacing, lineJumps, shapes, connectors}`;
shapes and connectors as the model above (colours as `#rrggbb` /
`#aarrggbb`, empty for none; pictures as base64).

- M87 members, each written only when it differs from its default, so an
  M50 drawing saves byte for byte as before: on a shape `layers`, `links`,
  `dataLink`, `structure` (a container kind read from Visio),
  `containerResize`, `containerMargin`; on a connector `layers`, `links`;
  on a page `jumpStyle`, `jumpOn`, `layers`, `comments`, `backgroundPage`,
  `backPage`, `autoSize`, `print` (`paperWidth`, `paperHeight`, `zoom`,
  `across`, `down`, `center`); on the drawing `recordsets` (name, source,
  sheet, columns, rows, key column, refresh time). Ids `m…` (comments)
  and `r…` (recordsets) come from the same counter as the rest.
- Saves are atomic (`QSaveFile`).
- Members this version does not know are kept per object (root, page,
  shape, connector) and written back, so a newer minor revision survives a
  round trip; a file with a **newer** `version` is refused with a message.
- The same shape/connector JSON is the clipboard fragment
  (`application/x-qindadiagram-fragment`); glued ends carry their current
  point so a paste without its shapes frees them in place.
- MIME `application/x-qindadiagram` (`data/mime/qindaoffice.xml`, glob
  `*.qdiagram`, JSON magic).

## Output and import

- **One painter** (`painter.h`) draws the canvas, the Shapes panel
  thumbnails, Print, Print Preview, Export PDF (the host's one
  `PrintRenderCallback`: each page fitted on one sheet, turned a quarter
  when page and sheet orientation differ — or, since M87, across the
  sheets its print setup asks for, its background pages under it and
  non-printing layers left out), PNG (2×, page or selection,
  96-dpi metadata), SVG (`QSvgGenerator`, page or selection) and Copy as
  Picture (PNG + SVG on the clipboard). Text is laid out at a fixed
  reference size and scaled, so line breaks match on every device.
- **Org chart from CSV**: `Name, Title, Manager` (loose header names:
  full name/person; position/role; reports to/supervisor), every other
  column becomes shape data; RFC 4180 quoting. The top of the tree is an
  executive card; the chart is tree-laid-out and centred on the page;
  unknown managers are reported.
- **draw.io / diagrams.net**: uncompressed `.drawio`/`.xml` — each
  `<diagram>` becomes a page; vertices map to the nearest stencil by style
  (rect, rounded, ellipse, rhombus, triangle, hexagon, parallelogram,
  cylinder, cloud, document, process, actor, note, swimlane, step, arrows,
  callout, image, text and the `mxgraph.flowchart.*` set) with fill,
  stroke, font colour and size, bold/italic/underline, alignment, dashes,
  rotation, flips and `UserObject` attributes as shape data; child
  geometry is resolved against its parents; edges keep their
  source/target glue (exit/entry points map to the nearest connection
  point), style (orthogonal/curved/straight), arrows and labels (edge
  label children included).
- **Visio** (M65, D-073): `.vsdx`/`.vsdm`/`.vstx`/`.vstm` open as
  drawings — masters, styles and themes resolved, geometry as custom
  outlines, groups flattened, glued connectors, pictures — and save as
  Visio packages, unchanged shapes verbatim and edited ones merged into
  their source ([visio-interchange.md](visio-interchange.md)). File ▸ Save
  As offers *Visio Drawing (*.vsdx)*; a drawing opened from Visio saves
  back to it. The Visio 2003–2010 formats (`.vsd`, `.vdx`, …) are refused
  with what to do in Visio.
- **Not supported**: compressed draw.io diagrams (refused with the
  diagrams.net setting that turns compression off); rich text runs inside
  one shape (one run per shape); per-page zoom memory (each page opens
  fitted); data graphics, drawing scale and guides (the rest of Visio's
  everyday features still missing are ranked in
  [plans/office-parity.md](plans/office-parity.md#qindadiagram-against-microsoft-visio-survey-of-2026-10-09-m87)).

## DrawingDocument and undo

`DrawingDocument : Core::Document` holds the drawing. Every change is
`edit(label, change)`: the change runs on a copy and, only when it
returns true and the drawing really changed, one snapshot command
(before, after) goes on the document's `QUndoStack`. Implicit sharing
keeps untouched pages shared, so a step costs one page's items. Keyboard
nudges and slider drags merge (`mergeKey`). Dirty follows the undo
stack's clean state; recovery snapshots are the file's JSON.

## QindaDiagram (app)

`DiagramHost : DocumentHost` owns the `DrawingDocument`, the window's
`DiagramService` (`diagram`) and the `diagram…Action` commands — 101 at
M50, 40 more in M87 (`diagram_host_m87.cpp`; never rename one;
`--list-commands` prints them) — plus the shared text ids
(`formatBold…`, `formatAlign…`, `formatGrow/ShrinkFont`,
`formatTextColor`) and `insertHyperlinkAction`. Menus: File (Import,
Export, Page Setup), Edit (Duplicate, Delete, Edit Text, Shape Data,
Format Painter, Tools, Layers), View (panels, rulers, grid, snap, page
breaks, AutoConnect, fit), Insert, Format (Text, fill, line, quick
styles, Connector), Arrange (Align, Distribute, Order, groups,
rotate/flip, aspect lock, Container, AutoConnect, Auto-Layout), Design
(orientation, paper, fit, Auto Size, background, Borders & Titles,
Themes), Data, Review. Ribbon (`DiagramRibbonLayout.qml`): File · Home ·
Insert · Design · Arrange · Data · Review · View, plus the contextual
**Shape**, **Connector** and **Container** tabs (`ribbonContexts`).

`DiagramService` is the whole QML API: pages, the selection (item ids,
group-expanded), the tool (`pointer` / `connector` / `text`, mirrored by
the tool radio group Ctrl+1/2/3), the format painter, item maps with
fixed keys, and one undo step per edit call; refusals come back as
`notify(title, text, "warning")`.

Open lists QindaDiagram and Visio drawings (the Visio 2003–2010 formats
too, so they can be refused with what to do); Save As offers QindaDiagram,
Visio drawing and Visio template, and the suffix decides the format. A
Visio file opened says on the status bar what it shows approximately or
only keeps (`DrawingDocument::loadWarnings`). The desktop entry declares
the Visio MIME types and the launcher routes `.vsdx`/`.vsdm`/`.vstx`/
`.vstm` here (M65).

### The canvas (D-050)

QindaDiagram builds on QindaTK's canvas stack — `CanvasView`,
`ToolController`, `SpatialTransform`, `SnapModel`, `SelectionOverlay`,
`NavigatorView`, `Ruler` — not on `DiagramCanvas` (see the decision):

- `DiagramCanvasAdapter` (a `CanvasDocumentAdapter`, QindaTK's
  `SlideCanvasDocument` pattern) shows the current page as **pick
  proxies**: a shape is an image object (a transparent pixel) on its
  rotated box, a connector a polyline along its route. The canvas picks,
  hovers and outlines them; `commit()` turns a gesture's edit into one
  undo step (moves carry container members and free ends; resizes and
  rotations project to rect + rotation).
- `DiagramPageView` paints the page under the transparent `CanvasView`
  with the engine's painter, in the view's pose, plus the live feedback:
  the transform preview (shapes follow the handles, connectors re-route
  while dragging), dynamic alignment guides, connection points and the
  glue ring for the connector tool, a stencil ghost while dragging from
  the panel, and a lone connector's end handles.
- `CanvasInput` (left button only; navigation stays with the canvas):
  select, Shift/Ctrl toggle, drag-move with snapping (objects, guides,
  quarter-grid), marquee, connector end re-glue, format-painter click,
  connector tool, text tool; keys Delete, arrows (quarter-grid; Shift = 1
  unit), Enter/F2, Escape.
- The **Shapes panel** is a `Tk.ShapePalette` with the painter's own
  thumbnails (QindaTK D-302: tiles, a category menu, search across
  stencils, drag reports); a drag drops the shape where it is released, a
  click in the middle of the view; drops snap their centre to the
  quarter-grid.
- The **inspector**: with a selection, Style (`Tk.SpatialTransformEditor`
  over the canvas's `SpatialTransform`, `Tk.FillStyleEditor`,
  `Tk.StrokeStyleEditor`, shadow, corners, opacity, quick styles; for
  connectors routing, arrowheads and label position), Text and Data
  (shape data); without one, the page (name, paper, orientation, size,
  background, grid, line jumps, fit) and the theme list. A container adds
  its section (automatic resize, margin, Fit to Contents, Disband; M87).
- The **Comments pane** (M87) takes the inspector's place while it is
  open; the dialogs of M87 — Layer Properties, Assign to Layer, Links,
  Layout Options, Link Data to Shapes, External Data and the extended Page
  Setup — are window children like the others.
- Rulers in inches (centimetres where the locale is metric) with the page
  band and the pointer; page tabs, page menu and zoom in the bar under the
  page; the minimap (off by default) in the canvas corner. Below 900 px
  the Shapes panel and the inspector become drawers opened from the
  canvas's top-right corner.
