// SPDX-License-Identifier: GPL-3.0-or-later
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

From the M0 acceptance criteria:

- [ ] Launch two `qindawrite` windows; Meta+Shift-drag one onto the other —
      they group into a container as **tabs**.
- [ ] Drag one tab to a screen edge — the container becomes a **split**.
- [ ] The panel shows the **global menu** of the focused window; with the
      export announced, the in-window menu bar is hidden and *View ▸ Show
      Menu Bar* brings it back.
- [ ] `Ctrl+W` / `Ctrl+Q` close the current window only; the process exits
      when its last window closes.
- [ ] The launcher lists QindaWrite / QindaCalc / QindaShow / QindaNote
      under the Office category with the suite icons.

## Milestone-specific gates

Added per milestone in the milestone commit (formula tables, xlsx
round-trips, fodp→pptx conversion, provider smoke tests, pen-input checks,
presenter-view-on-second-screen, reduced-motion, packaging).
