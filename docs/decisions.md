// SPDX-License-Identifier: GPL-3.0-or-later
# Decisions (ADR-lite)

Durable cross-cutting choices, newest first. Each entry: context, decision,
consequences. For desktop-level architecture see
`container-wm/docs/wiki/adr/`.

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
