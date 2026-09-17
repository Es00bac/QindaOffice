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
| **QindaOffice** | `qindaoffice` | The suite's launcher: a card per app and the recent documents of all four |

Every app carries an AI assistant panel and inline AI actions (M6) that run
through your existing Claude / Codex / Kimi CLI subscriptions or any
OpenAI-/Anthropic-compatible HTTP endpoint, including local models.

**Status: M21.** All four apps are working office applications with their
document engines, editing depth and settings; the launcher is the suite's
front door. See `docs/index.md` for what each part does today, and
`docs/decisions.md` for the durable choices behind it.

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
(`gui-apps/qindaoffice`); it installs the four apps plus the launcher, the
desktop files, icons, and the shared-mime-info package. See `docs/verification.md` for the
on-desktop checklist (containers, panel menu, single instance).

The repository is not published yet, so the ebuild takes a local snapshot
tarball rather than a download: `SRC_URI="${PF}.tar.xz"` with
`RESTRICT="fetch"`. Cut that tarball from the reviewed commit with the
tree's own script and drop it in `DISTDIR`:

```
tools/make-dist.sh 0.1.0_p20260917-r1 /var/cache/distfiles
ebuild .../qindaoffice-0.1.0_p20260917-r1.ebuild manifest
emerge -1 gui-apps/qindaoffice
```

The version passed to `make-dist.sh` is the ebuild's `PVR`, not its `PV`:
a re-cut on the same day is a new revision with different contents, and
naming the archive after `${P}` would give two of them the same distfile
name.

`make-dist.sh` refuses to run on a dirty tree and vendors
`third_party/QXlsx` **into** the tarball — without that, `src_unpack`
would have to fetch the submodule over https, which Portage's default
`FEATURES=network-sandbox` forbids. It also writes a `.dist-commit` file
naming the commit the archive was cut from, so an unpacked or installed
tree can always be traced back. Do not add `MAKEOPTS` or `-j` overrides to
the `emerge`: Portage's own configuration is the job limit.

## License

GPL-3.0-or-later, matching the QindaQt desktop. Vendored `third_party/QXlsx`
is MIT.
