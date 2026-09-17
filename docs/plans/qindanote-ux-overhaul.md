<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaNote UX overhaul — implementation plan (M22 → M26)

**Audience:** Kimi, the implementing agent. This document is self-contained:
it describes what QindaNote is today, exactly why it is unpleasant to use,
the target design, and five milestone-sized work packages with file lists,
signatures, action ids, tests and gates. Work through the milestones in
order; each one is a shippable commit on its own branch.

**Status:** plan, not yet started. Written against commit `251b2e2`
(M21). Nothing here changes the notebook file format in a way an M21 build
cannot open (see §5).

---

## 0. Read this first

1. `AGENTS.md` — non-negotiable. The rules that bite this work most:
   - **Stock Qt 6 widgets + platform theme.** No `setStyle`, no QSS, no
     palette hex literals for *chrome*. The one sanctioned exception is the
     document surface: a page is paper (D-004) and is pinned white with
     black text. Ink colours are *document data*, not chrome, and may be
     literal (add an `AGENT-NOTE` where you do it).
   - **Icons via `QIcon::fromTheme()`** with freedesktop names. When the
     theme lacks a name, ship the SVG into `data/icons/hicolor/scalable/actions/`
     (D-013). Never ship `hicolor/index.theme`.
   - **Every command is a persistent window-scoped `QAction` with a stable
     `objectName`** (rule 5). Existing ids (`notePenTool`, `noteTextTool`,
     `notePenColorAction`, `notePageNew`, …) must never be renamed or
     reused. New ids are listed in §3.7; use them verbatim.
   - **≤ 500 non-blank lines per hand-written file.** Several files in this
     plan are split for that reason; the splits are named below.
   - **Docs are part of the change.** Every milestone updates
     `docs/index.md` (the QindaNote row), `docs/verification.md` (gates) and
     adds a `docs/decisions.md` entry (numbers given below).
   - **Never override `MAKEOPTS`** or add `-j` flags. Plain
     `cmake --build --preset dev` is compliant.
   - **One document = one window.** No tabs, no continuous multi-page
     scrolling across sections, no document switcher.
   - **office_ink never links Qt Widgets.** Geometry, smoothing, erasing,
     selection maths and clipboard codecs go in `libs/office_ink`; anything
     that touches a `QWidget` stays in `apps/qindanote`.
2. `docs/decisions.md` entries D-004, D-009, D-011, D-012, D-013, D-017,
   D-022. This plan supersedes one clause of D-009 (fonts now scale with
   zoom) and extends D-022 (blocks gain a rect). Record both in new entries.
3. The code. It is small (≈3.2k lines) — read all of it before starting:
   - `apps/qindanote/src/note_window.{h,cpp}`, `note_window_ui.cpp`,
     `note_tool_strip.cpp`
   - `note_page_view.{h,cpp}`, `note_page_blocks.cpp`, `note_blocks.{h,cpp}`,
     `note_page_render.{h,cpp}`
   - `note_structure.{h,cpp}`, `note_find_bar.{h,cpp}`, `notebook.{h,cpp}`
   - `libs/office_ink/src/*` (stroke model, builder, spatial grid,
     persistence + renderer)
   - `libs/office_shell/src/wrapping_tool_bar.h`, `document_window.h`,
     `standard_actions.h`, `preferences.h`, `preferences_dialog.h`
   - `tests/qindanote/*` — every existing test encodes a past data-loss or
     crash regression. They must all stay green.

### Build and test

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure -R 'note'
```

Widget tests run offscreen (`QT_QPA_PLATFORM=offscreen`, pinned in each
test's `main`). New test files must be added to `tests/qindanote/CMakeLists.txt`,
and **every new app source file must be added to the app target AND to both
test targets** (`tst_qindanote`, `tst_note_blocks` list sources explicitly).

### Process per milestone

Branch `feat/note-m22-pen` (etc.) → implement → all `ctest -R note` green →
docs updated in the same commit → one commit with an imperative subject and a
body naming what landed, which gates passed, and what remains. Do not start
the next milestone on top of an unreviewed one unless told to.

---

## 1. What QindaNote is today

One notebook = one directory (`notebook.json` index; `sections/<id>/pages/<id>.json`
holding an `Ink::PageEnvelope`: title, background, stroke list, `blocksJson`).
One notebook = one window. Pages load lazily (`NotePage::loaded`).

The window: a `QTreeWidget` navigator (sections → pages) on the left, the
`NotePageView` inside a `QScrollArea` in the middle, a find bar underneath,
an Assistant dock on the right. The shell's main toolbar (New/Open/Save,
Undo/Redo, Cut/Copy/Paste, Find, Zoom) is on top. QindaNote's own tool strip
is a `WrappingToolBar` docked on the **left**, holding text-only buttons
(no icons ship for them): Pen · Highlight · Eraser · Text · a "px" spinbox ·
"Pen color" (opens `QColorDialog`) · background combo · zoom combo · Undo ·
Redo · Add text · Checklist · Image · New Page · New Section.

The page is 760 × 1004 page units, grows downward in sheet increments,
backgrounds blank/ruled/grid/dotted. Ink: pressure-width polylines drawn
segment by segment. Text: `QTextEdit`s stacked full-width at fixed 90 px
each; checklists are a `QCheckBox` + `QLineEdit` row at 28 px; images are
painted (bytes base64 in the envelope). Undo is a snapshot stack of the ink
layer only. Print/PDF go through `renderNotePage`, which shares the block
layout with the screen (D-022).

## 2. Why it sucks — the defect list

Every item below was verified by reading the code at `251b2e2`. Function
names are given rather than line numbers because the lines will move.

### Pen

| # | Defect | Where |
|---|--------|-------|
| P1 | **No antialiasing.** Neither `NotePageView::paintEvent` nor `renderNotePage` sets `QPainter::Antialiasing`; every stroke is jagged. | `note_page_view.cpp`, `note_page_render.cpp`, `ink_persistence.cpp::renderStroke` |
| P2 | **Highlighter width multiplier is dead code.** `renderStroke` sets `pen.setWidthF(stroke.width * 3.0)` for highlighters and then overwrites it with `widthAt(i)` inside the loop. A highlighter is 3–9 px, not the intended 18. | `ink_persistence.cpp::renderStroke` |
| P3 | **Translucent highlighter drawn segment by segment** with round caps → double-alpha blobs at every joint. | same |
| P4 | **Eraser only acts on pen-up.** Erasing happens in `commitStroke`, so dragging the eraser does nothing until you lift the pen. | `note_page_view.cpp::commitStroke` |
| P5 | **Eraser pushes an undo snapshot even when nothing was erased**, and `StrokeLayer::removeAt` is a linear scan per eraser sample that ignores the spatial grid. | `commitStroke`, `stroke_model.cpp::removeAt` |
| P6 | **Stroke eraser only** — no partial eraser, no eraser size, no eraser cursor. | — |
| P7 | **Full-widget `update()` on every `TabletMove`** → the whole visible page repaints per pen sample. | `note_page_view.cpp::event` |
| P8 | **No smoothing** beyond min-distance resampling; fast strokes are visibly polygonal. | `stroke_builder.cpp` |
| P9 | **Pen colour needs a modal `QColorDialog`**; no swatches, no presets; highlighter colour fixed yellow; width is a spinbox with " px". | `note_tool_strip.cpp` |
| P10 | **No tool cursors, no keyboard shortcuts for tools, stylus barrel buttons ignored.** | `note_page_view.cpp`, `note_tool_strip.cpp` |
| P11 | **No selection tool at all**: ink cannot be moved, scaled, recoloured, copied or deleted except by erasing. | — |
| P12 | **Dark theme makes ink invisible.** The page is filled with `palette().base()`; under the dark platform theme the page is dark and the default black pen vanishes. Contradicts D-004 ("the page is paper") and the white print renderer. | `note_page_view.cpp::paintEvent` |

### Text and blocks

| # | Defect | Where |
|---|--------|-------|
| T1 | **The Text tool does nothing** except suppress inking. Tapping the page with it does not create text; text only appears via "Add text", always full-width at the bottom of the stack. | `note_page_view.cpp::event`, `note_page_blocks.cpp::addTextBlock` |
| T2 | **No way to delete a block.** There is no command anywhere to remove a text, checklist or image block. | — |
| T3 | **Text, checklist, background and block insertion are not undoable.** History is ink-only. | `note_page_view.cpp` (undo/redo) |
| T4 | **Text widgets are opaque and sit above the ink.** You cannot write over typed text, and ink under a block is hidden. | `note_page_blocks.cpp::rebuildBlockWidgets` |
| T5 | **Fixed 90 px block height.** Overflow shows an inner scrollbar on screen and is clipped in print. | `note_blocks.h::kTextBlockHeight` |
| T6 | **Fonts do not scale with zoom** while the boxes do (D-009 called this intentional; on a pen surface it is a bug — zoom to write small and the typed text stays huge). | `note_page_blocks.cpp::syncBlocksGeometry` |
| T7 | **Plain text only**: no bold, lists, headings. | `rebuildBlockWidgets` (`setAcceptRichText(false)`) |
| T8 | **Checklist:** Enter does not create the next item, Backspace on an empty item does not remove it, no indent, checked items are not struck through. | `note_page_blocks.cpp` |
| T9 | **Images:** cannot be moved, resized, pasted or dropped; full-resolution bytes are stored. | `addImageBlock`, `note_tool_strip.cpp` |
| T10 | **Every keystroke and every stroke rewrites the page JSON to disk** (including base64 images) through `persistCurrentPage`. | `note_window.cpp` (the `pageEdited` connection) |

### Chrome and navigation

| # | Defect | Where |
|---|--------|-------|
| C1 | **Tool strip is a column of text buttons on the left** — no icons ship for `draw-*` names, so every button is as wide as its label. | `note_tool_strip.cpp`, `data/icons` |
| C2 | **Undo/Redo duplicated** on the strip and the main toolbar. | `note_tool_strip.cpp` |
| C3 | **Navigator cannot be hidden**, has no filter, no inline rename (F2 opens a dialog), no drag-and-drop reorder, no move between sections. | `note_window.cpp`, `note_structure.cpp` |
| C4 | **No next/previous page**, no duplicate page, no Ctrl+wheel zoom, no fit-width; zoom is a 5-entry combo. | `note_tool_strip.cpp` |
| C5 | **New pages do not inherit the background** of the page you were on. | `note_structure.cpp::newPage` |
| C6 | **Background and zoom are `QComboBox`es**, "Background…" in the Page menu is a `QInputDialog`. | `note_tool_strip.cpp`, `note_structure.cpp` |
| C7 | **No page size / orientation**; 760 × 1004 is hard-coded. | `note_page_view.h` |
| C8 | **No Preferences page** for the app; pen state is forgotten on restart. | — |
| C9 | **No inline AI actions** although `Ai::InlineActionFlow` exists and QindaWrite/QindaShow wire it. | — |

---

## 3. Target design

### 3.1 Principles

1. **The page is the product.** Chrome is one icon row; the navigator is a
   dock you can close; the page is paper on every theme.
2. **Pen first, typing one tap away.** Ink tools ink everywhere, even over
   typed text. The Text tool taps to type where you tapped.
3. **Direct manipulation.** Anything on the page — strokes, text, checklist
   items, images — can be selected, moved, resized and deleted with the
   Select tool. No dialog for something a drag can do.
4. **One history.** Ink, text, blocks, background and page setup share one
   `QUndoStack`. If it changed the page, Ctrl+Z undoes it.
5. **Nothing modal that a popup can do.** Colours, widths, backgrounds and
   zoom are menu buttons; `QColorDialog` is the "More…" fallback.
6. **Reachable.** Every tool and picker is a named `QAction` in the `&Ink`
   menu with a shortcut, so the global menu and the keyboard reach it.

### 3.2 Window layout

```
┌────────────────────────────────────────────────────────────────────────┐
│ File Edit View Ink Page Format AI Settings Help                        │
├────────────────────────────────────────────────────────────────────────┤
│ [New][Open][Save] | [Undo][Redo] | [Cut][Copy][Paste] | [Find] | [-][+] │  ← shell main toolbar (unchanged)
├────────────────────────────────────────────────────────────────────────┤
│ [Select][Pen][Highlight][Eraser][Text] | [●Color▾][—Width▾] |           │  ← noteToolsToolBar, TOP area,
│ [Add text][Checklist][Image] | [B][I][U][S][•][1.][H] | [Bg▾][100%▾]    │    icon-only, wraps
├────────────┬───────────────────────────────────────────────┬───────────┤
│ Notebook ▾ │                                               │ Assistant │
│ ┌────────┐ │            ┌───────────────────┐              │ (dock,    │
│ │filter… │ │            │                   │              │  hidden   │
│ └────────┘ │            │       page        │              │  by       │
│ ▸ Notes    │            │   (paper white)   │              │  default) │
│   Page 1 ● │            │                   │              │           │
│   Page 2   │            │                   │              │           │
│ ▸ Research │            └───────────────────┘              │           │
│ (dock F9)  │                                               │           │
├────────────┴───────────────────────────────────────────────┴───────────┤
│ Find: [________] Next Prev [ ] Case  ×                                  │  ← unchanged
├────────────────────────────────────────────────────────────────────────┤
│ Opened …                    Notes — page 1 of 2 · Pen 2.5 · Zoom 100 % │
└────────────────────────────────────────────────────────────────────────┘
```

- The tool strip moves to `Qt::TopToolBarArea`. Keep its objectName
  `noteToolsToolBar`. It stays a `WrappingToolBar`.
- The navigator becomes a `QDockWidget` (`noteNavigatorDock`, left area,
  closable, movable) with a filter field above the tree. `View ▸ Show
  Navigator` (`noteShowNavigatorAction`, F9, checkable) toggles it.
- Undo/Redo leave the strip (they are on the main toolbar).
- The status field gains the active tool and its width: `Notes — page 1 of
  2 · Pen 2.5 · Zoom 100 %`.

### 3.3 Tools, pointer rules, keyboard

| Tool | Key | Cursor | Pen / mouse left-drag | Barrel button |
|------|-----|--------|------------------------|---------------|
| Pen | `P` | `Qt::CrossCursor` | inks (pressure) | — |
| Highlighter | `H` | `Qt::CrossCursor` | inks (constant width, no pressure) | — |
| Eraser | `E` | circle cursor, radius × zoom | erases live during the drag | — |
| Text | `T` | `Qt::IBeamCursor` | tap empty page → new text block, editor opens at the tap; tap a block → edit it at the caret | — |
| Select | `V` | `Qt::ArrowCursor` | tap a stroke/block → select; drag on empty → lasso; drag selection → move; handles → resize | — |
| (any) | `Esc` | — | commit the open text editor; a second Esc clears the selection and returns to Pen | — |

Pointer rules, in this order:

1. **Touch is ignored** (existing palm baseline: `WA_AcceptTouchEvents`
   false, `PointerType::Finger` consumed). Keep the existing test green.
2. **Stylus eraser end** → temporary Eraser (exists). **Stylus barrel
   button:** `Qt::MiddleButton` held at press → temporary Eraser;
   `Qt::RightButton` held at press → temporary Select (lasso). Both revert
   on release, like the eraser end does today.
3. **Middle-drag** with the mouse pans the scroll area. `Ctrl+wheel` zooms
   around the cursor; plain wheel scrolls; `Shift+wheel` scrolls
   horizontally.
4. **Ink tools ink over blocks.** Text and image blocks are *painted* by
   the view, not hosted as widgets, so tablet and mouse events always reach
   the page. Only the block being edited is a live widget (see §3.5).
5. Mouse inking stays (fixed pressure 0.6) — laptops without a pen must
   work, and the tests drive that path.

Other shortcuts: `Ctrl+PgDown` / `Ctrl+PgUp` next / previous page (wraps
across sections); `Ctrl+Shift+N` new page (exists); `Ctrl+Shift+D`
duplicate page; `Ctrl+D` duplicate selection; `Delete` deletes the
selection or the selected block; `Ctrl+A` selects all ink and blocks when
no editor is focused (reuses `editSelectAllAction`); `Ctrl+Shift+0` fit
width; `F2` inline rename in the navigator; `F9` navigator.

Single-letter shortcuts are safe with editors: `QTextEdit`/`QLineEdit`
accept `ShortcutOverride` for printable keys, so typing `P` in a block types
`P`. The navigator tree does not, so `P` with the tree focused switches
tools instead of keyboard-searching — acceptable, note it in
`verification.md`.

### 3.4 Ink: rendering, erasing, selection (office_ink)

**Rendering.** Replace segment-by-segment `drawLine` with a filled outline
path built once per stroke:

```cpp
// libs/office_ink/src/stroke_geometry.h
namespace QindaOffice::Ink {
// Midpoint-quadratic smoothed centreline through the samples (no stored
// data changes; smoothing is a render-time property).
[[nodiscard]] QPainterPath centreline(const Stroke &stroke);
// Closed, fillable outline: variable width from widthAt() for Pen, constant
// `stroke.width` for Highlighter; round caps for Pen, flat for Highlighter.
[[nodiscard]] QPainterPath outline(const Stroke &stroke);
}
```

`renderStroke` becomes: `setRenderHint(Antialiasing)`, `fillPath(outline,
color)`; the highlighter fills with alpha ≈ 90/255 as **one** path, so
there are no joint blobs (fixes P2, P3). Pen samples < 2 still return early.

**Render order** (screen *and* print, so they cannot drift): background →
image blocks → text/checklist blocks → **highlighter strokes** → **pen
strokes** → (screen only) selection chrome and the in-progress stroke →
(screen only) the one live editor widget. Highlighter under pen, text under
ink: annotating typed notes is the point.

**Highlighter** is constant width (`stroke.width`, default 12), no pressure
term. Existing highlighter strokes in old notebooks have `width` 6 → they
render 6 wide; acceptable, and no format change.

**Smoothing.** `StrokeBuilder` gains `qreal positionSmoothing = 0.0`
(exponential moving average on the position, applied before the
min-distance test). The view sets it from the Preferences smoothing choice
(Off 0 / Light 0.25 / Strong 0.5).

**Erasing.**

```cpp
// libs/office_ink/src/stroke_edit.h
enum class EraseMode { Stroke, Partial };
// Erases at `pos` among `candidates` (from SpatialGrid::query). Stroke mode
// removes whole strokes; Partial removes samples within `radius` and splits
// a stroke where a gap opens (a fragment with < 2 samples is dropped).
// Returns true when anything changed. Indices are invalidated on true.
bool eraseAt(StrokeLayer *layer, const QSet<int> &candidates, const QPointF &pos,
             qreal radius, EraseMode mode);
```

The view applies `eraseAt` on **every** press/move sample (fixes P4), rebuilds
the grid once per event when something changed, repaints only the touched
rect, and pushes **one** undo command per gesture — snapshot taken at press,
committed at release only if anything changed (fixes P5).

**Selection maths** (M24):

```cpp
// stroke_edit.h (continued)
[[nodiscard]] QVector<int> strokesInPolygon(const StrokeLayer &layer, const QPolygonF &lasso); // ≥50 % of samples inside
[[nodiscard]] int strokeAtIndex(const StrokeLayer &layer, const QSet<int> &candidates, const QPointF &pos, qreal tolerance);
[[nodiscard]] QRectF boundsOf(const StrokeLayer &layer, const QVector<int> &indices);
void translate(StrokeLayer *layer, const QVector<int> &indices, const QPointF &delta);
void scaleAbout(StrokeLayer *layer, const QVector<int> &indices, const QPointF &origin, qreal sx, qreal sy); // widths scale by sqrt(sx*sy)
void recolour(StrokeLayer *layer, const QVector<int> &indices, const QColor &color);
void rewidth(StrokeLayer *layer, const QVector<int> &indices, qreal width);
void removeIndices(StrokeLayer *layer, QVector<int> indices);
[[nodiscard]] StrokeLayer extract(const StrokeLayer &layer, const QVector<int> &indices); // copies, normalised to bounds().topLeft()
void insertStrokes(StrokeLayer *layer, const StrokeLayer &strokes, const QPointF &at);
```

All pure model, tested in a new `tests/office_ink/tst_stroke_edit.cpp`
registered in `tests/office_ink/CMakeLists.txt` beside the existing
`tst_office_ink` (same `add_executable` + `add_test` shape).

### 3.5 Blocks: free-floating, painted, one live editor

**Data.** Every block gains a `rect` in page units. Text and checklist
blocks store `[x, y, w]` — height is always *computed* from the content.
Images store `[x, y, w, h]`. Text blocks gain `html` next to the existing
`text` (which stays the plain-text mirror used by Find, Replace and the AI
context). Checklist items gain `level` (0–3).

```json
{"blocks":[
  {"type":"text",  "text":"Agenda\nBudget",        "html":"<h2>Agenda</h2><p>Budget</p>", "rect":[40,40,680]},
  {"type":"check", "text":"call Sam", "checked":true, "level":1,                          "rect":[40,140,680]},
  {"type":"image", "data":"<base64>", "mime":"image/jpeg",                                 "rect":[40,180,400,300]}
]}
```

A block **without** `rect` (every M21 notebook) is laid out by the current
stacking rule — `layoutBlocks` assigns it a rect exactly where it sits
today. Rects are written back the next time the page is persisted after a
user edit, never on load (so opening a notebook does not dirty it).
`layoutBlocks` stays the single place geometry is decided (D-022 holds).

**Height** of a text/checklist block = `QTextDocument` height at the block
width with the page font. Put the measuring helper in `note_blocks.cpp`
(QtGui only, both the view and the renderer link it):

```cpp
// note_blocks.h
struct PageFont { QString family; qreal pointSize = 11; }; // from Preferences documentFont*, else app default family at 11 pt
[[nodiscard]] PageFont pageFont();
[[nodiscard]] qreal textBlockHeight(const PageBlock &block, const PageFont &font);   // lays out html (or text) at block.rect.width()
[[nodiscard]] QList<PageBlock> layoutBlocks(const QByteArray &blocksJson, qreal pageWidth); // now fills rect for every block, computing heights
[[nodiscard]] QByteArray writeBlocks(const QList<PageBlock> &blocks);             // inverse; keeps unknown block types verbatim by index
```

**Painting.** Blocks are painted by `renderNotePage` **and** by the view's
`paintEvent` through one shared function `paintBlock(QPainter*, const
PageBlock&, const PageFont&)` in `note_page_render.cpp`, using
`QTextDocument::drawContents`. Checklist: painted box + tick, text
struck-through and drawn with 50 % alpha when checked, indented by
`level × 24`. Images: `drawImage` into the rect. Screen painting scales the
painter by zoom first, so **fonts scale with zoom** (supersedes the D-009
clause; record in the M23 decision).

**One live editor.** The view owns at most one editor widget at a time:

```cpp
// note_page_editor.h  (new, apps/qindanote/src)
class NoteBlockEditor final : public QObject {   // hosts ONE QTextEdit (text) or QLineEdit (check) over a block
public:
    void open(int blockIndex, const QPointF &caretPagePos);   // creates the widget, paper palette, NoFrame, transparent viewport, font × zoom
    void commit();   // writes html+text (or text) back through writeBlocks → BlocksCommand; removes an EMPTY text block; deletes the widget
    [[nodiscard]] bool isOpen() const; [[nodiscard]] int blockIndex() const;
    void syncGeometry(); // page rect × zoom; height follows the document as you type (the block below does not move — blocks are free-floating)
signals: void committed(); void nextCheckItemRequested(int afterIndex, int level); void removeEmptyCheckItemRequested(int index);
};
```

Behaviour:
- Text tool tap on empty page → `appendBlock` with `rect = [x, y, min(360, pageW − x − 40)]`, then `open()`.
  "Add text" on the strip still appends a full-width block at the bottom of
  the lowest block (today's placement) and opens it — discoverable path.
- Tap outside the editor, `Esc`, switching tool, switching page, save,
  print, export, close → `commit()`. An empty text block is removed on
  commit (no litter).
- Checklist editor (`QLineEdit`): `Enter` → commit and insert a new check
  item directly below with the same `level`, editor moves to it;
  `Backspace` on an empty item → remove it and move the editor to the item
  above; `Tab` / `Shift+Tab` → `level ± 1` (0–3). Clicking the painted box
  toggles `checked` (hit-test in the view, no editor needed).
- The editor's `QTextEdit` has **its own undo disabled**
  (`setUndoRedoEnabled(false)`); `Ctrl+Z` inside it reaches the page
  history, whose typing command merges bursts (see §3.6).
- Formatting (`Format` menu + strip): Bold, Italic, Underline, Strikethrough,
  Bullet list, Numbered list, Heading (cycles body → h2 → h1 → body) and
  Text colour, all applied to the open editor's cursor and enabled only
  while an editor is open (D-011: enablement follows the host). Reuse the
  shell ids `ActionId::kFormatBold/Italic/Underline/StrikeOut/ListBullet/ListNumbered/TextColor`;
  the heading cycle is `noteFormatHeadingAction`.
- Paste with an editor open → into the editor (rich paste allowed; the
  stored `html` is what `QTextEdit::toHtml()` gives, `text` is
  `toPlainText()`). Paste with no editor open → image data → image block;
  ink MIME → strokes (M24); text → new text block at the last pointer
  position or the next free spot.
- Drag-and-drop of image files onto the page → image block at the drop
  point (the view accepts `image/*` and `.png/.jpg/.jpeg/.webp/.bmp/.gif`
  URLs and consumes them; every other drop propagates to
  `DocumentWindow::dropEvent`, which opens notebooks).

**Images.** On insert, downscale to ≤ 2048 px on the long edge and re-encode
(JPEG q85 when the source has no alpha, PNG otherwise); store `mime`. The
default rect is `[40, nextFreeY, min(imgW, pageW − 80), h]` keeping aspect.
Select tool: move, corner handles resize with aspect locked.

**Find / Replace** keep reading `text`. Replace must rewrite `html` too:
load into a `QTextDocument`, `find`/replace with a `QTextCursor` loop, then
write both `html` and `text` back. Extend `replaceInPage` accordingly.

### 3.6 One history

Replace the two `QVector<StrokeLayer>` stacks with a `QUndoStack` owned by
the view (`note_page_history.{h,cpp}`):

| Command | Payload | `mergeWith` |
|---------|---------|-------------|
| `InkCommand` | old/new `StrokeLayer` snapshot (what the stacks hold today; bounded at 100 entries via `setUndoLimit`) | no |
| `BlocksCommand` | old/new `blocksJson` + block index + caret position | consecutive typing on the same block within 1 s merges into one step |
| `BackgroundCommand` | old/new `background`, `backgroundSpacing` | no |
| `PageSetupCommand` | old/new `pageSize` | no |

Text: `"Undo Stroke"`, `"Undo Erase"`, `"Undo Typing"`, `"Undo Delete
Block"`, `"Undo Move"`, … (the shell shows the action text; keep the
`editUndo`/`editRedo` actions bound through `canUndoChanged`/`canRedoChanged`
as they are through `undoAvailable`/`redoAvailable` today). `setPage()`
clears the stack. Structural operations (delete page/section) stay outside
the stack and keep their confirmation dialogs (D-017).

### 3.7 Menus and action ids

New `&Ink` menu (inserted before `&Page`, objectName `noteInkMenu`) and new
`F&ormat` menu (after `&Page`, objectName `noteFormatMenu`). Existing ids
are marked *(exists)* and must not change.

| Menu | Text | objectName | Shortcut |
|------|------|------------|----------|
| Ink | Select | `noteSelectTool` | V |
| Ink | Pen | `notePenTool` *(exists)* | P |
| Ink | Highlighter | `noteHighlighterTool` *(exists)* | H |
| Ink | Eraser | `noteEraserTool` *(exists)* | E |
| Ink | Text | `noteTextTool` *(exists)* | T |
| Ink ▸ Pen Colour | Black / Grey / Blue / Red / Green / Orange / Purple / Brown | `notePenColor_black` … `notePenColor_brown` | — |
| Ink ▸ Pen Colour | Custom… | `notePenColorAction` *(exists; today's QColorDialog)* | — |
| Ink ▸ Pen Width | Fine 1.5 / Medium 2.5 / Thick 4 / Bold 6 / Marker 10 | `notePenWidth_fine` … `notePenWidth_marker` | — |
| Ink ▸ Highlighter Colour | Yellow / Green / Pink / Blue / Orange | `noteHighlighterColor_yellow` … | — |
| Ink ▸ Highlighter Width | Thin 8 / Medium 12 / Wide 18 / Broad 24 | `noteHighlighterWidth_thin` … | — |
| Ink ▸ Eraser | Erase Whole Strokes / Erase Partially | `noteEraserMode_stroke`, `noteEraserMode_partial` | — |
| Ink ▸ Eraser | Small 6 / Medium 12 / Large 24 / Huge 40 | `noteEraserSize_small` … | — |
| Ink | Delete Selection | `noteDeleteSelectionAction` | Delete |
| Ink | Duplicate Selection | `noteDuplicateSelectionAction` | Ctrl+D |
| Page | New Page / New Section / Rename… / Move Up / Down / Delete… | *(exist)* | *(exist)* |
| Page | Duplicate Page | `notePageDuplicate` | Ctrl+Shift+D |
| Page | Next Page / Previous Page | `notePageNext`, `notePagePrevious` | Ctrl+PgDown / Ctrl+PgUp |
| Page ▸ Background | Blank / Ruled / Grid / Dotted | `notePageBackground_blank` … (checkable, exclusive) | — |
| Page ▸ Background | Narrow / Medium / Wide spacing | `notePageBackgroundSpacing_narrow` … | — |
| Page | Page Setup… | `notePageSetupAction` | — |
| Page | Export PDF… | `noteExportPdf` *(exists)* | — |
| Page | Add Text / Add Checklist Item / Add Image… | `noteAddTextAction`, `noteAddCheckAction`, `noteAddImageAction` *(exist)* | — |
| Page | Delete Block | `noteDeleteBlockAction` | Delete (when a block is selected) |
| Format | Bold / Italic / Underline / Strikethrough / Bullets / Numbered / Text Colour | `ActionId::kFormat*` | Ctrl+B / I / U … |
| Format | Heading (cycle) | `noteFormatHeadingAction` | Ctrl+Alt+H |
| View | Show Navigator | `noteShowNavigatorAction` | F9 |
| View | Fit Width | `noteViewFitWidthAction` | Ctrl+Shift+0 |
| View ▸ Zoom | 50 / 75 / 100 / 150 / 200 / 300 % | `noteZoom_50` … (checkable) | — |

Remove the old `Page ▸ Background…` `QInputDialog` action (`notePageBackground`)
**only** by turning it into the submenu's parent; keep the id on the
submenu's `menuAction()` so nothing addressed by that name disappears.

The strip's `[Color ▾]` and `[Width ▾]` buttons are `QToolButton`s with
`InstantPopup` menus that hold **the same `QAction`s** as the `Ink` menu;
they show the colour/width of the *active* tool: Pen → pen menus,
Highlighter → highlighter menus, Eraser → the eraser-size menu (colour
button disabled). Their icons are painted pixmaps (a filled circle of the
current colour; a line of the current width) — document state, not chrome;
add an `AGENT-NOTE` saying so. `[Bg ▾]` and `[100 % ▾]` are the same kind
of button over the Page ▸ Background and View ▸ Zoom actions; the zoom
button's text shows the current percentage.

### 3.8 Icons to ship

Add to `data/icons/hicolor/scalable/actions/`, in the same neutral mid-tone
single-path style as the existing 24 (open `edit-duplicate.svg` for the
template; verify at 16 px on light and dark toolbars):

`draw-freehand` (Pen), `draw-highlight` (Highlighter), `draw-eraser`,
`edit-select` (Select, arrow), `insert-text` *(exists)*, `checkbox`
(Checklist), `insert-image` *(exists)*, `view-sidetree` (navigator),
`zoom-fit-width`, `go-next-view-page`, `go-previous-view-page`,
`format-text-color` *(exists)*, `format-text-bold/italic/underline/strikethrough`
*(exist)*, `format-list-ordered/unordered` *(exist)*, `format-text-larger`
(Heading), `draw-rectangle`, `draw-ellipse`, `draw-line`, `draw-arrow`
(M26). Update the packaging install list if it enumerates files.

### 3.9 Preferences and remembered state

`addPreferencePages()` → a **Notes** page with exactly these keys, each
read by the code named (D-014: a key nothing consumes is a defect):

| Key | Control | Read by |
|-----|---------|---------|
| `note/pressure` (bool, default true) | "Pen pressure changes width" | view: when false every sample's pressure is 0.5 |
| `note/smoothing` (off/light/strong, default light) | combo | `StrokeBuilder::positionSmoothing` |
| `note/defaultBackground` (blank/ruled/grid/dotted) | combo | `NoteDocument()` seed page and `Notebook::create()` seed |
| `note/defaultPageSize` (screen/a4/letter) | combo | same |

Per-tool *state* (last tool, pen colour/width, highlighter colour/width,
eraser mode/size, navigator visible, zoom mode) is not a preference and
does not go in the dialog. Store it as `tools.json` under
`Core::stateDirFor(Core::appLeaf())` through `Core::JsonSettings`, written
on change and read at window construction (rule 7: bounded, per app,
degrade visibly). Keep the four `Preferences` accessors in
`libs/office_shell/src/preferences.{h,cpp}` next to `spellCheckEnabled`.

---

## 4. Milestones

Each milestone lists: scope, files, steps, tests, docs, gates. "Files" marks
new files with `+`, split files with `⇢`.

### M22 — Pen & paper (rendering, eraser, strip, keyboard, perf)

**Scope.** Fix P1–P10, P12, C1, C2, C6 (strip part), T10. After this
milestone QindaNote is a good pen app; nothing about text changes yet.

**Files.**
- `+ libs/office_ink/src/stroke_geometry.{h,cpp}` — `centreline`, `outline`.
- `+ libs/office_ink/src/stroke_edit.{h,cpp}` — `EraseMode`, `eraseAt` (selection ops come in M24).
- `libs/office_ink/src/ink_persistence.cpp` — `renderStroke` fills the outline, antialiased; add `renderStrokes(QPainter*, const QVector<Stroke>&, const QSet<int>* visible = nullptr)` that draws highlighters first, then pens.
- `libs/office_ink/src/stroke_builder.{h,cpp}` — `positionSmoothing`.
- `libs/office_ink/src/spatial_grid.{h,cpp}` — add `void remove(int index)`/`insert` only if you need incremental updates; otherwise keep `rebuild`.
- `libs/office_ink/CMakeLists.txt`, `+ tests/office_ink/tst_stroke_edit.cpp` (+ CMake).
- `apps/qindanote/src/note_page_view.{h,cpp}` ⇢ split input handling into `+ note_page_input.cpp` (tablet, mouse, wheel, keys, cursors, barrel buttons, pan) and keep paint/metrics in `note_page_view.cpp`.
- `+ apps/qindanote/src/note_page_history.{h,cpp}` — `QUndoStack` + `InkCommand` (only this command in M22; behaviour identical to today's stacks).
- `apps/qindanote/src/note_tool_strip.cpp` ⇢ strip layout; `+ note_ink_actions.{h,cpp}` — `NoteInkActions` owning the tool/colour/width/eraser `QAction`s, the `Ink` menu population, the popup buttons, and `tools.json` persistence.
- `apps/qindanote/src/note_window.{h,cpp}` — strip to the top, `Ink` menu insertion, debounced persist, status text.
- `apps/qindanote/src/note_page_render.cpp` — antialiasing, `renderStrokes` order.
- `data/icons/hicolor/scalable/actions/{draw-freehand,draw-highlight,draw-eraser,edit-select,checkbox}.svg`.
- `libs/office_shell/src/preferences.{h,cpp}`, `+ apps/qindanote/src/note_preferences_page.{h,cpp}` (pressure + smoothing now; the two defaults in M25).

**Steps.**
1. `stroke_geometry`: implement `centreline` (midpoint quadratics; a
   2-sample stroke is a straight line) and `outline` (walk the centreline
   at ≤ 2 page-unit steps, offset by ±width/2 along the normal; round caps
   for Pen via `arcTo`, flat for Highlighter). Unit-test that the outline
   of a horizontal 2-sample pen stroke of width 4 at pressure 0.5 has
   bounds height ≈ 4 and that a highlighter outline ignores pressure.
2. `renderStroke` → fill path; `renderStrokes` → two passes. Set
   `Antialiasing` inside `renderStroke` so every caller gets it.
3. `NotePageView::paintEvent`: pin the page to paper — `fillRect(page,
   Qt::white)`, ruled/grid/dot lines in the same grey the renderer uses
   (`0xc8c8d2`), page border `palette().dark()` is fine (that is chrome
   around paper). Add an `AGENT-NOTE` citing D-004. Draw ink through
   `renderStrokes` with the grid's visible set.
4. `eraseAt` in office_ink + live erasing in `note_page_input.cpp`: on
   press take the snapshot, on every sample `eraseAt` with `m_grid.query`
   candidates, rebuild the grid when it returns true, `update()` the
   union of the erased strokes' bounds (zoomed); on release push the
   `InkCommand` only if `changed`. Eraser cursor: a `QCursor` from a
   painted ring pixmap of `radius × zoom × 2` px, regenerated on zoom or
   size change.
5. Narrow repaint during inking: on `TabletMove`/`MouseMove` call
   `update(segmentRect)` = bounds of the last two samples ± `width`,
   mapped to widget space. Never full `update()` while inking.
6. Tool cursors (`setCursor` on tool change), barrel buttons (temporary
   tool swap with the same restore pattern as `m_toolBeforeEraser`, which
   becomes `m_toolBeforeTemporary`), middle-drag pan (scroll the
   `QScrollArea` parent found via `parentWidget()` chain; do not couple
   the view to `NoteWindow`), `Ctrl+wheel` zoom about the cursor
   (`setZoom` then adjust the scroll bars so the page point under the
   cursor stays put).
7. `NoteInkActions`: create every `Ink` action from §3.7 with the exact
   ids, exclusive `QActionGroup`s for colours/widths/modes, shortcuts
   P/H/E/T/V/Esc; the popup `QToolButton`s; icon painting; `tools.json`
   read at construction and written on change. The view exposes
   `setHighlighterColor/Width`, `setEraserMode/Size`; colours and widths
   are applied to the *active* tool by `NoteInkActions`, not by the view.
8. Strip layout (§3.2) at the top area; remove Undo/Redo and the spinbox
   and the two combos from the strip (the zoom/background menu buttons
   arrive with their actions in M25 — until then keep the two combos but
   move them to the end of the strip).
9. `note_page_history`: `QUndoStack` with `InkCommand`; wire
   `editUndo`/`editRedo` to `undo()`/`redo()` and enablement to
   `canUndoChanged`/`canRedoChanged`; keep the `undoAvailable`/`redoAvailable`
   signals emitting (tests use them). `setUndoLimit(100)`.
10. Debounce `persistCurrentPage`: a 500 ms single-shot `QTimer` restarted
    on `pageEdited`; flush synchronously before page switch, save, print,
    export, close and in `NoteWindow::~NoteWindow`. The recovery journal
    already covers the window in between.
11. Status text: `"%1 — page %2 of %3 · %4 %5 · Zoom %6 %"` with the tool
    name and width.
12. Preferences: `pressure` + `smoothing` accessors, the Notes page, the
    view reading both in `applyPreferences()`.

**Tests** (`tests/qindanote/tst_qindanote.cpp`, or split a new
`tst_note_input.cpp` if the file passes 500 lines):
- `eraserErasesDuringTheDrag` — press with the eraser over a stroke, move; the stroke is gone **before** release.
- `eraserGestureIsOneUndoStepAndNoneWhenNothingErased` — undo depth after an empty erase is unchanged; after erasing two strokes in one drag a single undo restores both.
- `partialEraserSplitsAStroke` (office_ink) — a 10-sample line erased in the middle becomes two strokes.
- `highlighterIsConstantWidthAndDrawnUnderPen` — render to a `QImage`, sample a pixel where a pen stroke crosses a highlighter: it is the pen colour.
- `pageIsPaperUnderADarkPalette` — set a dark `QPalette` on the view, grab, assert the page centre pixel is white.
- `toolShortcutsSwitchTools` — `QTest::keyClick(&window, Qt::Key_H)` → `pageView()->tool() == Highlighter`; with a `QLineEdit` (find bar) focused the key types instead.
- `barrelButtonIsATemporaryEraser` — tablet press with `Qt::MiddleButton` erases and restores the Pen on release.
- `inkingRepaintsOnlyTheSegmentRect` — install an event filter counting `QPaintEvent` region areas during a 20-sample stroke; the largest region is far smaller than the page rect.
- `persistIsDebounced` — three quick edits on an open (saved) notebook write the page file once (compare `QFileInfo::lastModified` / a byte-revision counter).
- `stripIsIconOnlyAndWrapsAtAThirdWidth` (new `tst_note_ui.cpp`, same shape as `tst_write_ui`'s squeeze test) — every strip action has a visible button inside `strip()`.
- Keep: all sixteen existing tests.

**Docs.** `docs/index.md` row; `docs/verification.md` "M22 pen gates"
(manual: tablet pressure looks right, no lag at 5 000 strokes, palm on
screen does nothing, dark theme page is white); `docs/decisions.md`
**D-026: Ink is filled geometry, the eraser is live, and pen state is
window-independent** (why outline paths, why highlighter under pen, why
tool state is state not preference, why the strip moved to the top).

**Gate.** `ctest -R 'note|ink'` green; strip squeeze test green; manual
tablet check recorded.

### M23 — Text & blocks (tap to type, painted blocks, one editor, one history)

**Scope.** Fix T1–T9, T3 fully (unified history). Depends on M22.

**Files.**
- `apps/qindanote/src/note_blocks.{h,cpp}` — `rect`, `html`, `level`, `mime`, `PageFont`, `textBlockHeight`, `writeBlocks`, legacy stacking for rect-less blocks.
- `apps/qindanote/src/note_page_render.cpp` — `paintBlock` shared painter; render order per §3.4.
- `apps/qindanote/src/note_page_blocks.cpp` ⇢ becomes block hit-testing, painting calls, `appendBlock`/`removeBlock`/`moveBlock`/`resizeBlock`, checklist toggle; `+ note_page_editor.{h,cpp}` (`NoteBlockEditor`); `+ note_page_clipboard.cpp` (paste/drop of text and images; ink MIME added in M24).
- `apps/qindanote/src/note_page_history.{h,cpp}` — `BlocksCommand` (merging), `BackgroundCommand`.
- `+ apps/qindanote/src/note_format_actions.{h,cpp}` — Format menu/strip actions bound to the open editor.
- `apps/qindanote/src/note_window_ui.cpp` — `replaceInPage` rewrites `html`; `NoteContext`/`NoteApplier` use the editor (insert with no editor open → new block).
- `apps/qindanote/src/note_tool_strip.cpp` — format buttons; "Add text"/"Checklist"/"Image" stay.
- Tests: `tests/qindanote/tst_note_blocks.cpp` (extend), `+ tst_note_editor.cpp`.

**Steps.**
1. Data + layout first (`note_blocks`), with tests for legacy stacking,
   rect round-trip, height from content, and unknown-type preservation.
2. `paintBlock` + render order; the view stops creating a widget per block
   (`rebuildBlockWidgets` goes away). Keep `layoutBlocks` as the only
   geometry source.
3. `NoteBlockEditor` (§3.5). Paper palette on the widget (same pin as
   `PageTextView`, cite D-004), `Qt::WA_NoSystemBackground` off — the
   editor *is* opaque white while open, that is fine; only painted blocks
   are transparent to ink.
4. Text-tool tap → new block / open existing; "Add text" → append + open;
   checklist Enter/Backspace/Tab; box toggle hit-test; empty-block removal.
5. `BlocksCommand` with `mergeWith` for typing; every block mutation
   (`append/remove/move/resize/toggle/level`) goes through it. Background
   changes go through `BackgroundCommand`. Undo text per command.
6. Format actions (§3.5); enable only while an editor is open; strip and
   menu share the actions.
7. Paste/drop of images and text; image downscale + `mime`.
8. Fonts × zoom on screen; the print path is unchanged because it paints
   the same blocks at zoom 1.
9. `replaceInPage` html-aware; Find unchanged; AI applier via the editor.
10. Remove `kTextBlockHeight`/`kCheckBlockHeight` once nothing reads them.

**Tests.**
- `legacyBlocksWithoutRectKeepTheirStackedPositions` — an M21 `blocksJson` lays out at exactly the old y positions and heights of a 90/28 stack are replaced by content heights ≥ one line.
- `textToolTapCreatesABlockAtTheTapAndOpensTheEditor`.
- `emptyTextBlockIsRemovedOnCommit`.
- `typingIsOneUndoStepPerBurst` — type "abc", wait > 1 s, type "d"; two undos restore "" then… assert depth is 2 after, 1 after the first undo.
- `deletingABlockIsUndoable`; `movingABlockIsUndoable`; `backgroundChangeIsUndoable`.
- `checklistEnterInsertsTheNextItemBelow`; `backspaceOnEmptyItemRemovesIt`; `tabIndentsAnItem`; `clickingTheBoxTogglesWithoutAnEditor`.
- `inkOverATextBlockInks` — pen press/move/release over a painted text block adds a stroke (the block is not a widget).
- `printPaintsTheSameBlockRectsAsTheScreen` — compare `layoutBlocks` output with what `paintBlock` receives (spy via a test hook or by rendering both and diffing the block regions).
- `replaceRewritesHtmlAndText`.
- `pastedImageIsDownscaledAndCarriesMime`.
- `fontsScaleWithZoom` — editor font point size at zoom 2 is twice zoom 1.

**Docs.** `index.md`; `verification.md` "M23 text gates"; `decisions.md`
**D-027: Blocks are free-floating, painted under the ink, edited one at a
time, and share one history with the ink** (also records the D-009
supersession on fonts and the `rect`/`html` additions to D-022).

**Gate.** All note tests green; open an M21 notebook and confirm the
blocks are where they were; type, ink over it, print preview matches.

### M24 — Select tool & ink editing

**Scope.** Fix P11. Depends on M22 and M23.

**Files.**
- `libs/office_ink/src/stroke_edit.{h,cpp}` — the selection ops from §3.4; `+ tests/office_ink/tst_stroke_edit.cpp` extended.
- `+ apps/qindanote/src/note_page_selection.{h,cpp}` — `NoteSelection` (strokes *or* one block), lasso capture, handles, hit-testing, painting of the selection chrome (dashed bounds + 4 corner handles, stock `QPainter`).
- `apps/qindanote/src/note_page_input.cpp` — Select tool branches.
- `apps/qindanote/src/note_page_clipboard.cpp` — `application/x-qinda-ink+json` + `image/png` render on copy; paste at the last pointer position (+20 units if it would land exactly on the source).
- `apps/qindanote/src/note_ink_actions.cpp` — Delete/Duplicate Selection, `editSelectAll` when no editor is open, colour/width pickers apply to a stroke selection when one exists.

**Steps.**
1. office_ink ops + tests (polygon selection majority rule; translate;
   scale about origin with width scaling; extract/insert normalisation).
2. `NoteSelection`: model of what is selected + geometry helpers; the view
   paints it after the ink.
3. Input: tap → `strokeAtIndex`/block hit; drag on empty → lasso polygon
   (painted live); drag inside bounds → move (one `InkCommand` or
   `BlocksCommand` at release); corner handle → scale (aspect locked for
   images; free for strokes; width only for text); `Delete`; `Ctrl+D`.
4. Clipboard: cut/copy/paste of strokes between pages and windows; the
   shell's `editCut/Copy/Paste` follow the selection when no editor is
   focused (extend `wireEditActions` — today they follow a focused
   `QTextEdit` only).
5. Pickers apply to the selection: choosing a colour with strokes selected
   recolours them (`InkCommand`) instead of changing the pen.

**Tests.** `lassoSelectsStrokesMostlyInside`, `movingASelectionIsOneUndoStep`,
`scalingScalesWidths`, `deleteRemovesSelectedStrokes`, `copyPasteInkBetweenPages`
(two `NotePage`s, one view), `recolourAppliesToSelectionNotThePen`,
`escClearsSelectionThenReturnsToPen`, `selectAllWithNoEditorSelectsInkAndBlocks`.

**Docs.** `index.md`; `verification.md`; `decisions.md` **D-028: A
selection is strokes or one block; ink has a clipboard format.**

### M25 — Navigator, navigation, page setup, preferences

**Scope.** Fix C3–C5, C6 (menu part), C7, C8.

**Files.**
- `+ apps/qindanote/src/note_navigator.{h,cpp}` — `NoteNavigator` (`QWidget` with the filter `QLineEdit` + `QTreeWidget`; inline rename; drag-and-drop; emits `pageActivated(sectionId, pageId)`, `renameRequested`, `moveRequested`). `note_window.cpp` loses `rebuildNavigator`/`selectPage` internals to it; the window keeps the id ownership contract (AGENT-GUARD there stays true).
- `apps/qindanote/src/notebook.{h,cpp}` — `movePageTo(sectionId, pageId, targetSectionId, index)`, `moveSection(id, index)`, `duplicatePage(sectionId, pageId)` (copies the envelope, new id, title "… (copy)"), per-page `pageSize` in the envelope (office_ink `PageEnvelope` gains `QSizeF pageSize` default 760×1004 and `int backgroundSpacing` default 28; persisted only when non-default so M21 files are byte-identical on a no-op save).
- `apps/qindanote/src/note_structure.cpp` — next/previous/duplicate page, background submenu + spacing, `Page Setup…` (`+ note_page_setup_dialog.{h,cpp}`: size combo Screen/A4/Letter/Custom + orientation; `PageSetupCommand`), new page inherits background/spacing/size from the current page.
- `apps/qindanote/src/note_page_view.cpp` — `m_pageWidth`/`m_minPageHeight` come from the page; `fitWidth()`; zoom actions 50–300 checkable; print callback uses each page's own size.
- `apps/qindanote/src/note_tool_strip.cpp` — `[Bg ▾]` `[100 % ▾]` menu buttons replace the two combos.
- `apps/qindanote/src/note_preferences_page.cpp` — default background + default page size.
- Icons: `view-sidetree`, `zoom-fit-width`, `go-next-view-page`, `go-previous-view-page`.

**Steps.**
1. `NoteNavigator`: `setEditTriggers(EditKeyPressed | SelectedClicked)`,
   items editable, `itemChanged` → rename through the notebook (block
   signals during rebuild — keep D-017 §3: select *outside* the blocker);
   `setDragDropMode(InternalMove)` + `dropEvent` override that reads the
   drop indicator, computes `(sectionId, pageId, targetSectionId, index)`
   and calls `movePageTo`, refusing a page dropped at top level and a
   section dropped inside a section (revert by rebuilding); filter field
   hides non-matching pages (and sections with no visible page); current
   page bold.
2. Dock: `QDockWidget` `noteNavigatorDock`, `View ▸ Show Navigator` F9
   checkable, visibility remembered in `tools.json`.
3. Page navigation actions; `Duplicate Page`; new-page inheritance.
4. `PageEnvelope::pageSize`/`backgroundSpacing` + persistence + view +
   renderer + print (per-page size); `Page Setup…`; background submenu.
5. Fit width: zoom = `(viewport width − 48) / pageWidth`, re-applied on
   viewport resize while the mode is "fit"; `Ctrl+Shift+0`; default zoom
   mode for a new window is fit-width unless `Preferences::defaultZoomPercent`
   is not 100 (then that wins — do not silently ignore a preference).
6. Preferences defaults for new notebooks.

**Tests.** `inlineRenameWritesThroughToTheNotebook`, `dragDropMovesAPageBetweenSections`
(drive `movePageTo` + a synthetic drop if `QTest` drag is unreliable
offscreen — test the model op and the `dropEvent` mapping separately),
`filterHidesNonMatchingPages`, `nextPageWrapsAcrossSections`,
`duplicatePageCopiesInkAndBlocksWithANewId`, `newPageInheritsBackgroundAndSize`,
`pageSizeRoundTripsAndOldFilesStayByteIdentical` (save an M21 fixture
untouched → identical bytes), `fitWidthTracksTheViewport`,
`navigatorToggleIsRemembered`.

**Docs.** `index.md`; `verification.md` (drag-drop, dock, page setup, print
of mixed sizes); `decisions.md` **D-029: The navigator is a dock and a
drop target; pages carry their own size.**

### M26 — Shapes, templates, inline AI, thumbnails (polish)

**Scope.** C9 and the "powerful" extras. Each item is independent; do them
in this order and stop where told.

1. **Shapes as strokes.** A Shape tool (`noteShapeTool`, key `S`) with
   variants Line / Arrow / Rectangle / Ellipse (`noteShape_line` …) that
   rasterise to ordinary `Stroke`s (rectangle 5 samples, ellipse 64,
   arrow = shaft + two head strokes) so the renderer, eraser, selection,
   undo and persistence need **no** changes. Also "hold to straighten":
   a pen stroke whose end is held still for 600 ms with ≤ 3 units of
   movement snaps to a line/rect/ellipse if the fit error is < 6 % of its
   bounds (`Ink::fitShape(const Stroke&) → std::optional<Stroke>` in
   office_ink, tested). Icons `draw-line/arrow/rectangle/ellipse`.
2. **Templates.** `data/templates/qindanote/{meeting,cornell,daily,weekly}.json`
   — page envelopes (blocks + background) — and `File ▸ New from
   Template…` via `insertFileMenuActions` (the same hook the other apps
   use; insert **before** `fileMenuAnchor()`). Install rule in CMake.
3. **Inline AI.** Wire `Ai::InlineActionFlow` like `write_window.cpp`
   (the `for (const Ai::InlineAction &action : Ai::inlineActions())` loop,
   ids `noteAi_<id>`) over the open editor's selection, plus two
   note-specific actions in the AI menu: `noteAiSummarisePage` (page text
   → new text block at the bottom) and `noteAiToChecklist` (selection →
   checklist items). Nothing is sent without the explicit click (existing
   privacy contract).
4. **Thumbnails.** Optional. 48 px-high page previews in the navigator
   rendered through `renderNotePage` into a per-page `QPixmap` cache,
   invalidated on `pageEdited`; a lazy page shows a blank paper glyph
   until loaded. Only if the navigator stays responsive with 200 pages
   (render on a `QTimer` after idle, one page per tick).

**Docs.** `index.md`; `decisions.md` **D-030: Shapes are strokes.**

---

## 5. File-format compatibility rules

- **Additive only.** New keys: `blocks[].rect`, `blocks[].html`,
  `blocks[].level`, `blocks[].mime`, page `pageSize`, page
  `backgroundSpacing`. No key is renamed; `StrokeTool` enum values do not
  change; `tool` ints stay 0/1/2.
- **Old files load unchanged.** Add `tests/fixtures/notes/m21/` with a real
  notebook saved by the M21 build (two sections, ink, text, checklist,
  image). `tst_qindanote::m21NotebookLoadsAndLaysOutWhereItDid` loads it
  and asserts block positions, stroke count and background.
- **No-op save is byte-identical** for an M21 page file (defaults are not
  written): covered by `pageSizeRoundTripsAndOldFilesStayByteIdentical`.
- **New files degrade gracefully in M21**: it ignores `rect`/`html`
  (stacks the blocks, shows plain `text`) and unknown page keys. Do not
  break that — always write `text` alongside `html`.
- `notebook.json` gains `"format": 2` for humans and tools; nothing reads
  it to refuse.

## 6. Out of scope (do not build)

- Tabs, MDI, a document switcher, continuous multi-page scrolling
  (AGENTS.md rule 1).
- Handwriting recognition or searching ink.
- Touch gestures (pinch/two-finger scroll): touch stays ignored on the
  page; revisit only with a desktop-level decision.
- A custom-drawn toolbar, floating "context" toolbars, or any styling
  beyond stock widgets.
- Undo for page/section deletion (D-017 keeps the confirmation dialog).
- Cloud sync, sharing, collaboration.

## 7. Definition of done (whole plan)

- Every defect in §2 is either fixed and covered by a named test, or
  listed in the final commit body as deliberately deferred with a reason.
- `ctest --preset dev` green; every new file ≤ 500 non-blank lines with
  SPDX headers; every new command has a stable objectName from §3.7 and
  appears in a menu.
- `docs/index.md`, `docs/verification.md` and `docs/decisions.md`
  (D-026 … D-030) updated in the milestone commits, not afterwards.
- An M21 notebook opens, looks the same, and saves byte-identical until
  edited.
- Manual pen check on the QindaQt desktop with a tablet: pressure,
  highlighter under pen, live eraser, barrel button, palm ignored, dark
  theme white page, fit-width on a laptop window with the navigator open.
