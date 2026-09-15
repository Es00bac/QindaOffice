// SPDX-License-Identifier: GPL-3.0-or-later
# QindaQt Office documentation

One page per library and application lands with its milestone; this index
lists what exists today.

- [Verification](verification.md) — build/test gates and the on-desktop
  manual checklist.
- [Decisions](decisions.md) — ADR-lite log of durable cross-cutting choices.

## Libraries

| Library | Depends on | Status |
| --- | --- | --- |
| `office_shell` | Qt Widgets/DBus + `office_core`, `gui-wm/qindaqt-desktop` archives | M0 frame; M1: document host (save/save-as/close consent, recents, drag-drop), `SingleInstance`, `runOfficeApp`, `AppServices`; M9: `DesktopSettings` (org.qindaqt.Settings1 snapshot, fails closed); M10: `TemplateLoader`, File-menu hook (two-phase, named "fileMenu" anchor) |
| `office_core` | Qt Core/Gui | M1: `Document`, `PlainTextDocument`, `AtomicFileStore`, `JsonSettings`, `RecentFiles`, `RestoreInventory`, `RecoveryJournal`, `LibreOfficeConverter`, `Paths` |
| `office_ai` | Qt Core/Gui/Network + `office_core` | M6 (planned) |
| `office_ink` | Qt Gui | M7: stroke model, builder filters, spatial grid, renderer, JSON persistence |
| `calc_engine` | Qt Core | M2: model, lexer/Pratt parser, evaluator, dependency recalc, number formats, CSV; M10: ~145 functions incl. hyperbolic/trig extras, BASE/DECIMAL/ARABIC, *A variants, percentiles, linear regression (SLOPE/INTERCEPT/CORREL/RSQ/FORECAST), *IFS families, DAYS360/ISOWEEKNUM/YEARFRAC/NETWORKDAYS/WORKDAY, RATE/EFFECT/NOMINAL/FVSCHEDULE/depreciation, ChartSpec model |
| `calc_io` | Qt Core + QXlsx | M3: xlsx read/write, shared-formula reconstruction, LibreOffice gate; M10: chart part export (QXlsx::Chart) |
| `show_model` | Qt Gui + KArchive | M8: presentation model, .fodp writer/reader (shapes, runs, images, notes, builds, transitions), slide renderer, single-shape editor paint |

## Applications

| App | Engine milestone | Status |
| --- | --- | --- |
| QindaWrite | M4/M5 | Word processor: paged view, WYSIWYG PDF/print, styles, full I/O matrix (html/md/txt/odt native, docx/rtf via LibreOffice), spell check, tables/images/lists; M10: QAccessibleInterface+QAccessibleTextInterface/EditableText over the paged view (Document role, cursor/selection/text) |
| QindaCalc | M2/M3/M10 | Working app: engine-backed grid, in-cell editing, formula bar, sheet tabs, xlsx/csv I/O; M10: Qt Charts floating objects (bar/stacked/line/area/pie/scatter) with a creation dialog and live preview, exported as native xlsx chart parts |
| QindaShow | M8 (model+editor) / M9 (player) | M8: full editor — canvas over show_model (select/move/resize/in-place text edit), insert text box/rect/ellipse/line/arrow/image, z-order, appear-on-click builds, thumbnail sorter with drag reorder, notes/transition/layout dock, full undo/redo, .fodp native + .odp/.pptx via LibreOffice. M9: `PresentationController` + fullscreen player on the chosen screen (cut/fade/slide/zoom transitions, disabled under accessibility.reducedMotion; pen/laser annotations; B/W blank), presenter view on the second screen (current/next, notes, timer, clock, counter), deck PDF export (slides + notes pages), deck-generation AI (outline parser + panel/inline seams) |
| QindaNote | M7 | Working app: notebook directories, page view with pen/highlight/erase/text, growing page, undo, tablet + palm rejection |
