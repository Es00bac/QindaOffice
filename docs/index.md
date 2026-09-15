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
| `office_shell` | Qt Widgets/DBus + `office_core`, `gui-wm/qindaqt-desktop` archives | M0 frame; M1: document host (save/save-as/close consent, recents, drag-drop), `SingleInstance`, `runOfficeApp`, `AppServices` |
| `office_core` | Qt Core/Gui | M1: `Document`, `PlainTextDocument`, `AtomicFileStore`, `JsonSettings`, `RecentFiles`, `RestoreInventory`, `RecoveryJournal`, `LibreOfficeConverter`, `Paths` |
| `office_ai` | Qt Core/Gui/Network + `office_core` | M6 (planned) |
| `office_ink` | Qt Gui | M7 (planned) |
| `calc_engine` | Qt Core | M2: model, lexer/Pratt parser, evaluator, dependency recalc, ~85 functions, number formats, CSV |
| `calc_io` | Qt Core + QXlsx | M3: xlsx read/write, shared-formula reconstruction, LibreOffice gate |
| `show_model` | Qt Gui + KArchive | M8 (planned) |

## Applications

| App | Engine milestone | Status |
| --- | --- | --- |
| QindaWrite | M4/M5 | Working page view: paged editing, WYSIWYG PDF, named styles (M5 formats next) |
| QindaCalc | M2/M3 | Working app: engine-backed grid, in-cell editing, formula bar, sheet tabs, xlsx/csv I/O |
| QindaShow | M8 (model+editor) / M9 (player) | M0 skeleton window |
| QindaNote | M7 (ink + notes) | M0 skeleton window |
