<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Verification

## Build gates (every milestone)

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Widget tests run under `QT_QPA_PLATFORM=offscreen` (pinned via ctest
`ENVIRONMENT`; each test's `main` also sets it defensively).

## On-desktop manual checklist (run inside a QindaQt session)

From the M0 acceptance criteria. **Verified 2026-09-14** on the live QindaQt
session (compositor queried over `org.qindaqt.Compositor1`; evidence in git
history):

- [x] Windows map as ordinary top-levels with the right identity:
      `applicationId=org.qindaqt.QindaWrite`, title `Untitled — QindaWrite`.
- [x] One process owns `org.qindaqt.QindaWrite` on the session bus; a second
      launch hands off over D-Bus and (with no paths) opens a second
      ordinary window in the primary process.
- [x] Global menu: each window exports a live `com.canonical.dbusmenu`
      endpoint at `/org/qindaqt/QindaOffice/QindaWrite/Menu/<n>` on the
      app's bus connection (identical announcement mechanism to QQTerm).
- [x] `gtk-launch org.qindaqt.QindaWrite` launches the Portage-installed
      binary (launcher path end-to-end).
- [x] Packaging: `emerge -1 gui-apps/qindaoffice` from the `qindaqt` overlay
      builds and installs all four apps + desktop files + icons + MIME
      (M10 acceptance; +charts USE flag drives QQO_WITH_CHARTS and pulls
      dev-qt/qtcharts).
- [x] M10 gates: 385-row function oracle green; chart part lands in the
      xlsx package (series range + title in chart XML); the paged view
      answers QAccessible queries (Document role, cursor/selection);
      TemplateLoader surfaces the bundled starters and a picked template
      opens detached from its path.

Still interactive-only (pointer/focus on a busy live desktop; the
app-side requirements above are the parts the suite owns):

- [ ] Meta+Shift-drag the two windows together — they group as **tabs**;
      drag a tab to a screen edge for a **split** (compositor-side; the
      desktop's own `compositor.hybrid-pointer-nested` harness covers the
      gesture).
- [ ] Eyeball the panel menu against the focused window, and the launcher
      Office category with the suite icons.
- [ ] Dirty a document, `Ctrl+W` — Save/Discard/Cancel consent; kill -9
      mid-edit and relaunch — the Recover dialog offers the journal entry.

## Milestone-specific gates

Added per milestone in the milestone commit (formula tables, xlsx
round-trips, fodp→pptx conversion, provider smoke tests, pen-input checks,
presenter-view-on-second-screen, reduced-motion, packaging).

### M22 pen gates (automated: `tst_stroke_edit`, `tst_note_input`, `tst_note_ui`)

- office_ink: a pen outline follows its pressure width with round caps;
  the highlighter outline ignores pressure (constant width, flat caps);
  the stroke-mode eraser removes whole strokes and reports misses (no
  phantom undo step); the partial eraser splits a stroke at the gap and
  drops fragments under two samples; a pen stroke crossing a highlighter
  reads as the pen colour (highlighter under ink, one translucent fill).
- App: the eraser acts DURING the drag (stroke gone before release); one
  undo step per eraser gesture and none for an empty swipe; a held barrel
  MiddleButton borrows the eraser and reverts on release; a 20-sample
  stroke repaints only segment-sized rects (never the whole page); the
  page stays white under a dark palette (D-004 pin); P/H/E switch tools
  and type into a focused find field instead of switching; three quick
  edits coalesce into one debounced page write; the strip is icon-only
  for the shipped glyphs and keeps every command visible squeezed to a
  third of the window width.

Manual (QindaQt desktop, tablet):

- [ ] Pressure looks right (thin at a light touch, swelling under
      pressure); Settings ▸ Preferences ▸ Notes ▸ "Pen pressure changes
      width" off → uniform width; smoothing Off/Light/Strong felt.
- [ ] No lag at 5 000 strokes; a palm resting on the screen does nothing.
- [ ] Highlighter sits under pen ink; the eraser ring cursor tracks
      radius × zoom; the stylus eraser end and the barrel button erase
      while held and revert on release.
- [ ] Dark theme: the page is white, ink readable; strip icons legible at
      16 px on light and dark toolbars.
- [ ] Single-letter shortcuts switch tools; with the navigator TREE
      focused a letter switches tools instead of keyboard-searching (the
      tree does not take ShortcutOverride — accepted, D-026); Esc returns
      to Pen; Ctrl+wheel and the zoom combo agree.

### M11 UI completeness (automated: `tst_write_ui`)

- Main + formatting toolbars exist with persistent named actions
  (objectNames from the shell's `ActionId` table); Format/Insert menus
  exist after `finalizeAppMenuHooks()` and share the same action objects.
- Bold/italic/underline/strikethrough toggle and re-read from the cursor;
  font family/size combos apply and sync; alignment, bullet/numbered
  lists (toggle + convert), indent clamp at 0.
- Insert Table (2x3 bordered) and Insert Columns (1xN borderless) land as
  real `QTextTable`s; find wraps and reports, replace-all counts.
- Edit actions follow control availability; a pristine document has zero
  undo steps (Qt `setPageSize` undo quirk cleared); zoom actions move the
  view; the paper palette stays white-on-dark under any theme.

### M15 additions (automated: `tst_qindanote`)

- Pen width (1-24 px) and pen color flow into newly inked strokes
  (mouse-fallback inking verified stroke width/color).
- Page background choice (blank/ruled/grid/dotted) edits the envelope and
  reports pageEdited.
- Zoom (50-200%) scales the page rect and repositions text blocks while
  inked coordinates stay in page space (identical samples at 1x and 2x).

### M14 additions (automated: `tst_write_ui`, `tst_write_view`)

- Table menu + toolbar section: row/column insert+delete, merge, split -
  all persistent named actions enabled only inside a table; deletion keeps
  the cursor in the table.
- Page Setup dialog edits header/footer ({page}/{pages} tokens); the
  exported PDF carries header and per-page footer text on every page
  (poppler-verified); HTML save/reload round-trips page size, margin and
  header/footer metas.

### M13 additions (automated: `tst_show_ui`, `tst_show_model`)

- Shapes toolbar + Insert/Shape menus share persistent named actions
  (objectNames from the shell ActionId table).
- Bold/italic/fill/stroke mutate through the canvas with undoable
  before/after payloads; two undos walk a fill then a bold back.
- The shape clipboard round-trips through the JSON codec with a fresh id
  and a +12 pt offset; cut removes the source; paste selects the new shape.
- Insert actions land shapes through the normal undo path; editing actions
  follow selection and clipboard shape; Select All selects every shape.

### M12 additions (automated: `tst_calc_ui`)

- Undo/redo restores values AND re-evaluates formulas (dependency index
  rebuilt by the snapshot restore); sheet insert is undoable and the tab
  bar follows `structureChanged`.
- Rich clipboard round-trips formulas exactly; plain TSV pastes numbers,
  booleans, text and formulas; cut clears in one undo step.
- Bold/italic/underline, alignment, font family/size, text/fill colors and
  the number-format combo write `CellStyle`s through one-undo-step
  `StylePatch`es; the UI state re-reads the cursor cell.
- The find bar walks matches row-major with wrap; F3/Shift+F3 drive it.

### M11 on-desktop checklist (interactive)

- [ ] Under the QindaQt dark theme a .docx opens as a white page with dark
      text (no "white boxes on dark"), matching PDF export.
- [ ] Toolbar icons resolve from the Breeze theme (offscreen runs show the
      text fallback by design); `Ctrl+F` opens the find bar, Esc closes it.

### M1 additions (automated)

- `tst_single_instance` — two helper processes over a private session bus:
  primary acquires, secondary hands off and exits 0. Skips with an explicit
  message when the environment cannot spawn a bus or denies Qt name
  ownership (some sandboxed/elevated contexts).
- `tst_libre_office_converter` — live txt→pdf conversion against the
  installed soffice; skipped when the conversion pipeline is unavailable.
- `tst_office_application` — canonical-path uniqueness (duplicate open
  activates), pristine-untitled reuse, recents bookkeeping, 32-window bound.
- `tst_recovery_journal` / `tst_restore_inventory` / `tst_recent_files` /
  `tst_atomic_file_store` / `tst_plain_text_document` — store bounds,
  debounce, round trips, optimistic-revision inputs.

### M1 manual checklist (live desktop)

- [ ] Open a file from the file manager: it lands in a new ordinary window
      (or a pristine untitled one); opening the same file twice activates
      the existing window.
- [ ] Edit, close: Save/Discard/Cancel consent appears; Discard clears the
      recovery entry; Save clears it after writing.
- [ ] Kill the app mid-edit; relaunch: a Recover dialog offers the unsaved
      document; Restore brings it back dirty, Discard forgets it.
- [ ] Settings ▸ Restore Previous Documents (off by default); with it on,
      the previous session's paths reopen at launch.
- [ ] Second launch (`qindawrite file`) from a terminal while one instance
      runs: paths hand off over D-Bus; `--new-instance` bypasses.

### M16 shell-reachability audit

Automated by `tst_document_window`, `tst_calc_window`, `tst_recovery_journal`
and `tst_office_ai`.

Rationale: the milestone suites drive documents directly, so a command can
be fully wired and still be unreachable from the UI. This pass dumps every
window-scoped `QAction` of the four real windows under
`QT_QPA_PLATFORM=offscreen` (enabled state, objectName, shortcut) and
asserts the reachability contract. See D-011.

- A document installed through `setDocumentHost()` enables Save and Save
  As; before this pass they were disabled in all four apps, so File ▸ Save,
  the toolbar Save button and Ctrl+S did nothing.
- Close Window is `Ctrl+W` and Quit is `Ctrl+Q` (AGENTS.md rule 1), asserted
  as literal chords — `QKeySequence::Close`/`::Quit` resolve to Ctrl+F4 and
  the "Exit" media key under the keyboard scheme the qindaqt platform theme
  yields.
- Every menu action in QindaWrite/QindaCalc/QindaShow/QindaNote carries a
  non-empty objectName (rule 5); the QindaNote tool strip has the toolbar
  objectName `QMainWindow::saveState()` requires.
- QindaWrite has exactly one `&AI` menu (the inline actions live in the
  shell's AI menu next to Assistant…, as in QindaShow).
- An empty `recoverySnapshot()` is not written as a journal entry, so the
  stub engines (QindaCalc, QindaNote) cannot produce a Recover prompt whose
  restore always fails.
- `ModelCatalog::configFor()` resolves every id `modelIds()` hands out, and
  a corrupt `providers.json` is kept and reported instead of being replaced
  by the seed.
- QindaCalc: undo back to the saved position clears the dirty mark (dirty
  follows the `QUndoStack` clean index); chart insert is one undo command.

### M17 capability completeness

Automated by `tst_qindanote`, `tst_calc_window`, `tst_calc_io` and
`tst_office_ai`. Rationale in D-012: a declared capability that is a stub is
a defect, and the stubs were hiding a data-loss bug. Live-LibreOffice cases
skip with a message where `soffice` is unavailable.

- QindaNote: saving a notebook after opening ONE page leaves every other
  page's file intact (`save()` writes only loaded pages; Save As
  materialises unloaded ones from the source first).
- QindaNote: Edit ▸ Redo replays an undone stroke, follows the view's real
  history depth, and a fresh edit ends the redo branch.
- QindaNote: `recoverySnapshot()` round-trips sections, page titles, ink and
  page background; an untitled notebook recovers entirely from memory, a
  saved one re-attaches to its directory so unopened pages stay on disk.
- QindaCalc: `recoverySnapshot()` round-trips formulas (re-parsed and
  recomputed), typed values (the text `007` stays text), styles set on EMPTY
  cells, extra sheets, named ranges and chart specs.
- QindaCalc: an `.ods` saves and reopens through the LibreOffice bridge with
  its formula recomputed; `mimeTypes()` matches the `.desktop` MimeType list.
- calc_io: a named range survives the xlsx round trip, and `=SUM(NAME)` still
  evaluates after import (names are read before the cells, so the dependency
  index sees them).
- office_ai: `configFor()` resolves every id `modelIds()` hands out; a
  corrupt `providers.json` is moved to `.corrupt` and the picker still works.

### M21 the next tier, and the suite's launcher

Automated by `tst_calc_rules`, `tst_calc_rules_ui`, `tst_write_review`,
`tst_show_design`, `tst_note_blocks`, `tst_launcher` and the pristine-window
cases now in all four app suites. Rationale in D-018b (open reuse), D-019
(Calc rules), D-020 (Write document features), D-021 (Show design layer),
D-022 (Note blocks) and D-023 (the launcher).

**Opening.** A freshly created window of every app is pristine, so File ▸
Open replaces its blank document instead of opening beside it; the window
the Open came FROM is preferred; a window that has been typed into is never
displaced.

**QindaCalc.** A blank cell satisfies no comparison and an empty input is
never refused. Two conditional formats compose field by field. A refused
value emits `inputRejected` rather than vanishing. A filtered row leaves
the grid entirely (the row below moves up into its place) and the header
row is never hidden. Comments survive undo AND the recovery payload. All
four rule kinds follow an inserted row.

**QindaWrite.** Footnote markers number by document order and renumber when
one is inserted in the middle; a note prints on its marker's page; an
orphaned note is pruned. The contents lists headings with the page numbers
the view paints, never lists itself, and Update replaces the table in
place. A tracked insertion is rejectable and a tracked deletion is
acceptable, exactly inverse. The ruler's markers describe the cursor's
paragraph. Enter after a heading starts body text, and a Heading 1 at the
very start does not push a blank page ahead of itself.

**QindaShow.** A master really paints the background and supplies defaults
only to runs that never chose. A half-played animation draws something that
is neither the background nor the final fill. Snapping takes a candidate
inside the threshold and nothing outside it, and does nothing when off. A
group survives the fodp round trip. A multi-shape gesture is one undo step.

**QindaNote.** Three kinds of block stack without overlapping, each as tall
as its kind needs. A checklist item's text and tick reach the envelope. An
image block carries its bytes, not a path.

**Launcher.** Four distinct apps with distinct binaries and desktop ids;
every suffix routes to the app that opens it and a suffix-less path to
QindaNote; recents merge the four lists, skip files that are gone, and
survive a `recent.json` that is not JSON without rewriting it; each card
carries an accessible name and description.

### M21 packaging gate (Gentoo)

The ebuild is the only supported install path, and it must build with no
network access at all — Portage's default `FEATURES` include
`network-sandbox`. Verify before tagging a snapshot:

- [ ] `tools/make-dist.sh <version> <distdir>` exits non-zero on a dirty
      tree. A tarball that matches no commit is a tarball nobody can
      reproduce.
- [ ] The tarball contains `third_party/QXlsx/QXlsx/CMakeLists.txt`. The
      submodule must be VENDORED, not referenced: `src_unpack` cannot fetch
      it under `network-sandbox`.
- [ ] `ebuild <ebuild> clean unpack` leaves the tree at `${WORKDIR}/${P}`
      and its `.dist-commit` names the commit the archive was cut from.
- [ ] `emerge -1 gui-apps/qindaoffice` runs with **no** `MAKEOPTS` or `-j`
      override; Portage's configured job and load limits are the limits.
- [ ] After merge, all five binaries exist (`qindawrite`, `qindacalc`,
      `qindashow`, `qindanote`, `qindaoffice`), and the five desktop files
      and the hicolor SVGs are installed.
- [ ] `packaging/gentoo/gui-apps/qindaoffice/` and the `qindaqt` overlay
      hold the SAME ebuild. The in-repo copy is the source; a drift means
      the overlay is packaging something the tree does not describe.
- [ ] The install image contains **no** `.a`, `.so*`, `.h` or `.cmake`
      files (D-025). Check against an eclass-like configure, not the dev
      preset — `cmake.eclass` forces `BUILD_SHARED_LIBS=ON`, so a build
      that is static locally can still ship a shared library under
      Portage:

      ```
      cmake -S . -B /tmp/ic -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64
      cmake --build /tmp/ic && DESTDIR=/tmp/img cmake --install /tmp/ic
      find /tmp/img \( -name '*.a' -o -name '*.so*' -o -name '*.h' \
                     -o -name '*.cmake' \) | wc -l     # must be 0
      ```
- [ ] `ldd` on the installed `qindacalc` names no `libQXlsxQt6`: the
      vendored copy must be linked in, not planted in `/usr/lib64`.
- [ ] Two machines cutting the SAME commit produce byte-identical
      archives. The Manifest pins one hash and `pkg_nofetch` tells the
      operator to run `make-dist.sh`, so a non-reproducible tarball means
      the second machine's own archive is rejected as a Manifest
      mismatch:

      ```
      sha512sum <distdir>/qindaoffice-<pvr>.tar.xz          # on each host
      grep SHA512 .../gui-apps/qindaoffice/Manifest
      ```

      A plain `tar -cJf` does NOT satisfy this — readdir order, the
      creation times of `.dist-commit` and the staging directories, the
      embedded uid/gid, and multi-threaded xz all vary per machine.

### M21 manual checklist (live desktop)

- [ ] `gtk-launch org.qindaqt.QindaOffice` opens the launcher with four
      mascots; clicking a card starts that app, and closing the launcher
      leaves it running.
- [ ] QindaCalc: colour a range by rule, put a list validation on a column
      and try to type something else, filter a header row, hover a
      commented cell.
- [ ] QindaWrite: insert two footnotes and check the numbers on the page
      and in the PDF; build a contents list and re-paginate; record a few
      changes and accept half of them; drag the ruler's indent markers.
- [ ] QindaShow: give the deck a master, animate a bullet and play it,
      drag a shape near the slide centre and watch the guide, group two
      shapes and move them.
- [ ] QindaNote: add a checklist and an image, then print — the page prints
      what the screen showed, in the same places.

### M20 office-suite completeness

Automated by `tst_preferences`, `tst_calc_structure`, `tst_calc_ui`,
`tst_write_ui`, `tst_write_typography`, `tst_show_ui` and `tst_qindanote`. Rationale in D-014
(settings, status bar, dead entries), D-015 (QindaCalc structure), D-016
(QindaShow canvas and arrange), D-017 (QindaNote structure and layout) and
D-018 (QindaWrite typography).

**Shell.** Settings ▸ Preferences… is enabled and opens a dialog whose OK
writes through and whose Cancel does not; the `Preferences` store bounds
what it reads back and survives a reopen. Every window has a status bar from
construction with a PERMANENT right-hand field a transient `showMessage()`
cannot displace. View ▸ Show Toolbars / Show Status Bar apply and persist.
The File menu ends with Quit however many app entries were inserted.
Auto-save is off by default and, when on, skips (with a status message)
rather than putting a dialog on screen for a document with no path or a file
changed on disk.

**calc_engine.** Inserting rows moves cells AND rewrites every formula that
points at them — including cross-sheet ones that name the edited sheet — and
a straddling range grows with the insert; deleting turns a reference into
the gap into `#REF!` while a straddling range shrinks; styles on EMPTY
cells, explicit geometry, merges, named ranges and chart anchors all follow.
Renaming a sheet requalifies every reference to it; deleting one breaks only
its own references and leaves the rest working after the index rebuild; the
last sheet cannot be deleted. Sort moves rows with their formulas (relative
references shift by the row delta), keeps the header row and sinks blanks in
both directions. `recalcAll()` refreshes cells off the diagonal (it used to
read the packed key transposed and skip them).

**QindaCalc.** The grid starts on A1, not on a default-constructed
`{-1,-1}`; the status field reads the address, Sum, Average and Count of the
selection. Delete clears the selection in one undo step. A header click
selects the whole row or column bounded by the used range. Insert/Delete
Rows are one undo step each and the dependents re-evaluate. Sort Ascending
keeps a text header on top. AutoSum totals the contiguous run above the
cursor. Merge works from the selection and Unmerge from any cell the merge
covers. A sheet rename keeps cross-sheet formulas working. With a pane
frozen, row 1 stays visible after scrolling to the bottom and still
hit-tests as row 1 — painting and clicking walk the same band lists.

**QindaWrite.** The font-size combo is never blank. Superscript reaches the
selection's char format; line spacing writes `ProportionalHeight` and the
menu's check mark re-reads it from the document. The word count follows the
document and narrows to the selection, and appears in the status field.
Insert Date puts text in and Insert Horizontal Rule leaves a block carrying
`BlockTrailingHorizontalRulerWidth` (so it survives the HTML round trip).

**QindaShow.** The canvas paints a white page (an empty slide used to render
as a flat dark-grey view). Align Centre with one shape selected centres it
on the SLIDE and is one undo step. Bring to Front is one command whatever
the distance. Move Slide Up reorders the deck and follows the selection.
A deck-wide Replace is one undo step however many slides it touched.

**QindaNote.** A fresh window opens on a real, writable page and says so in
the status field. The page view no longer forces the window tall — a 900x500
window stays 900x500. Deleting a page removes its file as well as its index
entry. Move Page reorders within the section and refuses past either end.

### M19 the rest of the standard commands

Automated by `tst_calc_ui`, `tst_show_ui` and `tst_qindanote`. Print,
Print Preview, View ▸ Zoom and Edit ▸ Find/Replace were created disabled by
`createStandardActions()` and wired only in QindaWrite, so in the other three
apps they were menu entries the app did not implement.

- QindaShow prints through the same pure `renderSlide` the PDF export uses;
  QindaNote through `note_page_render.cpp`, a page renderer with no widget or
  palette in it, covering the whole notebook (pages never opened are read
  from disk first); QindaCalc paginates its used range by rows, never
  splitting a row across pages.
- Zoom works in all four and is presentation state only — it never touches
  stored column widths, row heights or the slide model, so it cannot dirty a
  document. QindaCalc's grid geometry and hit-testing both go through
  `colWidthPx()`/`rowHeightPx()`, which is what keeps painting and clicking
  agreeing at any zoom.
- QindaCalc's find bar gained Replace and Replace All. Replace edits the
  cell INPUT, never the formatted display text, and a whole Replace All
  sweep is ONE undo step however many cells it touched.
- QindaShow's find searches slide titles, shape text and speaker notes and
  selects the matching slide. QindaNote's searches page titles and typed
  text blocks, reading pages that were never opened first — a search that
  skipped them would report "not found" for text that is really there.

### M18 toolbar reachability

Automated by `tst_calc_ui` and `tst_write_ui`. Rationale in D-013: a command
behind an extension chevron is a command the user never sees.

- Squeezing the window to a third of its width grows the format strip a row
  instead of dropping commands; every strip command still has a visible
  button inside the strip (QindaCalc: 8 commands; QindaWrite: 16+).
- Every strip command carries a non-empty objectName.
- The cursor outline is painted on the cursor cell, not one header offset
  down and to the right of it.

### M20 manual checklist (live desktop)

- [ ] Settings ▸ Preferences…: set a document font and a default zoom; a
      newly opened window of the same app uses both. Turn off Show Toolbars
      and open a second window — its toolbars are off too.
- [ ] Turn auto-save on at 1 minute, edit a saved document and wait: the
      status bar reports the auto-save and the window title loses its dirty
      mark, with no dialog at any point.
- [ ] QindaCalc: right-click the grid and the sheet tab; drag a column
      boundary and double-click it; freeze at B2 and scroll — the first row
      and column stay put; select a numeric block and read Sum/Average/Count.
- [ ] QindaShow: the slide is a white page centred in the canvas at any
      window size, and the current slide's thumbnail shows the slide (not a
      blue rectangle).
- [ ] QindaNote: shrink the window to half the screen height — the page
      scrolls instead of the window refusing to shrink.
- [ ] Panel global menu: activate the new commands by objectName (Insert
      Rows, Sort Ascending, Bring to Front, New Section, Word Count).

### M16 manual checklist (live desktop)

- [ ] `Ctrl+S` saves in each of the four apps; `Ctrl+W` closes the window
      and `Ctrl+Q` closes the current window only.
- [ ] Panel global menu: activate *New from Template*, *Export PDF*,
      *Bring forward* and the AI inline actions from the panel — each
      resolves by objectName instead of being inert.
- [ ] Launcher and task switcher show the suite icons (the hicolor SVGs are
      installed by `make install` / the ebuild) — four mascots, one family.
- [ ] QindaCalc: type a name into the Name Box with a range selected — it
      defines the name; typing it again jumps back to that range; Ctrl+Z
      removes the definition.
- [ ] QindaNote: open a multi-page notebook, view one page, Ctrl+S, reopen —
      the pages you never opened still have their ink.
- [ ] Formatting strips show ICONS, not words: the suite's own
      hicolor/scalable/actions set resolves under the QindaQt icon theme
      (which inherits hicolor and carries none of the format family).
- [ ] Narrow any app window until the strip wraps — no "»" chevron appears
      and no command disappears.
- [ ] QindaCalc with a chart open: type into cells — the chart re-renders
      in place; its window does not disappear, move or jump to the front.
