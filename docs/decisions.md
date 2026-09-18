<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Decisions (ADR-lite)

Durable cross-cutting choices, newest first. Each entry: context, decision,
consequences. For desktop-level architecture see
`container-wm/docs/wiki/adr/`.

## D-027: The suite's UI is QindaTK on QtQuick; paper stays paper; the panel menu is exported from an action model

**Date:** 2026-09-17 · **Status:** accepted (M27; M28–M31 extend it to the
other apps) · **Plan:** `docs/plans/qindatk-conversion.md`

**Decision.** The office suite's user interface is QindaTK (QML, on
QtQuick) — the same toolkit the QindaQt desktop's first-party apps use —
not Qt Widgets. One document is still one ordinary top-level window
(ADR-0110), now a `Tk.AppWindow` over a `DocumentHost` in `libs/office_quick`
(`QindaOffice.Shell`): the host owns the document, the commands
(`ActionModel`), the menu tree (`MenuTree`) and every flow (open, save,
close consent, overwrite, print, preferences) as *asynchronous requests the
QML window answers with dialogs*; the window renders bands and dialogs from
the host and never owns state. QindaNote is the first app on it.

**What was verified before deciding (plan §2).**

- *V2 — the pen.* QtQuick delivers a raw `QTabletEvent` (pressure, tilt,
  pointer type) only to a C++ `QQuickPointerDeviceHandler` subclass and only
  through the platform path; plain items and QML handlers see synthesised
  mouse events with no pressure. Hence `Tk.StylusHandler` (a QindaTK
  addition) and `NotePageItem` as a `QQuickPaintedItem` in document units
  under a `Tk.Viewport`. Touch never reaches it: the palm baseline.
- *V3 — the desktop's `AppShell::ActionRegistry`* refuses commands without
  a shortcut and models flat menus only, so the suite keeps its own
  exporter over the desktop's toolkit-neutral `Exporter::MenuSource`
  (*V4*: validator limits depth 6, 128 children, 1024 items, unique ids,
  one checked item per radio group).
- *V5 — the Widgets suite's panel menu was dead for 71 of 72 commands*,
  in every app, since the start: activation resolved through
  `menuBar()->findChild<QAction*>(id)` while the actions were parented to
  the window. `ActionModelMenuSource` exports from the model and
  `GlobalMenuExport` resolves an activation by id *in the model*;
  `tests/qindanote/tst_note_menu_export.cpp` asserts that every exported
  command resolves and fires (a hard failure now, not an expected one).
- *V9 — one announcement.* With `QGuiApplication` there is no
  `QDBusMenuBar`, so the double export (`/MenuBar/2` from Qt plus the
  suite's own endpoint) disappears; the suite's export is the only one.

**Paper stays paper (D-004 kept).** The page is pinned white with dark ink
under every theme; the chrome follows the desktop's tokens through
`ThemeBridge.qml` when the `QindaTK.QindaQt` bridge is installed and the
`sloom-dark` preset otherwise, at the `comfortable` density
(`Preferences.themePreset` / `Preferences.density`). `NoteInputTest::
pageIsPaperUnderTheDarkPreset` reads the pixels.

**Printing without a QApplication.** `QPrintDialog`/`QPrintPreviewDialog`
are Widgets-only. `PrintJob` renders the app's callback into a PDF; the
preview is that same PDF rendered by Poppler (`PagePreviewItem`); the
dialog (`PrintDialog.qml`) lists printers from `PrinterModel`. What you
preview is what prints.

**Rules superseded** (AGENTS.md is rewritten in M31): rule 3 (Widgets
palette roles → QindaTK theme tokens; no literal colours in chrome), rule
4 (`QAction` per command → one `ActionEntry` per command; the `objectName`
becomes the entry id, the same stable string), rule 8 (menu shape is the
`MenuTree`, exported through `ActionModelMenuSource`). The Widgets apps
build behind `QQO_WIDGETS_APPS=ON` until their milestone lands.

**Consequences.** Every window has a structural `--dump` fixture
(`tests/qml_fixtures/`); QML files obey the same 500-line budget;
`office_ai` is Widgets-free (`AssistantController` + `AssistantPanel.qml`,
`ProposalDialog.qml`, an `InlineActionFlow` presenter seam), its Widgets
panel compiled only behind the option.

## D-026: Ink is filled geometry, the eraser is live, and pen state is window-independent

**Context.** QindaNote's M22 pen overhaul (`docs/plans/qindanote-ux-overhaul.md`).
Ink was drawn segment by segment with `drawLine`: no antialiasing anywhere,
the highlighter's width multiplier was dead code overwritten inside the
loop, and a translucent stroke double-painted every joint into blobs. The
eraser did nothing until pen-up, pushed an undo snapshot even when it
erased nothing, and scanned the whole layer per sample. Colour needed a
modal dialog, the tool strip was a column of text buttons down the left,
every keystroke rewrote the page JSON, and pen state was forgotten on
restart. The page also followed `palette().base()`, so a dark theme hid
the default black pen (the exact bug D-004 pinned out of QindaWrite).

**Decision.**

- Strokes are **filled outline paths** (`office_ink/stroke_geometry`):
  midpoint-quadratic smoothed centreline, walked at ≤ 2 page-unit steps and
  offset by ±width/2 — `widthAt()` pressure width for Pen, constant
  `stroke.width` for Highlighter; round caps for Pen, flat for Highlighter.
  `renderStroke` fills one path with antialiasing set inside the function,
  so every caller gets smooth ink. The highlighter fills at alpha 90 as ONE
  path (no joint blobs), and `renderStrokes` paints in two passes —
  highlighters first, then pens — so highlighting over ink keeps the pen
  readable. Screen and print use the same render order (background →
  images → blocks → highlighter → pen); the page itself is pinned white
  per D-004.
- The eraser is **live**: `Ink::eraseAt` runs on every press/move sample
  against spatial-grid candidates (whole-stroke mode, or partial mode that
  drops samples and splits at gaps). One undo command per gesture, pushed
  at release only when something actually changed. Repaints are narrowed
  to the touched rect, inking repaints only the fresh segment.
- The page history is **one QUndoStack** (`note_page_history`) replacing
  the two hand-rolled snapshot vectors; M22 wires `InkCommand` only, M23
  adds blocks/background commands to the same stack.
- Tools, colours, widths and eraser options are **one action set** shared
  by the `&Ink` menu and the strip's two popup buttons (rule 5: the strip
  never clones a menu action). The strip moved to the top area and is
  icon-only through shipped `draw-*` glyphs (D-013); Undo/Redo left it
  (they are on the main toolbar).
- Per-tool state is **state, not preference** (`tools.json` in the app's
  state dir): last tool, colours, widths, eraser mode/size change every
  few seconds and no dialog should claim them. Preferences gains exactly
  two note keys — `note/pressure`, `note/smoothing` — both read by the
  view (D-014). Position smoothing itself is a render-time EMA in
  `StrokeBuilder`; stored samples are never rewritten.
- Page writes are debounced (500 ms) with a synchronous flush at page
  switch, export, the replace sweep and close; the recovery journal
  already covers the window in between.

**Consequences.** An M21 notebook opens unchanged and saves byte-identical
until edited (no format keys were touched). The stylus barrel button
borrows the eraser for one gesture (MiddleButton; RightButton becomes the
M24 lasso). Single-letter tool shortcuts (P/H/E/T) work because text
editors accept `ShortcutOverride` for printable keys; the navigator tree
does not, so a letter with the tree focused switches tools instead of
keyboard-searching — accepted and documented in `docs/verification.md`.
D-009's zoom-as-view-transform stands unchanged.

## D-025: The package ships runtime files only

**Context.** `make install` exported the suite's insides: seven static
archives in `/usr/lib64`, every first-party header under
`/usr/include/qindaoffice/`, and — worse — the vendored QXlsx as
`/usr/lib64/libQXlsxQt6.so.1` with its headers and CMake package config.
Nothing consumes any of it. The libraries are the apps' own implementation,
split up for testing, not a public API; there is no second consumer in the
overlay or anywhere else, and the headers carry no stability promise.

The QXlsx half was an active hazard rather than mere clutter. The vendored
copy is built static by default, but Gentoo's `cmake.eclass` sets
`BUILD_SHARED_LIBS ON CACHE BOOL ""` for every package it builds, so under
Portage — and only under Portage — it became a shared library installed
under the same SONAME a system-wide QXlsx would use, ready to collide with
a real `dev-libs/qxlsx` on any machine that later installed one.

**Decision.** The libraries drop their `install()` rules entirely. The
vendored QXlsx is pinned static with a NORMAL `BUILD_SHARED_LIBS` variable
— normal shadows cache, so the eclass cannot override it — and added with
`EXCLUDE_FROM_ALL`, which keeps its own unconditional `install()` rules out
of the image; QXlsx offers no option to disable them.

**Consequences.** The image is five binaries, five desktop files, the
hicolor icons, the mime package and the templates: nothing a user cannot
run. QXlsx links into `calc_io` instead of being loaded at runtime, so the
apps no longer depend on a library the suite itself planted in
`/usr/lib64`. The libraries are still ordinary CMake targets, so the tests
link them exactly as before; anyone who genuinely needs them builds the
tree. Re-exporting them later means restoring a real versioned API, not
just the `install()` lines — which is the right bar for making them public.

## D-024: The package is a snapshot tarball, not a git checkout

**Context.** The ebuild used `git-r3` against
`file:///home/cabewse/work_SPaC3/QindaOffice` with an empty `EGIT_COMMIT`
— a path that exists on one machine, pinning nothing. Worse, the tree it
cloned carries QXlsx as a git submodule, and Portage's default `FEATURES`
include `network-sandbox`: `src_unpack` cannot reach `https://` at all, so
the submodule could never be fetched. The package was unbuildable anywhere
but its author's desktop, and not reproducible even there.

**Decision.** `SRC_URI="${P}.tar.xz"` with `RESTRICT="fetch"`, the shape
the rest of the `qindaqt` overlay already uses for unpublished snapshots.
`tools/make-dist.sh` cuts the archive from a commit: it refuses a dirty
tree, exports the tracked tree at `HEAD`, exports QXlsx **into**
`third_party/QXlsx`, and writes `.dist-commit`. `pkg_nofetch` prints that
command, so an operator who has only the ebuild can still produce the
distfile.

**Consequences.** The build needs no network and no submodule support, so
it works under stock sandbox settings. Every installed tree names the
commit it came from. The cost is a manual step per snapshot — the archive
has to be cut and manifested before the version can be merged — which is
the right trade while the repository is unpublished, and disappears the
day it is hosted (the `SRC_URI` becomes a URL and nothing else changes).
The in-repo `packaging/gentoo/` copy is the source of truth; the overlay
copy must match it, and `docs/verification.md`'s M21 packaging gate checks
that they have not drifted.

## D-023: The suite has a front door

**Context.** Four apps and no way in. The desktop launcher listed them
individually, so "open the thing I was working on" meant remembering which
app owned it, starting that app, and finding the file in its own recent
list. Nothing showed the suite as one thing.

**Decision.** `qindaoffice` — a launcher, not a document window, so
ADR-0110's one-document-per-window rule does not apply to it, but
everything else does: an ordinary top-level window, the desktop identity
pinned before it maps (rule 2), stock widgets and palette colours only
(rule 3).

It shows one painted card per app (mascot, name, what it is for) and the
suite's recent documents merged from the four apps' own lists. It READS
that state and never writes it: recents live in each app's config dir
(rule 7, bounded persistence per app, never shared), so the launcher walks
four files rather than introducing a shared one — a shared list is exactly
the cross-app store that rule exists to prevent. A file it cannot parse is
skipped, never rewritten, because it belongs to another app.

Apps start DETACHED (`QProcess::startDetached`): the launcher is a front
door, and closing it must not take the documents with it. A card for an app
whose binary is missing says so instead of looking clickable and doing
nothing, and a launch that fails says so in the status line — a launcher
that silently does nothing is the worst possible front door.

`AppCard` is painted rather than assembled from labels so the whole card is
one hover and one click target and the mascot can be as large as it
deserves. Painted widgets have no accessible identity for free, so each
card sets its name and description explicitly.

**Consequences.** The launcher links `office_core` only (for the per-app
config paths) and never `office_shell`: it hosts no document, so pulling in
the document frame — and the desktop's global-menu archives with it — would
be dependency for its own sake. `appIdForPath` is the one place the suite
decides which app owns a suffix; it mirrors what each app's own `load()`
dispatches on, and the two must not drift.

## D-022: A note page has three kinds of block, and one layout

**Context.** A note page carried typed text only, stacked at a fixed 100 px
per index, and the on-screen layout and the print layout each computed that
stride for themselves. Adding checklists and images — blocks of different
heights — made that duplication untenable: a layout computed twice is a
layout that drifts, and the symptom is typed text printing somewhere the
user never saw it.

**Decision.** `note_blocks.h` decides where every block sits. The view
positions widgets through it and the pure page renderer prints through it;
neither computes geometry of its own. Checklist items are a checkbox plus
an editable label that write back to the envelope on every change — the
contract the text blocks already had. Images carry their BYTES, base64 in
the page envelope, because a notebook page is one self-contained JSON file
and a path reference breaks the moment the notebook moves; they are painted
rather than hosted in a widget so they sit under the ink, and their height
is bounded so one photograph cannot push everything else off the page.

**Consequences.** A new block kind is a case in `layoutBlocks` plus a case
in each of the two painters; nothing else needs to know. An unknown kind
from a newer version is skipped rather than renumbering everything after it.

## D-021: QindaShow's design layer is model state

**Context.** M20 left QindaShow with one build effect ("appear"), no
masters, no snapping and no grouping — the four things that separate a
slide someone arranged from a slide someone typed into.

**Decision.** All four are fields on the model, not scene constructs, so
all four survive save and reload (through the fodp writer's qq: extension
namespace) and undo like any other edit.

* **Masters** carry the background and the default type. The RENDERER
  applies them, so the editor canvas, the sorter thumbnails, the presenter
  view, the player and the PDF all show the same deck. Only runs that never
  chose for themselves inherit, so a deliberately red word stays red.
* **Animations** are a per-shape kind and duration the renderer applies at
  a progress value. The player tells a build advance from a slide change
  and animates the arriving shapes rather than cross-fading two whole
  frames — blending frames for a revealed bullet flashes everything else on
  the slide. Reduced motion still means the shape is simply there.
* **Snapping** works in slide points, not pixels, so the threshold means
  the same at every zoom. Only the shape the pointer has hold of snaps: the
  rest of a multi-selection follow it exactly, or a group gets pulled apart.
* **Grouping** is a shape field. Selecting one member selects the group.

**AGENT-GUARD.** `rebuildItems()` clears the scene, and
`QGraphicsScene::clear()` fires `selectionChanged` WHILE the items are being
destroyed — any listener that walks the item list then touches half-dead
objects (two SIGSEGVs found by test). The rebuild blocks the scene's signals
and emits one honest notification afterwards; a pure z-order change reorders
the model and re-applies z values instead of rebuilding at all; and moving a
shape's rect calls `prepareGeometryChange()` first, because the scene indexes
items by bounding rect.

**Consequences.** A gesture that touches several shapes now brackets itself
(`compoundEditStarted`/`Finished`) and the window folds everything between
into ONE `SlideStateCommand`. Anything that moves more than one shape must
use that bracket, or it is one undo step per shape again.

## D-020: QindaWrite's document-scale features, and two defects they found

**Context.** Footnotes, a table of contents and tracked changes are the
three things a word processor is expected to do that Qt's QTextDocument has
no concept of at all.

**Decision.**

* **Footnotes** are two halves: a superscript MARKER carrying an id in the
  body, and the note text beside the document keyed by that id. Numbering is
  never stored — it is the marker's position in document order, recomputed
  on every read, which is what makes inserting a note in the middle
  renumber the rest. The notes print in the page's bottom margin band
  through `drawPageDecoration`, which the screen view, the print path and
  the PDF exporter all already call. Documented bound: the band is the
  bottom margin, so a page shows about six short notes before the rest are
  elided — a growing band needs per-page re-layout, which a single
  QTextDocument flow cannot express.
* **The table of contents** reads the document's own heading styles and
  takes page numbers from the SAME `PageGeometry` the view paints with,
  which is the only thing that makes printed page numbers worth printing.
  Generated blocks carry `kTocBlockProperty` so Update replaces exactly the
  previous table, and the table never lists itself.
* **Tracked changes** are built from a char-format property per run.
  Insertions are marked after the fact from `contentsChange`; deletions
  cannot be (the text is already gone), so while recording the delete
  gestures are intercepted on the view and turned into strikethrough marks
  that stay until the change is resolved. Accept and reject are exact
  inverses.
* **The ruler** draws the page band at the view's own zoom and scroll
  offset and carries the three indent markers, which write into the cursor
  block's format. It numbers centimetres from the TEXT COLUMN: numbering
  from the paper edge reads one margin out.

**Two defects these surfaced.** Enter at the end of a heading kept writing
in heading style, because Qt carries the block format forward — so
everything after a heading was a heading, which is also why the contents
listed body paragraphs. And Heading 1 forces a page break before it, so a
document whose first paragraph is a Heading 1 opened with a blank page 1
and every page number in it was one out; the break is now suppressed for
the first block only.

**Consequences.** Anything that walks fragments and rewrites them must do
it from the END backwards — replacing a run changes every later position,
so a forward pass edits at stale offsets. Both the footnote renumberer and
the revision resolver say so at the loop.

## D-019: QindaCalc's rules live in the engine

**Context.** Conditional formatting, data validation, the autofilter and
cell comments are the four things a spreadsheet says about a RANGE rather
than about one cell's value. All four could have lived in the app.

**Decision.** They live in `calc_engine` (`sheet_rules.h`) for the same
reason the structural edits do: the grid, the printer and the
crash-recovery payload have to evaluate them identically, and all four have
to survive undo and move with the rows and columns they cover.

* Conditional formats layer rule by rule, later rules winning field by
  field, so "highlight the row" and "make negatives red" compose. They sit
  over the cell's own style but under the selection, so a formatted block
  never hides the cursor. A blank cell satisfies no comparison — treating
  it as zero paints every empty cell of a "less than 10" rule.
* Validation is enforced in `SpreadsheetDocument::applyInput`, the one path
  every UI edit takes, so the in-cell editor, the formula bar, paste and the
  AI apply are all covered. A refused value SAYS why; clearing a cell is
  never refused, or a mistyped value cannot be taken back.
* The autofilter hides a row by giving it no band at all: painting, the
  header, hit-testing and scroll-into-view walk the same list, so a hidden
  row is uniformly absent rather than painted blank.
* Comments draw the usual corner triangle and show as a tooltip from the
  VIEWPORT — tooltips never reach the scroll area itself.

**Consequences.** `structure.cpp` moves all four with the rows and columns
they cover, and `calc_undo`/`calc_recovery` carry them: a rule the payload
does not carry is a rule the user loses on recovery, silently. A new rule
kind has to be added in all three places or it is a defect.

## D-018b: Open replaces the blank document you opened from

**Context.** ADR-0110 already said a pristine untitled window may absorb an
opened path, and `OfficeApplication` implemented it — but it never worked
the way it read. QindaWrite's fresh window was never pristine, because
`WriteDocument` marked itself dirty from `QTextDocument::contentsChange`,
which also fires for FORMAT-only edits, and the spell checker is a
`QSyntaxHighlighter` whose first pass runs while the window is still being
built. And the reuse picked whichever blank window came first in the list,
not the one the user pressed Open in.

**Decision.** Dirty follows `QTextDocument`'s own modified flag — which is
what "the user edited this" means — while `contentChanged` still fires for
the recovery journal, but only when characters actually moved.
`openPaths()` takes the requesting window and prefers it when it is
pristine. A window that has been typed into is not pristine, so its content
is never displaced: that open goes to a new window.

**Consequences.** All four app suites assert that a freshly created window
is pristine. Anything that dirties a document during window construction
breaks pristine reuse silently, which is exactly how it broke.

## D-018: QindaWrite gains the typography a word processor is reached for

**Context.** The Format menu covered bold/italic/underline/strikethrough,
one colour, alignment, lists and indent; Insert covered page break, table,
image and columns. That leaves out what people actually open a word
processor to do: highlight a passage, set a footnote marker in superscript,
change line spacing, drop today's date into a letter, turn a URL into a
link. There was no word count anywhere — the one number a word processor is
asked for most — and the font-size combo showed an EMPTY box on a pristine
document, because a fresh char format carries no explicit point size and the
sync wrote `QString()` for that case.

**Decision.** `WriteTypography` (a controller beside `FormatController`,
same action discipline) owns highlight colour, superscript/subscript,
grow/shrink font, line spacing (single / 1.5 / double, as
`ProportionalHeight` so it means what it says elsewhere), and Insert ▸
Hyperlink / Special Character / Horizontal Rule / Date / Time. The
horizontal rule is a block with `BlockTrailingHorizontalRulerWidth`, not a
drawn line, so it survives the HTML round trip like the rest of the
document; a hyperlink resets the char format afterwards (clearing the
foreground rather than setting black, so the page stays readable under a
dark theme) instead of letting link styling bleed into the next keystroke.

Word and character counts live in the permanent status field and follow the
selection when there is one; Tools ▸ Word Count… shows the full breakdown.
Both size combos (QindaWrite's and QindaCalc's) now fall back to the
effective font rather than going blank — a blank size box reads as "no
size", which is not a state the user can act on.

**Consequences.** Format/Insert commands belong in one of the two
controllers, never inline in `write_window.cpp`; the window is at its line
budget. `SpellChecker::setEnabled()` exists so the Preferences ▸ Writing page
is a real setting and not a checkbox over nothing.

## D-017: A notebook you cannot organise, and a page view that owned the window's height

**Context.** QindaNote's Page menu held two entries — New Page and
Background… — so a notebook could grow but never be tidied: no second
section, no rename, no delete, no reordering. Three defects sat underneath
that. `NotePageView` set the full page size as its `minimumSize`, so the
whole window could not be made shorter than roughly 1100 px and did not fit
on a laptop screen. A pristine `NoteDocument` had no section and no page at
all, so a fresh window showed an empty navigator and a page surface bound to
nothing. And `rebuildNavigator()` selected the first page INSIDE a
`QSignalBlocker`, so `currentItemChanged` never fired for it — the navigator
showed a highlighted page that had never actually been opened.

**Decision.**

1. **The page lives in a `QScrollArea`.** `NotePageView::updatePageMetrics()`
   resizes the widget to the zoomed, grown page; the scroll area supplies
   the scrollbars. Zoom and page growth are reachable instead of clipped,
   and the window's minimum is the window's own.
2. **A pristine notebook carries one section and one page**, so the first
   window is writable immediately. `Notebook::create()` seeds only when the
   notebook is really empty — seeding unconditionally on the first save gave
   that notebook a second, duplicate "Notes" section.
3. **Select outside the blocker.** The blocker covers the teardown and
   rebuild (clear() fires `currentItemChanged` for every item it destroys);
   the selection happens after it, through the normal open path.
4. **`NoteStructureActions`** owns new page/section, rename, delete, move
   up/down and the background picker, reached from the Page menu and the
   navigator's context menu. `Notebook` gained the matching structure edits;
   `removePage`/`removeSection` delete the page FILES too, because an index
   entry removed without its file leaves an orphan the next load resurrects.
   Deletions are not undoable (the notebook has no structural history), so
   each one asks first and names what it will remove.
5. **The declared-but-dead commands are implemented**: Edit ▸ Cut/Copy/Paste/
   Select All follow the focused text block, Edit ▸ Replace sweeps the whole
   notebook (reading unopened pages from disk first, and detaching the open
   page so its live editors cannot write the old text back), the AI
   Assistant docks like every other app's, and Page ▸ Export PDF goes
   through the same paginating callback Print uses.

**Consequences.** Anything that changes the notebook's structure goes
through `NoteStructureActions` and ends with `refreshNavigator()` +
`selectPageById()`, which is what keeps the navigator, the status field and
the command enablement agreeing.

## D-016: QindaShow drew no slide, and Arrange held two commands

**Context.** The editor canvas is a `QGraphicsView` with one item per
SHAPE — and no item for the slide itself. Nothing painted the page, so an
empty slide rendered as a flat dark-grey view with no page edges anywhere in
it; a new deck opened on exactly that, because `newDeck()` used
`Presentation::addSlide()` (no shapes) while `addSlide()` used `makeSlide()`
(layout placeholders). The sorter's thumbnails were built from a single
pixmap, so Qt rendered the SELECTED one tinted with the highlight colour —
the current slide showed as a flat blue rectangle. Arrange held "Bring
forward" and "Send backward", built as anonymous lambdas with no
objectName, so they were unreachable from the panel's global menu.

**Decision.**

1. **`SlideCanvas::drawBackground()` paints the page** — shadow, white
   sheet, hairline frame — and `fitScale()` scales the whole slide into the
   viewport, with the user's zoom multiplying it. Zoom 100% means "the whole
   slide, as large as it goes", which is what a slide editor is expected to
   show.
2. **A pristine deck carries the layout's placeholders** ("Click to add a
   title"), so the first thing the user sees is something to type into.
3. **Thumbnails register the same pixmap for Normal, Active and Selected.**
4. **`ShowArrangeActions`** owns front/forward/backward/back, duplicate,
   delete, align (six), distribute (two) and move-slide up/down. One shape
   aligns to the SLIDE, several align to each other, so "align centre" is
   useful in both situations without a mode. Multi-shape geometry is pushed
   as ONE `SlideStateCommand`: aligning six shapes is one action to the user
   and must be one Ctrl+Z.
5. **Deck-wide Replace** (Edit ▸ Replace, previously a dead entry) rewrites
   titles, shape runs and notes in one `SlideListCommand`.

**AGENT-GUARD (two crash shapes found by test).** `rebuildItems()` clears
the scene, and `QGraphicsScene::clear()` fires `selectionChanged` WHILE the
items are being destroyed — any listener that walks the item list then
touches half-dead objects. The rebuild now blocks the scene's signals and
emits one honest `selectionChanged` afterwards, and a pure z-order change
reorders the model and re-applies z values instead of rebuilding at all.
Changing a shape's rect calls `prepareGeometryChange()` first: the scene
indexes items by bounding rect, and moving the model behind its back leaves
stale entries that hit-testing follows.

## D-015: QindaCalc could not insert a row

**Context.** An audit of what the spreadsheet could actually do found the
whole Insert menu was "Chart…", the whole Sheet menu was "Insert Sheet",
there was no Data menu, and the grid had no context menu. There was no way
to insert or delete a row or column, sort, merge, put a border on a cell,
widen a column, freeze a header, fill a formula down, total a column, or
rename, reorder or delete a sheet. Clicking a row or column header did
nothing at all (the press fell through to `QAbstractScrollArea`), **the
Delete key did not clear a cell** — Cut was the only way to empty one — and
nothing anywhere reported the sum of the selection, which is a
spreadsheet's most-read number. The cursor started at a
default-constructed `CellAddr`, which is `{-1,-1}` and not a cell at all.

**Decision.**

1. **Structural edits live in the engine** (`calc_engine/structure.h`),
   because moving a cell is only half the job. `applyStructureEdit()` moves
   cells, styles, merges and explicit geometry, then rewrites EVERY formula
   in the workbook — including ones on other sheets that name this one — so
   references follow; a reference that falls inside a removed band becomes
   `#REF!` rather than silently re-pointing at whatever slid into its place.
   Named ranges and chart anchors follow too. `Workbook::reindexAll()`
   rebuilds the dependency index afterwards: it is keyed by sheet INDEX and
   cell position, both of which the edit moves.
2. **Sheet operations rewrite references by name.** Rename requalifies every
   formula (a rename that does not would turn each cross-sheet formula into
   `#NAME?`); delete turns references to the removed sheet into `#REF!` and
   leaves every other sheet working; the last sheet cannot be removed.
3. **Sort moves rows with their formulas**, shifting relative references by
   the row delta, blanks last in both directions, `std::stable_sort` so a
   second sort on another column acts as a tie-break.
4. **The view owns no document mutation.** Header clicks select whole rows
   and columns (bounded by the USED range — a literal 1,048,576-row
   selection would hang every range command and mint a style per cell),
   boundary drags resize with live feedback and report the final size for
   ONE undo step, double-clicking a boundary autofits, Delete emits
   `clearRequested`, and right-click emits `contextMenuRequested`.
5. **Freeze panes is a band problem, not a scroll-offset problem.**
   `visibleRowBands()`/`visibleColumnBands()` are the single source of
   position for painting, the header bands and hit-testing. Deriving a
   position from the scroll value alone stops being true the moment a pane
   is frozen.

**Consequences.** New spreadsheet commands belong in
`CalcStructureActions`; new document mutations follow the existing
snapshot-push-dirty-notify shape in `calc_structure_ops.cpp`. A latent
engine bug surfaced on the way: `Workbook::recalcAll()` read the packed cell
key with row and column swapped, so every formula cell off the diagonal was
marked dirty at an address holding no cell and was skipped — exactly the
stale cross-sheet reference the function exists to fix.

## D-014: The suite has settings, a status bar, and no dead menu entries

**Context.** Settings ▸ Preferences… was created disabled by
`createStandardActions()` and never enabled, so all four apps shipped a dead
menu entry and the suite had no settings UI at all. No window created a
status bar until something called `showMessage()`, at which point the bar
appeared and pushed the document area up under the user's cursor. Three of
the four apps added "New from Template" with `QMenu::addMenu()`, which
appends — so it sat BELOW Quit. QindaCalc and QindaNote offered an AI menu
whose only entry was a permanently disabled Assistant….

**Decision.**

1. **`Shell::Preferences`** — one atomic JSON store per app under
   `$XDG_CONFIG_HOME/qindaoffice/<app>/preferences.json`, written through on
   every change and announcing one coarse `changed()` so every window of the
   app re-applies. **Every key is read by something**: adding one means
   wiring it in the same change (D-012).
2. **`Shell::PreferencesDialog`** with shell pages (chrome, default zoom,
   auto-save, document font) plus `addPage()` so an app adds its own —
   which is what keeps `office_shell` from having to know about `office_ai`
   or any engine.
3. **Auto-save never opens a dialog.** It runs on a timer the user is not
   watching, so every case that would ask a question — no path yet, the file
   changed on disk — is skipped and left to the explicit Save. Crash
   recovery still covers what auto-save declines to write. Default off.
4. **The status bar is created eagerly** with a permanent right-hand field
   (`setStatusInfo`), so a transient message can never displace it and the
   first message can never resize the document. Every app fills it: word and
   character counts, cell aggregates, "Slide 3 of 12", the current page.
5. **View ▸ Show Toolbars / Show Status Bar** are persistent named actions
   backed by the store. **`DocumentWindow::fileMenuAnchor()`** is the
   insertion point for app File-menu entries; appending is the defect shape.
6. **Every app's Assistant is wired.** A declared capability is implemented
   or not offered.

**Consequences.** `applyPreferences()` and `addPreferencePages()` are the
two app hooks; both run after the subclass exists (`finalizeAppMenuHooks`),
never from the base constructor. Window chrome that the store controls must
be applied in `applyChromePreferences()`, which is guarded against chasing
its own toggles.

## D-013: Toolbars wrap, and the suite ships the action icons its theme lacks

**Context.** The formatting strip showed **Bold Italic Underline Text Color
Fill Color Align Left Align Center Align Right** as words and ended in a "»"
chevron: at the window's real width the last commands were not on screen at
all. Two independent causes.

First, the icons never resolved. The actions already asked for the right
freedesktop names (`format-text-bold`, `format-justify-left`, …), but the
desktop's QindaQt icon theme carries the shell family (`document-new`,
`edit-undo`) and **not** the format/insert/zoom family, and its
`index.theme` says `Inherits=hicolor` — not breeze, which has them. A
missing name makes `QIcon::fromTheme()` return a null icon and Qt falls back
to the action **text**, so every button became as wide as its label.
QindaWrite's format actions had no resolving icon at all.

Second, `QToolBar` does not wrap. Whatever does not fit goes behind the
extension chevron — buttons that exist, are enabled, and are never seen.

**Decision.**

1. **`WrappingToolBar` + `FlowLayout` (office_shell).** Every command strip
   in the suite — the shared main toolbar and QindaCalc's, QindaWrite's,
   QindaShow's and QindaNote's own — hosts its items in a flow layout that
   wraps onto further rows and grows the toolbar. Nothing is ever hidden for
   want of width. `QToolBar::actions()` no longer reports strip contents
   (they live on the buttons), so `stripActions()` is the contract, and the
   UI tests assert against it.
2. **Per-action button style.** Icon-only when the theme resolved an icon,
   text otherwise — an icon-only button with a null icon is a blank square.
   The full text is always the tooltip, and a long label can be shortened
   for the strip alone through `QAction::setIconText()` (`QToolButton` labels
   itself from `iconText()`), which is how the main toolbar stopped carrying
   "Number format: #,##0.00" at full width.
3. **The suite ships the 24 action icons it needs**, into
   `hicolor/scalable/actions` — the freedesktop fallback every theme ends
   at, so `fromTheme()` resolves them under any active theme and a theme
   that *does* carry the name still wins. One neutral mid tone, because
   hicolor has no light/dark variants; verified legible at 16 px on both a
   light and a dark toolbar. **Never ship `hicolor/index.theme`** — that
   file belongs to `x11-themes/hicolor-icon-theme` and installing one would
   collide with it.

**Consequences.** The fix for a missing icon is to add it here, not to reach
into the desktop's theme. If the QindaQt theme later grows the format family
it takes precedence automatically and nothing here needs changing. A new
command strip should be a `WrappingToolBar`; a plain `QToolBar` is the
defect shape. Regression coverage: `tst_calc_ui` and `tst_write_ui` squeeze
their windows to a third of the width and assert every strip command still
has a visible button inside the strip.

## D-012: A stub is a bug — every declared capability is implemented or not declared

**Context.** A second audit pass asked what the suite *claims* to do and
compared it with what it does. Five capabilities were declared and not
delivered, which is indistinguishable from a defect at the UI:
`recoverySnapshot()` returned `{}` in QindaCalc and QindaNote (the mandated
default-on crash safety net was a no-op for half the suite); QindaNote had
an undo stack with no inverse, so Edit ▸ Redo could never enable;
`QindaCalc.desktop` declared `ods`/`xls` while `load()` rejected everything
but xlsx/csv; and the engine resolved named ranges that nothing could ever
create or persist.

The same pass found the shape of bug those stubs hide. `Notebook::save()`
wrote **every** page, but pages load lazily — so opening a notebook, viewing
one page and pressing Ctrl+S replaced every other page's file with an empty
envelope. The lazy-load model had no "is this loaded?" bit, so no caller
could tell content from absence.

**Decision.**

1. **Lazy content carries a loaded flag.** `NotePage::loaded` is the single
   source of truth: `save()` and `copyStructure()` never write an unloaded
   page (Save As materialises them from the source first), the navigator
   never re-reads a page that is already in memory, and the recovery payload
   records which envelopes it actually holds. Anything that loads content on
   demand must carry the same bit.
2. **Recovery payloads are the document's own serialization, not a file
   format.** QindaCalc's is the undo layer's `WorkbookSnapshot` in JSON
   (`calc_recovery.cpp`) — deliberately not an in-memory xlsx, because QXlsx
   has no chart read path and the round trip would drop every chart. Values
   are typed (`n`/`t`/`b`/`e`), never display text, or the text cell `007`
   comes back as the number 7. QindaNote's is the notebook index plus the
   loaded envelopes plus the source directory.
3. **A declared MIME type is a promise.** QindaCalc opens and saves foreign
   spreadsheet formats through the headless LibreOffice bridge, so its
   `.desktop` list, `mimeTypes()` and the file-dialog filters agree. The
   blocking converter glue moved to `Core::convertBlocking()`; QindaWrite
   and QindaShow each carried a byte-identical private copy.
4. **Named ranges round-trip.** The Name Box defines a name over the
   selection (and navigates to a reference or an existing name), names are
   pinned absolute on the defining sheet — a name is an address, not a
   relative offset — and they travel through xlsx both ways. Import reads
   them **before** the cells: `installFormula()` resolves a name to its
   dependency targets at install time, so a name defined afterwards leaves
   every formula that uses it unindexed and evaluated against nothing.
5. **A file we cannot parse is moved aside, never overwritten.**
   `Core::JsonSettings` and `Ai::ModelCatalog` rename an unreadable file to
   `<path>.corrupt` and continue from defaults. Refusing to write leaves the
   app permanently stuck; overwriting destroys the user's data; quarantining
   does neither.

**Consequences.** The four apps now either do the thing or do not offer it.
`Notebook::persistPage()` refuses without a directory (it used to write
`sections/…` into the process's working directory for untitled notebooks)
and writes atomically like every other document path. The regression tests
that matter — a notebook save that must not blank untouched pages, a
recovery payload that must survive typed values and styles on empty cells,
an xlsx that must carry its names — live in `tst_qindanote`,
`tst_calc_window` and `tst_calc_io`.

## D-011: Command availability is a property of the document host, not of the stub engine

**Context.** A second audit pass ran the four real windows offscreen and
dumped every window-scoped `QAction` with its enabled state, objectName and
shortcut. Three shell-level defects showed up in all four apps at once, none
of them visible from the test suite (which drove documents directly and
never asked whether the commands that reach them were reachable):

1. `createStandardActions()` creates Save/Save As disabled ("no engine
   yet"), and the only code that re-enabled them was
   `DocumentWindow::setDocument()`'s `PlainTextDocument` branch. All four
   apps install their engine through `setDocumentHost()` instead — so
   **File ▸ Save, File ▸ Save As, the toolbar Save button and Ctrl+S were
   dead in QindaWrite, QindaCalc, QindaShow and QindaNote**, while
   `saveDocument()` itself worked (the close-consent dialog reached it).
2. Close Window and Quit were bound with `QKeySequence::Close` / `::Quit`.
   Those resolve through the platform theme's keyboard scheme, and the
   qindaqt platform theme (ADR-0115) delegates `KeyboardScheme` to Qt's
   generic Unix theme → X11 scheme, where they are **Ctrl+F4** and the
   **"Exit" media key**. AGENTS.md rule 1 names Ctrl+W and Ctrl+Q.
3. Menu commands built with the `addAction(text, receiver, functor)`
   overload carry no objectName, which is the dbusmenu activation identity
   (rule 5) — those entries were unreachable from the panel menu.

**Decision.** (a) `setDocumentHost()` enables Save/Save As: a window that
owns a document can save, and command availability belongs to the host, not
to the stub engine's branch. (b) Ctrl+W and Ctrl+Q are literal
`QKeySequence`s in `standard_actions.cpp`, mirroring the first-party text
editor; the shortcut assertions in `tst_standard_actions` were pinning the
defect and now assert the mandated chords. (c) Every menu command in the
suite carries a stable objectName — `ActionId` for suite-standard commands,
an app-scoped literal (`calcTemplate_*`, `showAi*`, `notePenTool`, …) for
app commands, matching QindaWrite's existing pattern. The QindaNote tool
strip also gained the toolbar objectName `QMainWindow::saveState()` needs.

**Consequences.** Offscreen action-state dumps are the cheap way to catch
this class: a command can be perfectly wired and still be unreachable.
`tst_document_window` now installs a non-`PlainTextDocument` engine through
`setDocumentHost()` and asserts both the Save enablement and the two chords,
so neither can regress silently.

## D-006: QindaCalc undo is snapshot-based; clipboard carries a rich JSON format

**Context.** QindaCalc grew edit depth (M12): undo/redo, cut/copy/paste, cell
formatting toolbar, find bar. Cell-level inverse commands would duplicate
the engine's parse/dependency/recalc pipeline; the style table turned out
to already reserve id 0 as "no style" (ctor-seeded default, 1-based ids).

**Decision.** Every structured mutation (cell input, clear, paste,
StylePatch restyle, sheet insert) snapshots the whole workbook before and
after into one `QUndoStack` command (`calc_undo.h`). The snapshot skips the
reserved style id 0 - re-appending it on restore shifted every real style
id by one (cells pointed at wrong entries; found by test). Restore rebuilds
a fresh `Workbook` through public mutators, re-registers the dependency
graph from the stored ASTs and recalcs once; the rebuilt book is
move-assigned in. **Consequence:** `Sheet`/`CellStyle` references dangle
across any undo point - code must re-fetch from the document (rule recorded
where the seam lives). Clipboard: `text/plain` TSV (display text) plus
`application/x-qindacalc-cells` JSON (raw inputs) so in-app copy/paste
keeps formulas exact; cell styles do not travel yet (documented
limitation).

**Consequences.** Ctrl+Z/Y, Ctrl+X/C/V and F3 now behave like every other
spreadsheet; formatting actions share the suite-standard `ActionId` names
with QindaWrite's toolbar (same conceptual commands, different host).

## D-007: QindaShow styling flows through canvas old/new payloads; selection survives rebuilds

**Context.** M13 added shape styling (bold/italic/fill/stroke), a shapes
toolbar with persistent insert actions, and a shape clipboard. Two
invariants turned out to be load-bearing. First, the canvas owns all model
mutation and reports old/new payloads the window turns into undo commands;
letting the toolbox mutate shapes directly would have bypassed that. Second,
every undo push fires `indexChanged` -> `fullRefresh()` -> `setSlide()` ->
`rebuildItems()`, which recreates every item - selection used to die there,
and during window teardown the scene re-fires selection changes after the
window's actions are already destroyed (SIGSEGV found by test).

**Decision.** Styling ops are canvas methods (`setSelectedFill/Stroke/TextBold/
TextItalic`) emitting a new `shapeStyled(shapeId, before, after)` payload the
window wraps in `ShapeStateCommand`. `setSlide` preserves selection by shape
id across rebuilds; `~SlideCanvas` disconnects the scene before item
destruction so no signal escapes teardown. The clipboard codec
(`show_model/shape_json`) is total (images base64, colors, runs) and assigns
fresh shape ids on decode, so paste never collides.

**Consequences.** Insert-style ops, styling and clipboard all share one undo
stream; new ShowWindows are safe to create/destroy in tests and real sessions.

## D-008: Write page setup persists in HTML metas; headers/footers render from the shared page loop

**Context.** M14 added table operations and headers/footers. Two findings:
the `qindawrite-page` meta was written on save but never parsed on load
(HTML reload always reverted to A4), and QTextDocument has no header/footer
concept at all.

**Decision.** `page_setup_io` owns three HTML metas (`qindawrite-page`,
`-header`, `-footer`); `WriteDocument::load` parses the raw source HTML
(ODT/DOCX via the LibreOffice bridge still lose them - documented).
Headers/footers are `PageDecoration` text with `{page}/{pages}` tokens,
drawn by `drawPageDecoration` from THE shared pagination loop
(`renderDocumentPages`), which the screen view also calls - so screen,
print and PDF agree by construction. Two Qt quirks are load-bearing and
recorded as AGENT-NOTEs: `QTextTable::splitCell`'s row/col args are the
spans AFTER splitting (1x1 = full split), and on an empty cell
`firstCursorPosition()` lands on the boundary character where `cellAt()`
answers invalid - table row/column deletion repositions the cursor with a
one-character nudge.

**Consequences.** Save/reload now round-trips page size, margin and
header/footer through HTML; table row/column ops keep the cursor in the
table so follow-up ops and action enablement stay live.

## D-009: QindaNote zoom is a view transform; ink stays in page coordinates

**Context.** M15 added pen width/color, page background choice and zoom.
Strokes are stored in page coordinates with per-sample pressure; text
blocks are child QTextEdits positioned in widget space.

**Decision.** Zoom is purely a view transform: `pageRect`/`growHeight`
scale, events map widget->page coordinates through `toPagePos`, and the
spatial grid is queried in page space (the grid indexes page coordinates
and must not be rebuilt on zoom). Text block geometry derives from the
block index in page coordinates, so zoom repositions blocks without
touching their content; fonts intentionally stay unscaled (documented).
Pen width/color are presentation state on the view (toolbar widgets, not
ActionId commands - they are not document commands), and flow into every
new stroke; the highlighter keeps its fixed translucent yellow.

**Consequences.** Zoom never rewrites the document (no dirty flag, no
persistence impact); inking at any zoom lands identical page coordinates
(test-verified). Shape tools remain the one deliberate boundary: they need
a new envelope block kind and are recorded as future work.

## D-003: App-menu hooks are finalized explicitly after subclass construction

**Context.** `DocumentWindow::createMenus()` runs inside the base
constructor, so its `insertApplicationMenus()` / `insertFileMenuActions()`
calls dispatch to the base no-ops — the subclass vtable does not exist
yet. The contract said "the subclass ctor calls the hooks again", but only
QindaShow did: at runtime QindaWrite, QindaCalc and QindaNote had **no app
menus at all** (no Format/Insert/Page/Sheet), and the template submenu was
never inserted anywhere. Surfaced by the M11 UI-completeness report.

**Decision.** `DocumentWindow::finalizeAppMenuHooks()` (public,
idempotent) runs both hooks against the named `fileMenu`/`aiMenu` anchors.
Every app ctor calls it once its own state exists, and `showEvent` calls
it again as a safety net before the global-menu export snapshots the menu
bar.

**Consequences.** All four apps reliably have their app menus before first
show; a future app that forgets the ctor call still works via the
showEvent net. The two-pass comment in `createMenus()` still describes
why the base pass is a no-op.

## D-004: The page is paper, not chrome — pinned light palette for the document surface

**Context.** `PageTextView` filled the page with `palette().base()` and
let the text control inherit the desktop palette. Under the QindaQt dark
platform theme that made the *page* dark, while .docx imports (LibreOffice
HTML bridge) carry explicit white block backgrounds — the "white boxes
floating on dark" rendering bug. New typed text also came out light-on-dark.

**Decision.** ADR-0115 (stock Qt + platform theme) governs window chrome;
a document page is *paper* and must match the print/PDF path. The view
pins a paper palette (white Base/Window, black Text/WindowText) on the
viewport and the `QWidgetTextControl`; only the canvas between pages
follows the theme. Recorded with an AGENT-NOTE in `page_text_view.cpp`.

**Consequences.** Documents render identically under light and dark
desktop themes and match exported PDFs. Any future editor surface for
paged documents must make the same pin (QindaNote's page view already owns
its paper color).

## D-005: QTextDocument equivalents for word-processor features

**Context.** The M11 formatting toolbar needed column layouts and exposed
two Qt behaviors that leak into the product.

**Decision.** (a) Multi-column text: `QTextDocument` has no native
column-section layout (ODF `text:columns`), so *Insert ▸ Columns* inserts
a borderless 1×N table — the serialization-safe equivalent that
round-trips through HTML/ODT/DOCX (AGENT-NOTE in
`format_controller.cpp`). (b) `QTextDocument::setPageSize()` records an
undo item (Qt 6.11), so every pristine document was born with a phantom
undo step; `WriteDocument` clears the stacks after its initial paged
setup (AGENT-NOTE in `write_document.cpp`).

**Consequences.** Pristine documents have an empty undo stack (Undo
disabled until the first real edit); column layouts survive every export
format. Revisit (a) if Qt ever grows native text columns.

## D-001: Repository directory is `QindaOffice`, not `QindaQt_Office`

**Context.** The implementation plan (§1, §4) names the new repo
`~/work_SPaC3/QindaQt_Office`. The user's prepared workspace is
`~/work_SPaC3/QindaOffice` (this directory), which is where the session and
the writable sandbox root point.

**Decision.** The repository lives in `QindaOffice`. The CMake project
name, desktop IDs, namespace and suite name stay exactly as planned
(`qindaoffice`, `org.qindaqt.Qinda*`, `QindaOffice`, "QindaQt Office").

**Consequences.** The ebuild `EGIT_REPO_URI` points at this checkout. If a
remote is created later it can be named either way; nothing in the build
depends on the directory name.

## D-002: D-Bus hand-off uses raw method calls; QDBusInterface introspection can hang

**Context.** The single-instance hand-off (ported from QindaQt_Apps) used
`QDBusInterface` to call `OpenPaths`. In this development environment the
session bus accepts connections but denies Qt name ownership
(`registerService` returns `ServiceNotRegistered` where `busctl` succeeds),
and the `QDBusInterface` constructor's blocking introspection then parked
the calling thread forever — observed as a main-thread futex wait with no
bus socket ever opened.

**Decision.** `SingleInstance::invokeOpenPaths` builds a raw
`QDBusMessage::createMethodCall` and `QDBusConnection::call(message,
QDBus::Block, 2000)` — no introspection, explicit timeout, fails fast. When
the bus denies registration or the primary is unreachable,
`acquireOrHandOff` returns "not acquired, not handed off" and the runner
falls through to a standalone degraded process (never blocks). The
single-instance test spawns its own private `dbus-daemon` so it is hermetic
and skips with an explicit message where buses are unavailable.

**Consequences.** Degraded D-Bus environments degrade visibly instead of
hanging — the same rule as the global-menu export. QQTerm carries the same
`QDBusInterface` hand-off pattern and may need the same hardening if it ever
runs against such a bus.

## D-010: Office-wide bug-audit hardening (2026-09 audit batch)

**Context.** A full-repo audit (~212 findings) exposed recurring defect
patterns: cross-filesystem save (EXDEV after `QFile::remove` destroyed the
target), undo commands pushed before their mutation, raw element pointers
held into mutating containers, unchecked terminal signal emission, and
atomic-write gaps.

**Decision.** Five durable rules, now enforced in code with AGENT-GUARDs:

1. **Convert-then-save goes through `QSaveFile` at the destination.** Never
   `QFile::remove` + `QFile::rename` from a `QTemporaryDir` (tmpfs) onto the
   user's filesystem — the rename fails with EXDEV after the original is
   gone (`format_io.cpp`, `show_document.cpp`, `notebook.cpp`).
2. **Undo commands are pushed AFTER the mutation.** `QUndoStack::push` runs
   `redo()` synchronously; pushing before the mutation captured a stale
   after-image (redo became a no-op that resurrected old state) and refreshed
   observers against pre-mutation data (`show_window.cpp` slide-list ops,
   drag reorder). Documents that own raw-typed commands clear the undo stack
   whenever the backing model is replaced (`ShowDocument::load/loadRecovery`).
3. **UI holds IDs, never element pointers.** `Notebook`'s vectors reallocate
   on append/load; the navigator and window state carry section/page ids
   resolved through `Notebook::sectionById/pageById` (`note_window.cpp`).
   `SlideStateCommand` resolves its slide by id the same way.
4. **Terminal provider signals go through `ChatRun::emitFinished/emitFailed`.**
   The guards make the documented exactly-once contract (chat_types.h)
   structural; bare `emit finished/failed` in providers is the defect shape
   that double-freed the inline flow's state.
5. **All direct document writes are atomic and error-checked.** HTML/MD/TXT
   saves, fodp saves, notebook index+pages use `QSaveFile` with write/commit
   checks; notebook pages are persisted before their index so a mid-save
   crash never strands the index (violations of "fail honestly, stay
   visible").

**Consequences.** New apps/formats must reuse the same seams
(`writeUtf8Atomically` in format_io, `writeJsonAtomically` in notebook,
`pushSlideListCommand` after mutation). Calc import always finishes with
`Workbook::recalcAll()`; volatile cells are registered inside
`Workbook::installFormula` (after `updateCell`, which clears the flag).
