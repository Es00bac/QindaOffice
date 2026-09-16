<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Decisions (ADR-lite)

Durable cross-cutting choices, newest first. Each entry: context, decision,
consequences. For desktop-level architecture see
`container-wm/docs/wiki/adr/`.

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
