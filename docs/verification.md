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
      builds and installs all four apps + desktop files + icons + MIME.

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
