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
| `office_shell` | Qt Widgets/DBus, `gui-wm/qindaqt-desktop` archives | M0: `OfficeApplication`, `DocumentWindow`, `StandardActions`, `GlobalMenuExport` |
| `office_core` | Qt Core/Gui | M1 (planned) |
| `office_ai` | Qt Core/Gui/Network + `office_core` | M6 (planned) |
| `office_ink` | Qt Gui | M7 (planned) |
| `calc_engine` / `calc_io` | Qt Core (+QXlsx) | M2/M3 (planned) |
| `show_model` | Qt Gui + KArchive | M8 (planned) |

## Applications

| App | Engine milestone | Status |
| --- | --- | --- |
| QindaWrite | M4 (page view) / M5 (formats) | M0 skeleton window |
| QindaCalc | M2 (engine) / M3 (UI) | M0 skeleton window |
| QindaShow | M8 (model+editor) / M9 (player) | M0 skeleton window |
| QindaNote | M7 (ink + notes) | M0 skeleton window |
