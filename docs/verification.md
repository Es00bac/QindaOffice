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
- [ ] QindaCalc with a chart open: type into cells — the chart re-renders
      in place; its window does not disappear, move or jump to the front.
