<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaQt Office documentation

One page per library and application lands with its milestone; this index
lists what exists today.

- [Verification](verification.md) — build/test gates and the on-desktop
  manual checklist.
- [Decisions](decisions.md) — ADR-lite log of durable cross-cutting choices.

## Libraries

| Library | Depends on | Status |
| --- | --- | --- |
| `office_shell` | Qt Widgets/DBus + `office_core`, `gui-wm/qindaqt-desktop` archives | M0 frame; M1: document host (save/save-as/close consent, recents, drag-drop), `SingleInstance`, `runOfficeApp`, `AppServices`; M9: `DesktopSettings` (org.qindaqt.Settings1 snapshot, fails closed); M10: `TemplateLoader`, File-menu hook (two-phase, named "fileMenu" anchor); M20: `Preferences` + `PreferencesDialog` (app pages via `addPage`), permanent status field, View chrome toggles, auto-save timer, `fileMenuAnchor()` |
| `office_core` | Qt Core/Gui | M1: `Document`, `PlainTextDocument`, `AtomicFileStore`, `JsonSettings`, `RecentFiles`, `RestoreInventory`, `RecoveryJournal`, `LibreOfficeConverter`, `Paths` |
| `office_ai` | Qt Core/Gui/Network + `office_core` | M6 (planned) |
| `office_ink` | Qt Gui | M7: stroke model, builder filters, spatial grid, renderer, JSON persistence |
| `calc_engine` | Qt Core | M21: `sheet_rules.h` (conditional formatting, data validation, autofilter, cell comments), moved by the structural edits and carried by undo/recovery; M2: model, lexer/Pratt parser, evaluator, dependency recalc, number formats, CSV; M20: `structure.h` (insert/remove rows and columns with formula, name, merge, geometry and chart fixups; sheet rename/delete/move; range sort), `Workbook::reindexAll()`; M10: ~145 functions incl. hyperbolic/trig extras, BASE/DECIMAL/ARABIC, *A variants, percentiles, linear regression (SLOPE/INTERCEPT/CORREL/RSQ/FORECAST), *IFS families, DAYS360/ISOWEEKNUM/YEARFRAC/NETWORKDAYS/WORKDAY, RATE/EFFECT/NOMINAL/FVSCHEDULE/depreciation, ChartSpec model |
| `calc_io` | Qt Core + QXlsx | M3: xlsx read/write, shared-formula reconstruction, LibreOffice gate; M10: chart part export (QXlsx::Chart) |
| `show_model` | Qt Gui + KArchive | M8: presentation model, .fodp writer/reader (shapes, runs, images, notes, builds, transitions), slide renderer, single-shape editor paint; M21: slide masters, per-shape build animations and shape groups (model + fodp qq: round trip + renderer) |

## Applications

| App | Engine milestone | Status |
| --- | --- | --- |
| QindaWrite | M4/M5/M21 | Word processor: paged view, WYSIWYG PDF/print, styles, full I/O matrix (html/md/txt/odt native, docx/rtf via LibreOffice), spell check, tables/images/lists; M10: QAccessibleInterface+QAccessibleTextInterface/EditableText over the paged view (Document role, cursor/selection/text); M20: highlight colour, super/subscript, grow/shrink font, line spacing, Insert hyperlink/symbol/rule/date/time, word count in the status field and Tools ▸ Word Count…; M21: footnotes (marker + margin note, numbered by document order), table of contents with real page numbers, tracked changes (accept/reject), and a ruler with draggable indent markers |
| QindaCalc | M2/M3/M10/M20/M21 | Working app: engine-backed grid, in-cell editing, formula bar, sheet tabs, xlsx/csv I/O; M20: insert/delete rows and columns, sort, merge, borders, wrap, row height/column width (dialog, header drag, autofit), fill down/right, AutoSum, freeze panes, sheet rename/duplicate/delete/reorder, grid and tab context menus, Delete clears, header click selects, Sum/Average/Count in the status field, AI assistant; M21: conditional formatting, data validation, autofilter with a real dropdown, cell comments; M10: Qt Charts floating objects (bar/stacked/line/area/pie/scatter) with a creation dialog and live preview, exported as native xlsx chart parts |
| QindaShow | M8 (model+editor) / M9 (player) / M21 | M8: full editor — canvas over show_model (select/move/resize/in-place text edit), insert text box/rect/ellipse/line/arrow/image, z-order, appear-on-click builds, thumbnail sorter with drag reorder, notes/transition/layout dock, full undo/redo, .fodp native + .odp/.pptx via LibreOffice. M9: `PresentationController` + fullscreen player on the chosen screen (cut/fade/slide/zoom transitions, disabled under accessibility.reducedMotion; pen/laser annotations; B/W blank), presenter view on the second screen (current/next, notes, timer, clock, counter), deck PDF export (slides + notes pages), deck-generation AI (outline parser + panel/inline seams). M21: slide masters, build animations, snapping with guides, grouping, and multi-shape gestures as one undo step. M20: the canvas paints the slide page and fits it to the view, placeholders on a new deck, arrange (z-order extremes, align, distribute, duplicate), move slide up/down, deck-wide Replace, "Slide n of m" status |
| QindaOffice (launcher) | — | The suite's front door: a card per app and the recent documents of all four, merged from their own lists and opened in the app that owns the suffix. Reads the apps' state, never writes it; starts them detached |
| QindaNote | M7/M20/M21/M22 | Working app: notebook directories, page view with pen/highlight/erase/text, growing page, undo, tablet + palm rejection; M20: the page scrolls instead of setting the window's minimum height, a pristine notebook is writable immediately, sections and pages (new/rename/delete/reorder) with a navigator context menu, Edit clipboard commands, notebook-wide Replace, Export PDF, AI assistant, "section — page n of m" status; M21: image blocks and checklists over one shared block layout the screen and the printer both use; M22: filled antialiased ink outlines with pressure width, highlighter at constant width under the pen, live stroke/partial eraser with ring cursor, one QUndoStack, an Ink menu (tool shortcuts P/H/E/T, colour/width/eraser pickers) shared with the icon-only top strip, stylus barrel-button eraser, tool cursors, Ctrl+wheel zoom, debounced page writes, Notes preferences (pressure, smoothing), paper-white page under dark themes |
