<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaQt Office

The office suite for the [QindaQt](https://github.com/Es00bac/QindaQt)
desktop — four Qt 6 applications that live in ordinary top-level windows,
so the desktop's window containers group them into tabs and splits:

| App | Binary | What it is |
| --- | --- | --- |
| **QindaWrite** | `qindawrite` | Word processor (milestone M4+) |
| **QindaCalc** | `qindacalc` | Spreadsheet (M2/M3) |
| **QindaShow** | `qindashow` | Presentation editor + player (M8/M9) |
| **QindaNote** | `qindanote` | Notes with pen input (M7) |

Every app carries an AI assistant panel and inline AI actions (M6) that run
through your existing Claude / Codex / Kimi CLI subscriptions or any
OpenAI-/Anthropic-compatible HTTP endpoint, including local models.

**Status: M0.** The repository skeleton builds and tests green; the four
apps launch as real QindaQt windows with the shipping menu structure and
per-window global-menu export. Document engines land milestone by milestone
(see `docs/` and the plan referenced in `AGENTS.md`).

## Building

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Requires Qt 6.11 (with private headers), `gui-wm/qindaqt-desktop` (for the
global-menu static archives `office_shell` links), CMake ≥ 3.25, Ninja.
`dev` builds Debug with tests and strict warnings; `release` is Release
without tests. Optional: `-DQQO_WITH_CHARTS=ON` once `dev-qt/qtcharts` is
emerged (used by M10).

## Installing

`packaging/gentoo/` holds the ebuild for the `qindaqt` overlay
(`gui-apps/qindaoffice`); it installs the four binaries, the desktop files,
icons, and the shared-mime-info package. See `docs/verification.md` for the
on-desktop checklist (containers, panel menu, single instance).

## License

GPL-3.0-or-later, matching the QindaQt desktop. Vendored `third_party/QXlsx`
is MIT.
