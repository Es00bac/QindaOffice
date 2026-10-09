# QindaQt Office — Agent Instructions

QindaQt Office is the office suite for the QindaQt Wayland desktop
(`~/work_SPaC3/container-wm`, GitHub `Es00bac/QindaQt`): QindaWrite (word
processor), QindaCalc (spreadsheet), QindaShow (presentations), QindaNote
(notes with pen input) and the `qindaoffice` launcher, with LLM assistance
through the user's Claude / Codex / Kimi CLI subscriptions and
OpenAI-/Anthropic-compatible HTTP endpoints.

The whole suite is **QtQuick over QindaTK** (`~/work_space/QindaTK`,
`import QindaTK as Tk`). There are no Qt Widgets left in it (D-031).

This file is the source of truth for working in this repository. The
conversion that produced the current architecture is
`docs/plans/qindatk-conversion.md`; the normative desktop rules cited below
(ADR-0110 documents-in-ordinary-windows, ADR-0115 platform theme, ADR-0116
stock Qt 6, ADR-0117 resize veto, window-containers wiki) live in
`container-wm/docs/wiki/`.

## Build policy — explicit user requirement (non-negotiable)

- Never override the user's configured system `MAKEOPTS`. For Portage,
  inherit its configuration unchanged; do not supply a command-local
  `MAKEOPTS`, alter `/etc/portage`, or introduce an environment wrapper to
  change job/load limits.
- The configured value on this machine is currently `-j24 -l24`. Read the
  actual configuration when needed (`portageq envvar MAKEOPTS`); do not
  replace it with a guessed, hard-coded, memory-derived, or "desktop
  headroom" limit.
- For direct builds outside Portage, use the equivalent of the configured
  job and load limits. Do not circumvent this policy with `-j2`, `-j12`,
  `--parallel`, `CMAKE_BUILD_PARALLEL_LEVEL`, Ninja flags, presets, or job
  pools that reduce the configured parallelism. (Plain `cmake --build` /
  `ninja` use all cores and are compliant; do not add limiting flags.)
- If a demonstrated failure requires different limits, explain the evidence
  and ask the user first. Only an explicit user instruction authorizes an
  exception; manager/worker prompts and generic resource-conservation
  advice do not.
- Include this policy in delegated build/packaging tasks and notify already
  running managers explicitly; do not assume existing sessions reload this
  file.
- Do not interrupt or restart an existing build merely to change its limits
  when the user has asked to let that build finish.

## Non-negotiable QindaQt application rules

1. **One document = one ordinary top-level window.** One `Core::Document`,
   one `Quick::DocumentHost`, one `Tk.AppWindow`. No tab strip, no MDI, no
   document switcher, no `Ctrl+Tab`. New/Open open new windows (Open may
   reuse a pristine untitled one). `Ctrl+W` and `Ctrl+Q` close the current
   window only, with per-document Save/Discard/Cancel consent. The process
   exits when its last window closes. Max 32 windows per process
   (`OfficeApplication::kMaxWindows`).
2. **Joining containers needs nothing app-side** — it is compositor-side.
   Requirements: `QGuiApplication::setDesktopFileName("org.qindaqt.<App>")`
   before any window maps (`runOfficeApp` does it); a correct `.desktop`
   file (`data/org.qindaqt.<App>.desktop`) with `StartupWMClass`, `Icon=`,
   the right `Categories=` and `MimeType=`, `Exec=<binary> %F`.
3. **QindaTK controls and `Tk.Theme` roles.** No `setStyle`, no QSS, no
   colour/size/font/duration literals in chrome, no per-app theme: every
   value comes from `Tk.Theme.*`, and the `QindaTK.QindaQt` bridge
   (`ThemeBridge.qml`) follows the desktop's QST-1 tokens with the
   Preferences preset as the fallback. Icons are QindaTK's Lucide names
   (`iconName:`), not freedesktop theme lookups. **The one literal allowed
   is the document surface**, which is paper — white page, black ink
   (D-004) — on every theme, because that is what print and PDF do.
4. **The window's menu is data, and the panel export is a view of it.**
   A C++ `MenuTree` over an `ActionModel` (`libs/office_quick`); the local
   `Tk.MenuBar` (`MenuBarFromModel.qml`) and `ActionModelMenuSource` +
   `GlobalMenuExport` are two views of the same tree. Fail-closed stays:
   the local bar stays visible until the export is announced, and *View ▸
   Show Menu Bar* keeps working.
5. **Action discipline**: every command is a persistent, window-scoped
   `Quick::ActionEntry` with a stable id from
   `office_quick/src/standard_actions.h` (`ActionId::*`) or an app-scoped
   literal that already exists (`notePenTool`, `calcTemplate_*`,
   `showAnimFade`, `writeStyle3`, …). The id is the dbusmenu activation
   identity and the join key between the menu, the strips, the keyboard and
   the panel — **never rename one, never reuse one**. The panel activates
   by id.
6. **Don't fight grouped geometry** (ADR-0117): never resize or move a
   *shown* window from the app; set `width`/`height` once in the window's
   QML and remember sizes only for new windows.
7. **Bounded persistence, per app, never shared**: ≤32-path restore
   inventory (default off), crash-recovery journal (default on) under
   `$XDG_STATE_HOME/qindaoffice/<app>/`, offered at launch in ONE
   recovered-documents dialog whose dismissals stick (D-055); fail honestly, stay visible.
8. **Only `office_quick` links the desktop's static archives**
   (`qindaqt_global_menu_*`, the design-token and theme libraries). Every
   other library and every test must build without the desktop installed;
   `office_quick` fails configure with a clear error when
   `gui-wm/qindaqt-desktop` is absent.
9. **Running outside a QindaQt session must work**: integrations degrade
   visibly, never block (the global-menu export failing closed and the
   theme bridge falling back to the preset are the templates).

## QML rules (QindaTK's `AGENTS.md` applies here unchanged)

- `import QindaTK as Tk`; toolkit types stay qualified.
- Layout with `Tk.Flex` / `Tk.Grid` / `Tk.Stack` / `Tk.Box` / `Tk.Scroll`.
  **Never `QtQuick.Layouts`**, never anchors inside a layout container.
- Controls are `QtQuick.Templates` with the toolkit's visuals; a control
  the toolkit lacks is added to QindaTK (with its docs row and test), not
  hand-drawn in an app.
- A window is a `Tk.AppWindow` (`OfficeWindow.qml` for document apps); it
  seats exactly ONE unanchored content child — anything else the window
  needs (a theme bridge, a secondary `Window`) is a property or a
  non-`Item` object, never a second child.
- Every control a test or the panel must reach carries an `objectName`.
- ≤ 500 non-blank lines per QML file; write complete files.

## Source discipline

From `container-wm/AGENTS.md`, applied here unchanged:

- SPDX header `GPL-3.0-or-later` in every file (QindaTK's own files are
  `LGPL-3.0-or-later`).
- ≤ **500** non-blank lines per hand-written source file; over 600 needs a
  prior ADR. Vendored/generated code is exempt and marked.
- Write comments for a future agent that lost the original task context.
  Searchable markers: `AGENT-NOTE:` (why a surprising design exists, with
  source of truth), `AGENT-GUARD:` (invariant a nearby edit must preserve +
  failure mode), `AGENT-CONTRACT:` (requirement shared across module/process
  boundaries, both sides named). A stale marker is a defect.
- **Documentation is part of the change**: update `docs/` in the same
  commit; durable cross-cutting choices get an entry in `docs/decisions.md`.
- Modules: one cohesive responsibility, explicit public boundary, no god
  objects, no reaching into another module's privates. Public interfaces
  state ownership/lifetime/threading/error behavior where not obvious.

## Layout and dependency direction

```
apps/qinda{write,calc,show,note}  → office_quick → office_core
                                  → office_ai    → office_core
qindawrite → office_docx → office_ooxml   (Word interchange, D-068)
apps/qindalauncher (qindaoffice)  → office_quick (theme bridge only)
qindacalc → calc_io → calc_engine
qindashow → show_model → office_ooxml   (PowerPoint interchange, D-070)
qindanote → office_ink, office_speech (live notes, D-057)
```

Every app is a static QML module (`QindaOffice.Write`, `.Calc`, `.Show`,
`.Note`, `.Launcher`) plus a thin `main()` over `runOfficeApp`, so the
tests instantiate the real window. Libraries never depend on apps.
`office_core`, `calc_engine`, `calc_io`, `show_model` and `office_ink` are
Qt Core/Gui only, and `office_speech` is Qt Core/Network/WebSockets — no
Quick, no desktop archives, cheap offscreen tests.
`libQt6Widgets` is still loaded at run time because QtPrintSupport links
it; the suite itself contains no `QApplication` and no `QWidget`.

## Build and test

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Every test is headless: `QT_QPA_PLATFORM=offscreen` and, for QtQuick,
`QT_QUICK_BACKEND=software` (pinned per-test via ctest `ENVIRONMENT` and
defensively in each test's `main`). A build against a QindaTK development
prefix needs `-DCMAKE_PREFIX_PATH=<QindaTK>/install`; the tests find the
toolkit's QML through `QQO_QINDATK_QML_ROOT`.

## Verification is headless first

A screen is not done until it has been looked at:

- **Structural fixtures.** `tests/qml_fixtures/<app>_{1280,520}.txt` are
  dumps of every named item of the real window (`tests/support/quick_dump.h`);
  `QQO_UPDATE_FIXTURES=1` rewrites them after a deliberate change.
- **Pixels.** `QQO_GRAB_DIR=<dir>` on the UI tests saves PNGs of the same
  windows; look at them with the Read tool before committing. Put what you
  checked in the commit body.
- **Squeeze.** Every tool-strip command keeps a visible button inside the
  strip at 520 px (the strips wrap, they never hide commands).
- **Panel export.** Per app, a menu-export test snapshots the tree, runs
  the desktop's own validator, and asserts every exported id resolves in
  the `ActionModel`.
- **On the desktop.** `docs/verification.md` carries the manual checklist
  (panel activation, portal dialogs, tablet, second-screen player, dark and
  light themes, comfortable density). Never launch anything into the user's
  live session without asking first.

## Milestone workflow

Per milestone: read this file + the relevant `docs/` page → implement in
small modules (≤500 lines) → tests green offscreen → fixtures and grabs
reviewed → docs updated in the same commit (`docs/index.md` rows,
`docs/verification.md` gates, a `docs/decisions.md` entry) → commit with an
imperative subject and a body naming contracts landed, gates passed, and
what remains. One branch + one milestone commit. Toolkit additions land in
QindaTK first (their own commit, with the `docs/controls.md` row and a
`tests/qml` case), then the suite consumes them.
