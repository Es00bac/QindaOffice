# QindaQt Office — Agent Instructions

QindaQt Office is the office suite for the QindaQt Wayland desktop
(`~/work_SPaC3/container-wm`, GitHub `Es00bac/QindaQt`): QindaWrite (word
processor), QindaCalc (spreadsheet), QindaShow (presentations), QindaNote
(notes with pen input), with LLM assistance through the user's Claude /
Codex / Kimi CLI subscriptions and OpenAI-/Anthropic-compatible HTTP endpoints.

This file is the source of truth for working in this repository. The
milestone plan (M0–M10) lives in
`~/.claude/plans/i-want-to-create-majestic-lark.md`; the normative desktop
rules it cites (ADR-0110 documents-in-ordinary-windows, ADR-0115 platform
theme, ADR-0116 stock Qt 6, ADR-0117 resize veto, window-containers wiki)
live in `container-wm/docs/wiki/`.

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

1. **One document = one ordinary top-level window.** No tab strip, no MDI,
   no document switcher, no `Ctrl+Tab`. New/Open open new windows (Open may
   reuse a pristine untitled one). `Ctrl+W` and `Ctrl+Q` close the current
   window only, with per-document Save/Discard/Cancel consent (M1+). The
   process exits when its last window closes. Max 32 windows per process
   (`OfficeApplication::kMaxWindows`).
2. **Joining containers needs nothing app-side** — it is compositor-side.
   Requirements: `QApplication::setDesktopFileName("org.qindaqt.<App>")`
   before any window maps; a correct `.desktop` file
   (`data/org.qindaqt.<App>.desktop`) with `StartupWMClass`, `Icon=`, the
   right `Categories=` and `MimeType=`, `Exec=<binary> %F`.
3. **Stock Qt 6 widgets + the qindaqt platform theme.** Never call
   `setStyle`, no QSS, no palette hex literals, no per-app theme, no design
   tokens. Icons via `QIcon::fromTheme()` with freedesktop names.
4. **Global menu export is per-window and fail-closed** (see
   `office_shell/src/global_menu_export.*`): hide the local `QMenuBar` only
   after `start()` succeeded; keep *View ▸ Show Menu Bar* working.
5. **Action discipline**: every command is a persistent window-scoped
   `QAction` with a stable `objectName` from
   `office_shell/src/standard_actions.h` (`ActionId`). The objectName is the
   dbusmenu activation identity — never rename, never reuse.
6. **Don't fight grouped geometry** (ADR-0117): never `resize()`/`move()`/
   `setFixedSize()` a *shown* main window; remember sizes only for new
   windows.
7. **Bounded persistence, per app, never shared** (M1+): ≤32-path restore
   inventory (default off), crash-recovery journal (default on) under
   `$XDG_STATE_HOME/qindaoffice/<app>/`; fail honestly, stay visible.
8. **Only `office_shell` links the desktop's static archives**
   (`qindaqt_global_menu_*`). Every other lib and test must build without
   the desktop installed; `office_shell` fails configure with a clear error
   when `gui-wm/qindaqt-desktop` is absent.
9. **Running outside a QindaQt session must work**: integrations degrade
   visibly, never block (the global-menu export failing closed is the
   template for all desktop integrations).

## Source discipline

From `container-wm/AGENTS.md`, applied here unchanged:

- SPDX header `GPL-3.0-or-later` in every file.
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

`apps → office_shell`; later: `apps → office_ai → office_core`,
`qindacalc → calc_io → calc_engine`, `qindashow → show_model`,
`qindanote/qindashow → office_ink`. Libraries never depend on apps;
`office_core`, `calc_engine`, `show_model`, `office_ink` never link Qt
Widgets (cheap offscreen tests).

## Build and test

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Widget tests run with `QT_QPA_PLATFORM=offscreen` (pinned per-test via ctest
`ENVIRONMENT` and defensively in each test's `main`). See
`docs/verification.md` for the on-desktop manual checklist.

## Milestone workflow

Per milestone: read this file + the relevant `docs/` page → run the plan's
probes (§10) and store captures under `tests/fixtures/probes/` → implement
in small modules (≤500 lines) → tests green offscreen → docs updated in the
same commit → commit with an imperative subject and a body naming contracts
landed, gates passed, and what remains. One branch + one milestone commit.
