<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaDiagram: `diagram_engine` and the app

Flowcharts, org charts, network diagrams and everyday business drawings
(M50, decision [D-050](decisions.md#d-050-qindadiagram-a-drawing-model-of-its-own-on-the-toolkits-canvas-stack)).
Microsoft Visio is the reference for *scope* — stencils, glued connectors,
containers and swimlanes, shape data, pages, auto-layout — not for look.
Since M65 its file format too: `.vsdx`, `.vsdm`, `.vstx` and `.vstm` open
and save natively, with what the user did not change kept as Visio wrote it
([visio-interchange.md](visio-interchange.md), decision
[D-073](decisions.md#d-073-visio-interchange-for-qindadiagram)).

| Target | Depends on | What it is |
| --- | --- | --- |
| `diagram_engine` | Qt Core/Gui/Svg, `office_core` | The drawing model, stencils, routing, the one painter, edits, layout, file format, importers, `DrawingDocument`. No Quick. |
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
  never stored.
- **Groups** are one level: grouping members of groups regroups them;
  selecting any member selects the group.
- **Routing** is a pure function of the page (`routing.h`): orthogonal
  routes leave along each end's normal, choose the cheapest of the L, Z
  and U candidates and detours around every shape (length, bends and
  crossings), never cutting through a shape when a candidate avoids it.
  Line jumps (per page) hop a later connector over each earlier one it
  crosses.
- **Auto-layout** (`layout.h`): flowchart top-to-bottom or left-to-right
  (cycles broken, longest-path ranks, barycentre ordering, each shape
  pulled under its predecessors) and org chart trees (parents centred over
  children; a manager whose four or more reports are all leaves hangs them
  in a column beside a trunk). Selection or whole page; connectors re-glue
  to the facing connection points.
- **Themes** (`themes.h`) are document data: a fill/line pair per role
  (primary, decision, terminal, data, neutral, accent, container, note),
  connector and text colours and a font. New shapes take the drawing's
  theme; Design ▸ Themes restyles every shape and connector (sizes,
  patterns and shadows stay). Six themes: Qinda, Slate, Forest, Sunset,
  Ocean, Print (black and white).

## The `.qdiagram` file

UTF-8 JSON, pretty-printed: `{"format": "qindadiagram", "version": 1,
"title", "theme", "serial", "pages": […]}`; a page `{id, name, width,
height, background, grid, gridSpacing, lineJumps, shapes, connectors}`;
shapes and connectors as the model above (colours as `#rrggbb` /
`#aarrggbb`, empty for none; pictures as base64).

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
  when page and sheet orientation differ), PNG (2×, page or selection,
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
  fitted).

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
`DiagramService` (`diagram`) and 101 `diagram…Action` commands (never
rename one; `--list-commands` prints them) plus the shared text ids
(`formatBold…`, `formatAlign…`, `formatGrow/ShrinkFont`,
`formatTextColor`). Menus: File (Import, Export, Page Setup), Edit
(Duplicate, Delete, Edit Text, Shape Data, Format Painter, Tools), View
(panels, rulers, grid, snap, fit), Insert, Format (Text, fill, line,
quick styles, Connector), Arrange (Align, Distribute, Order, groups,
rotate/flip, aspect lock, Auto-Layout), Design (orientation, paper, fit,
background, Themes). Ribbon (`DiagramRibbonLayout.qml`): File · Home ·
Insert · Design · Arrange · View, plus the contextual **Shape** and
**Connector** tabs (`ribbonContexts`).

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
  background, grid, line jumps, fit) and the theme list.
- Rulers in inches (centimetres where the locale is metric) with the page
  band and the pointer; page tabs, page menu and zoom in the bar under the
  page; the minimap (off by default) in the canvas corner. Below 900 px
  the Shapes panel and the inspector become drawers opened from the
  canvas's top-right corner.
