<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Converting QindaQt Office to QindaTK — implementation plan (M27 → M31)

> **Landed 2026-09-17 (M27–M31).** The whole suite is QtQuick over
> QindaTK: QindaNote and the foundation (D-027), QindaCalc (D-028),
> QindaWrite (D-029), QindaShow (D-030), the launcher and the removal of
> Qt Widgets (D-031). `libs/office_shell` is deleted, `AGENTS.md` is
> rewritten, and the toolkit is packaged as `dev-libs/qindatk`.
>
> Two gates are the operator's, not the code's, and stay open in
> `docs/verification.md`: the on-desktop manual checklist (panel
> activation per app, portal dialogs, tablet, the second-screen player,
> light/dark themes, comfortable density) and
> `emerge -1 dev-libs/qindatk gui-apps/qindaoffice` from a freshly cut
> snapshot tarball.
>
> The `qtk-preview --dump` fixtures §7 asks for are in-test dumps of the
> real windows instead (`tests/support/quick_dump.h` and
> `tests/qml_fixtures/`): `qtk-preview` cannot load an app's own QML
> module, and the window is what needs pinning. `QQO_GRAB_DIR=<dir>` on
> the UI tests saves the PNGs.


**Audience:** the implementing agent (Kimi or Claude). Self-contained: what
the suite is today, what QindaTK is, what was verified on this machine
before writing a line of the plan, the target architecture, the additions
QindaTK itself needs (the user has authorised extending the toolkit), a
file-by-file fate table, and five milestones with steps, tests and gates.

**Status:** plan. Written on 2026-09-17 against QindaOffice `cc7427b`
(M22 pen overhaul on branch `fix/audit-hardening`) and QindaTK 0.1.0 as
copied from the desktop (`~/work_space/QindaTK`, no git history yet).

**Scope statement.** This is a rewrite of the suite's *UI layer* — every
`QWidget`, `QMainWindow`, `QDialog`, `QGraphicsView`, `QAbstractScrollArea`
and `QToolBar` — onto QtQuick with QindaTK, keeping every document engine,
file format, renderer and test of the engines untouched. It is roughly
16 000 non-blank lines of Widgets code replaced by QML plus C++ QQuickItems.
Done in one go it would break the tree for weeks; done per milestone each
app ships working.

---

## 0. Read this first

### 0.1 Rules that now govern the suite

`AGENTS.md` (QindaOffice) stays the source of truth **except** where this
plan supersedes it; M31 rewrites it. Until then, treat the following as the
rules for every file this plan creates:

1. **QindaTK's `AGENTS.md`** (`~/work_space/QindaTK/AGENTS.md`) applies to
   every QML file in this repository: `import QindaTK as Tk`, toolkit
   types qualified; **no literal colours, sizes, fonts or durations** in
   chrome — everything from `Tk.Theme.*`; layout with `Tk.Flex` /
   `Tk.Grid` / `Tk.Stack` / `Tk.Box` / `Tk.Scroll`, never
   `QtQuick.Layouts`; controls are `QtQuick.Templates` + toolkit visuals;
   ≤ 500 non-blank lines per file; `AGENT-NOTE/GUARD/CONTRACT` markers;
   docs in the same commit; **write complete files**.
2. **Rule 3 of QindaOffice's AGENTS.md ("stock Qt Widgets + platform
   theme, no design tokens") is superseded.** New rule: *QindaTK controls
   and `Tk.Theme` roles; the `QindaTK.QindaQt` bridge follows the
   desktop's QST-1 tokens; the one literal allowed is the document
   surface, which is paper (white page, black ink — D-004) on every
   theme.*
3. **Rule 4 (global menu from the `QMenuBar`) is superseded.** The window's
   menu is a C++ `MenuTree` over an `ActionModel`; the local `Tk.MenuBar`
   and the panel export are two views of it (§3.4). Fail-closed stays:
   the local bar shows until the export is announced.
4. **Rule 5 (action discipline) stays**, moved into `ActionModel`: every
   command is a persistent, window-scoped entry with a stable id from
   `standard_actions.h` (`ActionId::*`) or the app-scoped literals that
   exist today (`notePenTool`, `calcTemplate_*`, …). **Never rename an
   id.** The panel activates by id.
5. **Rules 1, 2, 6, 7, 9 stay verbatim**: one document = one top-level
   window (a `QQuickWindow`), no tabs; desktop-file name set before the
   first window; never resize a shown window; bounded per-app
   persistence; degrade visibly outside a QindaQt session.
6. **Rule 8 becomes:** only `office_quick` links the desktop's static
   archives; every other library and every test builds without the
   desktop installed.
7. **Build policy** (never override `MAKEOPTS`, no `-j`) applies to both
   repositories.
8. **Verification is headless first.** A QML screen is not done until
   `qtk-preview <file> --dump` shows the geometry and `--grab` shows the
   pixels, in compact and comfortable density and under a light theme
   (QindaTK `docs/agent-playbook.md`). Put the dump lines in the commit
   body.

### 0.2 What to read

- QindaTK: `AGENTS.md`, `docs/architecture.md`, `docs/layout.md`,
  `docs/controls.md`, `docs/theming.md`, `docs/docking.md`,
  `docs/agent-playbook.md`, `docs/verification.md`, `examples/studio-shell/`
  (a `MenuBar` + `DockHost` + `StatusBar` application built from the
  toolkit — the closest thing to an office window).
- QindaOffice: `AGENTS.md`, `docs/decisions.md` (D-004, D-009, D-011,
  D-012, D-013, D-017, D-022, D-026), `docs/plans/qindanote-ux-overhaul.md`
  (M22 landed; its M23–M26 design carries into §5.4 here).
- Desktop: `container-wm/docs/wiki/shell/global-menu.md`;
  `/usr/include/qindaqt/shell/global_menu/{exporter,protocol,dbusmenu}/*.h`.

### 0.3 Build and test

QindaTK (once, then after every toolkit change):

```
cd ~/work_space/QindaTK
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset release && cmake --build --preset release
cmake --install build/release --prefix ~/work_space/QindaTK/install   # local prefix for the suite
```

QindaOffice against the local toolkit:

```
cmake --preset dev -DCMAKE_PREFIX_PATH=$HOME/work_space/QindaTK/install
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Tests run with `QT_QPA_PLATFORM=offscreen` **and** `QT_QUICK_BACKEND=software`
(add the second to every QML-backed test's `ENVIRONMENT`; grabs need it).
`QML_IMPORT_PATH` must include the toolkit's install (or build) `qml/` dir
and the suite's own `${CMAKE_BINARY_DIR}/qml`.

### 0.4 Prerequisite — done (2026-09-18)

QindaTK is under git on both machines, with the desktop as the repo home.
The bare repository on `qinda` is authoritative; both working copies are
clones of it and track `origin/main`:

| role | path | `origin` |
| --- | --- | --- |
| hub (authoritative) | `desktop:~/git/QindaTK.git` | — |
| desktop working copy | `~/work_SPaC3/QindaTK` | `/home/cabewse/git/QindaTK.git` |
| laptop working copy | `~/work_space/QindaTK` | `ssh://desktop/home/cabewse/git/QindaTK.git` |

Toolkit changes now move between the machines with `git push` / `git pull`,
like every other project. **Do not rsync this tree.** The instruction that
used to stand here — `rsync -a --exclude build ~/work_space/QindaTK/
desktop:~/work_SPaC3/QindaTK/` — predates the repository and would overwrite
a live clone, reintroducing exactly the drift git now prevents.

`build/` and `install/` are gitignored, so each machine keeps its own. The
desktop checkout must stay at `~/work_SPaC3/QindaTK`: QindaStudio's
`CMakeLists.txt` sets `QINDATK_ROOT` to `$ENV{HOME}/work_SPaC3/QindaTK` and
reads its build tree, and the suite resolves the toolkit through
`<QindaTK>/install`. The ebuild's `EGIT_REPO_URI` points at the bare
repository.

---

## 1. What exists today

Non-blank lines per component (`grep -c '\S'`):

| Component | Lines | Widgets? | Fate |
|-----------|------:|----------|------|
| `libs/office_core` | 1 140 | no (Core+Gui) | **keep** (Document, RecoveryJournal, RecentFiles, RestoreInventory, JsonSettings, paths, LibreOffice bridge). Its tests use `QApplication` only for an event loop → `QGuiApplication`. |
| `libs/calc_engine` | 6 931 | no | **keep** |
| `libs/calc_io` | 458 | no | **keep** |
| `libs/show_model` | 1 330 | no (Core+Gui+KArchive) | **keep** |
| `libs/office_ink` | 792 | no | **keep** (M22's `stroke_geometry`, `stroke_edit`, `renderStrokes` are exactly what the QML ink item paints with) |
| `libs/office_shell` | 3 212 | yes | **replaced by `libs/office_quick`** (§3.2); `preferences`, `single_instance`, `template_loader`, `desktop_settings`, `standard_actions` (ids), the Widgets-free half of `office_application` move over |
| `libs/office_ai` | 1 879 | panel + dialog only | providers/catalog/flow **keep**; `assistant_panel.cpp`, `ProposalDialog` → QML (§3.8) |
| `apps/qindawrite` | 5 176 | view + chrome | engine files keep; `PageTextView` → `PageTextItem`; chrome → QML (§5.1) |
| `apps/qindacalc` | 6 113 | view + chrome | engine files keep; `SpreadsheetView` → `SpreadsheetItem`; dialogs → QML (§5.2) |
| `apps/qindashow` | 5 245 | `QGraphicsView` canvas, player, presenter | model/commands keep; canvas → `SlideCanvasItem`; player/presenter → `QQuickWindow`s (§5.3) |
| `apps/qindanote` | 4 052 | view + chrome | notebook/blocks/render/history keep; page view → `NotePageItem` + `Tk.StylusHandler`; chrome → QML (§5.4) |
| `apps/qindalauncher` | 525 | yes | → QML (§5.5) |
| `tests/*` | — | 31 of 44 test files construct a `QApplication`/widgets | engine tests keep (switch app type); UI tests rewritten (§7) |

Every custom-painted document view already separates *pure rendering*
from the widget: `renderNotePage`, the calc paint file, the slide painter
used by PDF export and the player, the write page painter. Those are the
`paint()` bodies of the new QQuickItems.

## 2. Verified before writing this plan

Every claim below was checked on this laptop (Qt 6.11.1, QindaQt desktop
`0.1.0_pre20260917-r1`, Wayland session `qindaqt-0`).

| # | Fact | Evidence |
|---|------|----------|
| V1 | QindaTK configures, builds and passes its suite here. | `cmake --preset dev` in the copied tree: `100% tests passed, 0 tests failed out of 11`; `qtk-preview --list-qst` lists `qinda-bliss qinda-dark qinda-dusk qinda-high-contrast qinda-light qinda-macos`, so the QST bridge works on this machine (`QindaQt.Tokens` is installed under `/usr/lib64/qt6/qml/QindaQt/Tokens`). |
| V2 | A plain `QQuickItem::event()` never receives stylus events; a QML `PointHandler` only receives the mouse events Qt synthesises from them (device "core pointer", pressure 0); a **C++ `QQuickPointerDeviceHandler` subclass receives the raw `QTabletEvent`** — pointer type `pen`, `xTilt` 12 — when the event enters through the platform path (`QWindowSystemInterface::handleTabletEvent`). | Probe in the session scratchpad (`tabletprobe2`): `C++ handler events 2 pressure 0 tiltX 12 pointer "pen"`. Read pressure from `QTabletEvent::pressure()`, not from the event point (it read 0 there). `QtQuick/private/qquickpointerdevicehandler_p.h` exists; QuickPrivate is already a QindaTK dependency. |
| V3 | The desktop's `QindaQt::AppShell::ActionRegistry` (what first-party QML apps export menus through) **refuses any action with an empty shortcut** and models **flat menus only** (`ActionSpec{id, menuId, menuLabel, label, shortcut, …}`, no submenus, no separators). | `container-wm/src/app_shell/src/action_registry.cpp::validate`: `action.shortcut.isEmpty()` → `InvalidArgument`; QindaStudio's `studio_menu_catalog.h` documents the same bound (125 of 272 entries not exportable). The suite has Open Recent, Pen Colour, Background… submenus and dozens of unaccelerated commands → the suite keeps its **own** exporter over the desktop's toolkit-neutral `Exporter::MenuSource` interface (§3.4). |
| V4 | The desktop's exporter/validator are toolkit-neutral and installed: `qindaqt/shell/global_menu/exporter/menu_source.h` (`MenuSnapshot snapshot()`), `protocol/menu_validation.h`, `dbusmenu/dbusmenu_server.h` (`publish`, `actionActivated(stableId)`). Limits: depth 6, 128 children, 1024 items, unique ids, one checked item per radio group. | Headers read; validator source read in `container-wm/src/shell/global_menu/protocol/src/menu_validation.cpp`. |
| V5 | **The existing Widgets suite's panel-menu activation is dead for 71 of 72 commands, in every app, and always was.** `GlobalMenuExport` resolves an activation with `menuBar()->findChild<QAction*>(id)`, but the actions are children of the window (or of `NoteInkActions`/`NoteStructureActions`), not of the menu bar. Clicking Quit through the suite's dbusmenu endpoint leaves the process running; through Qt's own `/MenuBar/2` endpoint it exits. The first-party editor this was ported from parents its actions to the menus, which is why it works there. | New test `tests/qindanote/tst_note_menu_export.cpp` (added this session, currently **failing on purpose** at `everyExportedCommandResolvesForActivation`, passing `exportedTreeIsCompleteAndValid`); live D-Bus test: `busctl --user call <name> /org/qindaqt/QindaOffice/QindaNote/Menu/1 com.canonical.dbusmenu Event isvu -- 14 clicked s "" 0` → still running; same on `/MenuBar/2` → exited (QindaWrite identical). The new export (§3.4) resolves by id in `ActionModel`, which fixes this by construction. |
| V6 | `QtQuick/private/qquicktextcontrol_p.h` is installed; it is the QtQuick sibling of the `QWidgetTextControl` `PageTextView` uses (`processEvent`, `setTextCursor`, `cursorRect`, `cursorForPosition`, `currentCharFormat`, `updateRequest`, `cursorPositionChanged`). Painting stays `QTextDocument::documentLayout()->draw()`. | Header grep. `PageTextView` uses 8 `processEvent`, 2 `drawContents`, 2 `inputMethodQuery`, `setTextInteractionFlags`, `createStandardContextMenu` (→ a `Tk.Menu`). |
| V7 | `QtQuick.Dialogs` (FileDialog, ColorDialog, MessageDialog, FontDialog) is installed; `Qt6PrintSupport` is installed. `QPrintDialog`/`QPrintPreviewDialog` are Widgets-only → replaced (§3.7). | `/usr/lib64/qt6/qml/QtQuick/Dialogs`, `/usr/lib64/cmake/Qt6PrintSupport`. |
| V8 | QindaTK's `Tk.Menu` is a real `T.Menu` (cascading submenus, keyboard); `Tk.MenuBar` a `T.MenuBar`; `Tk.MenuItem` shows `shortcut` as text and `checkable/checked` with a check glyph but has **no radio glyph**; `Tk.ToolBar` is a non-wrapping Flex row; `Tk.Dialog` has primary/secondary buttons only; there is no window type, no viewport, no stylus handler, no colour swatch picker, no prompt dialog. Lucide covers 42 of the 56 office command icons; missing: `clipboard-paste align-left align-center align-right align-justify indent-increase indent-decrease monitor-play sticky-note book-open sheet bar-chart-3 filter check-square` (most exist in Lucide upstream under these names and only need adding to `CURATED`). | Sources read; `qtk-preview --list-icons` diffed. These gaps are the toolkit additions in §4. |
| V9 | `QGuiApplication` + QML means Qt creates **no** `QDBusMenuBar` (that only exists for a Widgets `QMenuBar`), so the double announcement the Widgets suite makes today (`/MenuBar/2` from Qt plus `/org/qindaqt/…/Menu/1` from the suite) disappears; the suite's export becomes the only one. | `busctl --user tree` of the running Widgets apps shows both endpoints; the platform theme creates the Qt one only through `QMenuBar` (`qindaqt_platform_theme.cpp::createPlatformMenuBar`). |

## 3. Target architecture

### 3.1 Process shape

```
qindanote (QGuiApplication)                 ← no QApplication anywhere in the suite after M31
 ├── office_quick   OfficeApplication: single instance, ≤32 windows, recents, journal, restore inventory, open/new routing
 │                  DocumentHost (per window): Document*, title/dirty, ActionModel + MenuTree, status, close consent, save/load
 │                  GlobalMenuExport: ActionModelMenuSource → MenuExporter → DbusMenuServer → Wayland announcement (unchanged transport)
 │                  PrintService: PrinterModel (QPrinterInfo), PrintJob (QPrinter + render callback), page images for preview
 │                  QML module QindaOffice.Shell: OfficeWindow, MenuBarFromModel, ToolBarFromModel, FindBar, CloseConsentDialog,
 │                                              PrintDialog, PrintPreview, PreferencesDialog, OpenRecentMenu, OfficeStatusBar
 ├── office_ai      providers + ModelCatalog + InlineActionFlow (C++); QML module QindaOffice.Ai: AssistantPanel, ProposalDialog
 ├── office_ink / calc_engine / calc_io / show_model / office_core     unchanged
 └── app QML module (QindaNote.App …): NoteWindow.qml + C++ QQuickItems (NotePageItem, …) + controllers
```

Dependency direction stays `apps → office_quick → office_core`; `office_ai`
depends on `office_core` only; `office_quick` is the only target linking
`qindaqt_global_menu_*` and `Qt6::PrintSupport`.

### 3.2 `libs/office_quick` (replaces `office_shell`)

| File | Origin | Content |
|------|--------|---------|
| `action_model.{h,cpp}` | new | `struct ActionSpec { QString id, text, iconName; QKeySequence shortcut; bool enabled=true, checkable=false, checked=false, visible=true; QString radioGroup; }`. `class ActionModel : QAbstractListModel` with roles for every field plus `shortcutText` (native), `Q_INVOKABLE void trigger(id)`, `setEnabled/setChecked(id,…)`, `spec(id)`, signal `triggered(id)`. Ids are unique; `setChecked` on a radio-group member unchecks the rest. Window-scoped, GUI thread. |
| `menu_tree.{h,cpp}` | new | `struct MenuNode { enum Kind {Action, Separator, Submenu}; QString id /*action id or menu objectName*/, title; QList<MenuNode> children; }`. `class MenuTree : QObject` holding the ordered top-level menus, `QVariantList toVariant()` for QML, `insertMenuBefore(anchorId, node)`, `replaceSubmenu(id, node)` (Open Recent rebuilds), `menusChanged()`. |
| `menu_source.{h,cpp}` | new | `class ActionModelMenuSource final : QindaQt::Shell::GlobalMenu::Exporter::MenuSource` — `snapshot()` walks `MenuTree` + `ActionModel` into `Protocol::MenuTree`: ids as given, separators `sep:<n>`, `radioGroup` from the spec, `shortcutText` native, `mnemonicIndex` from `&` in `text`. Reports `complete=false` with the documented defect codes on overflow. |
| `global_menu_export.{h,cpp}` | moved | Same transport (`DbusMenuServer`, `registerDBusMenuForWindow`, xcb registrar). Constructor takes `(ActionModelMenuSource*, ActionModel*, QWindow*)`; `actionActivated(id)` → `ActionModel::trigger(id)`; `refresh()` on `MenuTree::menusChanged` and on any `ActionModel::dataChanged` (coalesced to one per event-loop pass). Local bar visibility rule unchanged. |
| `standard_actions.{h,cpp}` | moved | `ActionId::*` unchanged (**never rename**). `createStandardActions(ActionModel*)` fills the shared commands with texts, shortcuts (Ctrl+W/Ctrl+Q literal), icon names (`file-plus`, `folder-open`, `save`, `undo-2`, `redo-2`, `scissors`, `copy`, `clipboard-paste`, `search`, `zoom-in`, `zoom-out`, …) and initial enablement (disabled until a host wires them — D-011/D-012 keep). `standardMenuTree()` builds File/Edit/View/AI/Settings/Help with the anchors apps insert before (`fileMenuAnchor`, `aiMenu`). |
| `document_host.{h,cpp}` | from `document_window*.cpp` | `class DocumentHost : QObject` (QML_ELEMENT, uncreatable; created by the app): owns `Core::Document*`, `ActionModel`, `MenuTree`, `GlobalMenuExport`, status info, journal timer, auto-save, recents, `loadPath`, `saveDocument`, `saveDocumentAs`, `isPristineUntitled`, close consent as a state machine (`closeRequested` → QML `CloseConsentDialog` → `resolveClose(save/discard/cancel)`), file-dialog requests as signals the QML `FileDialog` answers (`openRequested(filters)`, `saveAsRequested(suggested, filters)` → `openPathsChosen(paths)`/`saveAsPathChosen(path)`). Everything `DocumentWindow` did minus the widgets. |
| `office_application.{h,cpp}` | moved | Widgets-free: window registry of `QQuickWindow*` + `DocumentHost*`, `kMaxWindows`, canonical-path uniqueness, pristine reuse (D-018b), restore inventory, recovery prompt (a QML dialog), `createWindow()` through the app's `QQmlComponent`. |
| `app_runner.{h,cpp}` | moved | `runOfficeApp(argc, argv, desktopId, qmlWindowUrl, hostFactory)`: `QGuiApplication`, `setDesktopFileName` before any window, single instance, one `QQmlApplicationEngine` for the process, import paths, `--help/--version`. |
| `print_service.{h,cpp}` | from `print_helper.cpp` | `PrinterModel : QAbstractListModel` (QPrinterInfo: name, default, duplex, page sizes); `PrintJob` (QPrinter + `PrintRenderCallback`, copies, range, orientation, `toPdf(path)`); `PagePreviewItem : QQuickPaintedItem` (paints page *n* through the callback into a cached `QImage`, DPI-aware). |
| `preferences.{h,cpp}` | moved | + `density` (compact/comfortable), `themePreset` (used only off-desktop), `note/*` keys. |
| `single_instance`, `template_loader`, `desktop_settings` | moved | unchanged |
| **dropped** | `wrapping_tool_bar`, `flow_layout`, `preferences_dialog.cpp`, `document_window_chrome.cpp` | replaced by `Tk.ToolBar { wrap: true }` (§4) and QML |

QML module `QindaOffice.Shell` (`libs/office_quick/qml/`):

| File | Contract |
|------|----------|
| `OfficeWindow.qml` | `Tk.AppWindow` (§4 A1). Required `host: DocumentHost`. Slots: `toolBars` (list of items placed in a column under the menu bar), `content`, `sideDock` (optional `Tk.DockHost`), `statusFields`. Instantiates `QindaQtTheme {}` (bridge) and sets `Tk.Density.modeName` from Preferences. Menu bar: `MenuBarFromModel { tree: host.menuTree; actions: host.actions; visible: host.localMenuVisible }`. Find bar slot below content. Title from `host.title` + dirty mark. `onClosing` → `host.requestClose()` (never `close.accepted = true` before the host says so). Never sets width/height after `Component.onCompleted` (ADR-0117). |
| `MenuBarFromModel.qml` / `MenuFromNode.qml` | `Instantiator`s over `tree.menus` → `Tk.Menu { title }`; children by kind → `Tk.MenuItem { text; shortcut: spec.shortcutText; checkable; checked; enabled; onTriggered: actions.trigger(id) }`, `Tk.MenuSeparator`, nested `MenuFromNode` for submenus. Objectnames `menu_<id>`, `menuItem_<id>`. |
| `ToolBarFromModel.qml` | `Tk.ToolBar { wrap: true }` filled from a list of action ids (+ `"|"` separators, `{widget: Component}` slots for combos): `Tk.IconButton { iconName; tooltip: text; checkable; checked; enabled; onClicked: actions.trigger(id) }`; a button whose icon is missing shows text (D-013's rule, now enforced by `Tk.Button` with `text`). |
| `OfficeStatusBar.qml` | `Tk.StatusBar` with a transient message field (left, `host.statusMessage`) and the permanent info field (right, `host.statusInfo`). |
| `FindBar.qml` | `Tk.TextField` + Next/Previous `Tk.IconButton`s + case `Tk.CheckBox` + close; `Escape` closes; emits `findNext(text, cs)` etc. Shared by Calc/Show/Note (Write keeps its replace variant on top of it). |
| `CloseConsentDialog.qml` | `Tk.MessageDialog` (§4 A4) Save / Discard / Cancel bound to `host`. |
| `RecoveryDialog.qml` | the crash-recovery prompt. |
| `PreferencesDialog.qml` | `Tk.Dialog` with `Tk.TabStrip` pages General / Documents / <app page slot>; controls bound to `Preferences` (`Tk.Switch`, `Tk.NumberField`, `Tk.ComboBox` for font family via `Qt.fontFamilies()`, density). |
| `PrintDialog.qml`, `PrintPreview.qml` | §3.7. |
| `OpenRecentMenu.qml` | rebuilt through `MenuTree::replaceSubmenu` — no QML needed beyond `MenuFromNode`; listed for clarity. |

### 3.3 Windows and the shell

- One document = one `Tk.AppWindow` instance created by `OfficeApplication`
  from the app's window component; the window's `objectName` is the
  journal key. `QGuiApplication::setDesktopFileName("org.qindaqt.<App>")`
  before the first window; `.desktop` files unchanged.
- Initial size only (`width/height` set in the component, never later).
- Docks: an app with side panels (Note navigator, Show notes/transition,
  Calc none, Write none) uses `Tk.DockHost` with `DockModel { storageKey }`
  so panel arrangement persists per app (rule 7: state dir).
- Local menu bar: `host.localMenuVisible` = `!exported && viewShowMenuBar`.
- Keyboard: every `ActionSpec.shortcut` is bound once by a C++
  `QShortcut`-equivalent on the window (`ShortcutBinder` in `DocumentHost`
  via `QQuickWindow` event filter, window-scoped) — **not** through QML
  `Shortcut{}` per item, so an action that is in no visible menu still
  fires and the export stays the single source of truth.

### 3.4 Global menu (replaces the broken activation, keeps the transport)

```
MenuTree + ActionModel ──► ActionModelMenuSource::snapshot() ──► MenuExporter (validates) ──► DbusMenuServer::publish
                      └──► MenuBarFromModel (local Tk.MenuBar)         DbusMenuServer::actionActivated(id) ──► ActionModel::trigger(id)
```

- Ids: action ids for actions, `MenuNode::id` (objectName) for menus,
  `sep:<n>` for separators (unique per snapshot).
- Radio groups: `ActionSpec::radioGroup` (e.g. `noteTool`, `notePenColor`);
  the model guarantees ≤ 1 checked per group, the validator requires it.
- Refresh: coalesced per event-loop pass; `Unchanged` snapshots cost
  nothing on the wire.
- Test (every app, offscreen, no D-Bus needed): snapshot → `complete` →
  `validateMenuTree().accepted` → **every exported action id resolves in
  `ActionModel`** and `trigger(id)` reaches the app's handler (a spy on
  `triggered`). This is `tst_note_menu_export` generalised into
  `tests/office_quick/tst_menu_export.cpp` (synthetic model) plus one
  `tst_<app>_menu.cpp` per app.
- Live check (manual, on the desktop): `busctl --user call … Event … clicked`
  on Quit exits the app; the panel shows radio checks for the Note tools.

### 3.5 Theme, paper, density

- `OfficeWindow.qml` instantiates `QindaQtTheme {}`; off the desktop
  `Tk.Theme.preset = Preferences.themePreset` (default `sloom-dark`, the
  toolkit default; the user can flip it).
- **Paper stays paper** (D-004): `PageTextItem`, `SlideCanvasItem`,
  `NotePageItem` and `PagePreviewItem` paint white paper and black default
  ink regardless of theme; the *canvas around* the page is
  `Tk.Theme.color.canvas`. The spreadsheet grid is a document surface too:
  white cells, black text, grid lines in a fixed light grey (the print
  renderer's), selection in `Tk.Theme.color.selection`.
- Density: `Tk.Density.modeName = Preferences.density` (default
  `compact`, the toolkit's measured reference); document zoom is
  independent of density.

### 3.6 Dialogs

| Need | Solution |
|------|----------|
| Open / Save As / Insert Image / Export PDF | `QtQuick.Dialogs.FileDialog` driven by `DocumentHost` signals; on the QindaQt desktop the platform helper gives the portal dialog (`xdg-desktop-portal-qindaqt` is running); off-desktop the QML fallback. Filters from `Document::mimeTypes()` (`nameFilters`). Verify on the desktop in M27 that the portal opens; if not, the QML dialog is the accepted fallback. |
| Colour (pen, text, fill, stroke, borders) | `Tk.ColorButton` + `Tk.ColorSwatches` popover (§4 A5); "Custom…" opens `QtQuick.Dialogs.ColorDialog`. |
| Save/Discard/Cancel, warnings, deletes | `Tk.MessageDialog` (§4 A4). |
| Rename page/section/sheet, new section, simple text prompts | `Tk.PromptDialog` (§4 A4) — used only where inline rename is impossible; navigator rename is inline (`Tk.TreeRow` editing, §4 A6). |
| About | `Tk.Dialog` with `Tk.Label`s. |
| Preferences | `PreferencesDialog.qml`. |
| Chart / Validation / Conditional format / Page setup / Masters | app `Tk.Dialog`s built from `Tk.PropertyRow`, `Tk.ComboBox`, `Tk.NumberField`, `Tk.ColorField`. |

### 3.7 Printing and PDF

- `PrintJob` wraps `QPrinter` (PrintSupport links Widgets as a library
  dependency but needs no `QApplication`; `QPrinter`, `QPrinterInfo`,
  `QPageSize` work under `QGuiApplication`). Record this in a decision
  entry; the ebuild keeps `qtbase[widgets]` for that reason alone.
- `PrintDialog.qml`: printer `Tk.ComboBox` (from `PrinterModel`), copies
  `Tk.NumberField`, range (All / Pages `from`–`to`), orientation
  `Tk.Segmented`, "Print to PDF…" (file dialog), Print / Cancel.
- `PrintPreview.qml`: `Tk.Viewport` (§4 A3) over a column of
  `PagePreviewItem`s, a `Tk.ZoomControl` island, page `n of m`, Print…
  button. Same callback as printing (plan 5.2 of the original).
- PDF export unchanged (`QPrinter::PdfFormat`); QindaWrite's
  `pdf_exporter.cpp` and QindaShow's `pdf_export.cpp` are Widgets-free
  already.

### 3.8 AI

- `libs/office_ai/src/assistant_panel.cpp` → `assistant_controller.{h,cpp}`
  (`AssistantController : QObject`, QML_ELEMENT: `models` list, `modelId`,
  `scope`, `conversation` (markdown string), `streaming`, `send(prompt)`,
  `stop()`, `insert()`, `replaceSelection()`, `copy()`, history bound as
  today) + `qml/AssistantPanel.qml` (`Tk.Panel` → `Tk.ComboBox` model,
  `Tk.Segmented` scope, `Tk.Scroll` of `Tk.Label { textFormat:
  Text.MarkdownText }`, `Tk.TextField` input, Send/Stop, Insert/Replace/Copy
  `Tk.Button`s, off-machine `Tk.Badge`). Privacy contract unchanged: nothing
  sent without Send.
- `ProposalDialog` → `qml/ProposalDialog.qml` (`Tk.Dialog`, two
  `Tk.TextArea`s, Apply / Regenerate / Discard).
- `office_ai` links Core, Network, Gui (for `QImage` in `DocumentContext`),
  Qml; **no Widgets**. `InlineActionFlow` keeps its API; the dialog it
  opens is created through a `std::function<void(original, proposal,
  callbacks)>` seam the app's QML fills.

---

## 4. QindaTK additions (authorised by the user)

Each is a proper toolkit change: file in `src/qml` or `src/core`, row in
`docs/controls.md`, gallery entry, `tests/qml/tst_*.qml` or
`tests/cpp/tst_*.cpp`, `docs/catalog.json` regenerated, a `docs/decisions.md`
entry where the choice is cross-cutting. Keep QindaTK's own rules
(LGPL header, ≤ 500 lines, no literals, Templates base).

| # | Addition | Contract |
|---|----------|----------|
| A1 | **`Tk.AppWindow`** (`src/qml/AppWindow.qml`, `T.ApplicationWindow`) | Properties: `menuBar` (alias of the template's), `toolBars` (list<Item>, laid out in a column `Tk.Flex` under the menu bar; each is typically a `Tk.ToolBar`), `statusBar` (Item slot, bottom), `content` (default property), `findBar` (Item slot between content and status bar, hidden when null/invisible). Background `Tk.Theme.color.bg`; the `palette` bridge `ApplicationShell.qml` uses so native popups (file dialogs off-portal) inherit the theme — copy those 14 palette lines, reading `Tk.Theme.color.*`. `font.family/pixelSize` from the theme. objectNames `appMenuBar`, `appToolBars`, `appContent`, `appFindBar`, `appStatusBar`. Test: instantiates with all slots; `--dump` shows the four bands stacked without gaps at 1280×800 and 520×700. |
| A2 | **`Tk.StylusHandler`** (`src/core/stylus_handler.{h,cpp}`, `QQuickPointerDeviceHandler` subclass) | QML: `acceptedDevices` default `Stylus \| Mouse`, `acceptedPointerTypes` default `Pen \| Eraser \| Generic`; read-only `active`, `pointerType` (`"pen"`, `"eraser"`, `"mouse"`), `pressure`, `tiltX`, `tiltY`, `rotation`, `buttons`, `position` (item coords); signals `began(sample)`, `moved(sample)`, `ended(sample)` where `sample` is a `QVariantMap` **and** C++ signals `sampleBegan/Moved/Ended(const StylusSample&)` (`struct StylusSample { QPointF pos; qreal pressure, tiltX, tiltY, rotation; QPointingDevice::PointerType pointerType; Qt::MouseButtons buttons; bool synthesizedFromMouse; }`) for C++ items that must not go through QVariant per sample. Mouse fallback: mouse events become samples with `pressure = 0.6` and `synthesizedFromMouse = true` (the office's laptop path). Touch is never accepted (palm baseline). Reads `QTabletEvent::pressure()/xTilt()/yTilt()` directly (V2). Test: `tests/cpp/tst_stylus_handler.cpp` sends the three-event sequence through `QWindowSystemInterface::handleTabletEvent` and asserts the C++ signals with the values above; a `Finger` touch produces nothing. `AGENT-NOTE`: the second private-API use in the toolkit (after `LayoutInfo`), justified by V2. |
| A3 | **`Tk.Viewport`** (`src/qml/Viewport.qml`) | A document viewport: `Tk.Scroll { overflowX: Auto }` whose content is scaled by `zoom` (`scale` on a host item sized `contentItem.implicitSize × zoom`), background `Tk.Theme.color.canvas`, `zoom`/`minZoom`/`maxZoom`, `fitMode` (`"none"`, `"width"`, `"page"`) re-applied on viewport resize, `zoomAbout(point, factor)` keeping the content point under the cursor fixed, Ctrl+wheel → `zoomAbout`, Shift+wheel horizontal, middle-drag pan, `contentPadding`. Signals `zoomChanged`, `fitModeChanged`. Pairs with `Tk.ZoomControl` in a `Tk.Island`. Test: `zoomAbout` keeps a content point fixed; fit-width tracks a resize. |
| A4 | **`Tk.MessageDialog`** and **`Tk.PromptDialog`** (`src/qml/`) | `MessageDialog`: `Tk.Dialog` with `text` (wrapping `Tk.Label`), `iconName`, and up to three buttons — `primaryText`, `secondaryText`, `tertiaryText` (left-aligned, e.g. Discard) — emitting `accepted`, `rejected`, `tertiary`; `destructive` on primary; objectNames `dialogTertiary`. Implement by adding `tertiaryText`/`tertiary()` to `Tk.Dialog` itself (footer `Tk.Spacer` between tertiary and the pair) so every dialog can have a third button. `PromptDialog`: `Tk.Dialog` with `label`, `text` (rw), `placeholderText`, `validator`-like `acceptEmpty: false`; Enter accepts; objectName `promptField`. |
| A5 | **`Tk.ColorSwatches`** + **`Tk.ColorButton`** | `ColorSwatches`: `Tk.Grid` of 16px swatches from `colors` (`[{name, color}]`), `current`, `columns` (8), a "Custom…" `Tk.Button` emitting `customRequested()`; `selected(color)`; keyboard navigable; accessible names from `name`. `ColorButton`: `Tk.IconButton`-sized (24) with a painted swatch of `color` under a small chevron, `InstantPopup` `Tk.Popover` holding `ColorSwatches`; `colorSelected(color)`, `customRequested()`; `tooltip`. Used by pen/highlighter/text/fill/stroke/border colour everywhere in the suite. |
| A6 | **`Tk.TreeRow` editing + `Tk.ToolBar.wrap` + icons + radio menu items** | (a) `Tk.TreeRow { editable }`: F2 / double-click → inline `Tk.TextField` over the label, `renamed(text)`, Escape cancels (Note navigator, Calc sheet tabs reuse the same edit pattern via `Tk.TabStrip { editable }` — add there too). (b) `Tk.ToolBar { wrap: true }` exposes the Flex `wrap` and grows in rows (the suite's D-013 rule: a command is never hidden for want of width); `implicitHeight` follows the wrapped content. (c) `Tk.MenuItem`: when `T.MenuItem.exclusiveGroup`/`autoExclusive` is set, draw a radio dot instead of the check glyph (`radio` derived from the action group). (d) Add the 14 missing Lucide names (V8) to `tools/scripts/gen_icons.py` `CURATED` and regenerate; if Lucide lacks a name (`sheet`, `sticky-note` may be `file-spreadsheet`, `sticky-note`), pick the upstream equivalent and add an alias. |
| A7 | **`Tk.Ruler` markers** (M29) | `markers: [{id, position (px), kind: "indent-first"\|"indent-left"\|"indent-right"\|"tab", draggable}]`, `markerMoved(id, position)`, `tabAdded(position)`, `tabRemoved(id)`; drawn with theme roles. QindaWrite's ruler becomes `Tk.Ruler` + a small binding file. |

Everything else the suite needs exists: `Tk.MenuBar/Menu/MenuItem`,
`Tk.ToolBar/IconButton/Button/Segmented/ComboBox/NumberField`,
`Tk.TabStrip` (sheet tabs, Write review tabs), `Tk.DockHost/DockPanel`
(Note navigator, Show side panels, Assistant), `Tk.StatusBar/StatusField`,
`Tk.SearchField/TextField/TextArea`, `Tk.Dialog/Popover`, `Tk.Scroll`,
`Tk.ZoomControl/Island`, `Tk.ListRow/TreeRow`, `Tk.Card` (launcher),
`Tk.Notice` (degraded integrations), `Tk.CommandPalette` (a free win:
`Ctrl+Shift+P` over `ActionModel`).

---

## 5. Per-app conversion

Conventions for every app: `apps/<app>/main.cpp` calls
`runOfficeApp(argc, argv, "org.qindaqt.<App>", u"qrc:/qt/qml/<Module>/<App>Window.qml"_s, [] { return new <App>Host; })`;
the app's QML module URI is `QindaOffice.<App>` (`qt_add_qml_module` on the
executable, QML under `apps/<app>/qml/`); C++ items are `QML_ELEMENT`s in
`apps/<app>/src/`; `<App>Host : Shell::DocumentHost` owns the document and
wires actions. Window QML files ≤ 500 lines: split by band (menus/actions
wiring in C++, toolbar QML, content QML, dialogs QML).

### 5.1 QindaWrite (M29)

| Today | Fate |
|-------|------|
| `page_text_view.{h,cpp}` (QAbstractScrollArea + `QWidgetTextControl`) | `page_text_item.{h,cpp}`: `PageTextItem : QQuickPaintedItem` (QML_ELEMENT). `QQuickTextControl` over `WriteDocument::textDocument()`; `paint()` draws every visible page: paper rect, `documentLayout()->draw(painter, ctx)` with `ctx.clip` per page and the cursor/selection palette pinned (D-004), headers/footers/footnote bands through the existing page painters; `keyPressEvent/mousePressEvent/…/inputMethodEvent` → `m_control->processEvent(e, pageOffset)`; `inputMethodQuery` forwarded; `updateRequest` → `update()`; `visibilityRequest` → `ensureVisible` signal for the `Tk.Viewport`; context menu → a `Tk.Menu` built from the standard edit actions. Hosted in `Tk.Viewport { fitMode: "width" }`. |
| `page_text_accessibility.cpp` (`QAccessibleWidget`) | `page_text_accessible.cpp`: `QAccessibleQuickItem` subclass with `QAccessibleTextInterface` over the control's cursor/document; registered with `QAccessible::installFactory`. Same coverage as today. |
| `write_ruler.cpp` (QWidget) | `WriteRuler.qml`: `Tk.Ruler` (A7) bound to `FormatController` paragraph indents/tabs. |
| `format_controller`, `write_actions`, `write_typography`, `table_actions`, `write_review_actions`, `write_toc`, `write_footnotes`, `write_revisions`, `style_catalog`, `spell_checker`, `format_io`, `page_geometry`, `page_setup_io`, `pdf_exporter`, `write_document` | **keep** the logic; the `QAction`/`QToolBar` construction becomes `ActionSpec`s in the `ActionModel` + `WriteToolBar.qml` (`ToolBarFromModel` with two widget slots: font family `Tk.ComboBox { model: Qt.fontFamilies() }`, size `Tk.NumberField`); text colour/highlight → `Tk.ColorButton`. |
| `find_replace_bar.cpp` | `WriteFindBar.qml` (`FindBar` + replace field + Replace/Replace All). |
| `write_window*.cpp` | `WriteHost` (C++: menus, action handlers, page setup dialog model) + `WriteWindow.qml` (`OfficeWindow` with the toolbar, ruler, viewport, find bar). Page Setup, Word Count, Insert Table/Hyperlink/Symbol dialogs → `Tk.Dialog`s. |

Gates: every M14/M18/M20 write test ported; `tst_write_accessibility`
passes against the item; a `.docx` round trip and PDF export unchanged
byte-for-byte on the fixtures; `qtk-preview` dump at 520 px shows every
toolbar command (wrap).

### 5.2 QindaCalc (M28)

| Today | Fate |
|-------|------|
| `spreadsheet_view{,_input,_paint}.cpp` (QAbstractScrollArea) | `spreadsheet_item.{h,cpp}` + `spreadsheet_item_input.cpp` + `spreadsheet_item_paint.cpp`: `SpreadsheetItem : QQuickPaintedItem` over `SpreadsheetDocument`; own `rowOffset/columnOffset`, `visibleRange`, two `Tk.ScrollBar`s bound to `scrollX/scrollY` properties in `CalcGrid.qml`; frozen panes, selection, fill handle, autofilter buttons, comments markers painted exactly as today (paint code moves nearly verbatim; `viewport()` → `this`, `palette()` → fixed document colours + `Tk.Theme` selection colour passed in as a property); keyboard/mouse/wheel from the input file via `QQuickItem` event overrides; in-cell editor = `Tk.TextField` overlaid at `editorRect` (property), committing through the existing edit command. |
| `calc_window*.cpp`, `calc_window_commands.cpp` | `CalcHost` + `CalcWindow.qml`: formula bar (`Tk.TextField` + name box `Tk.ComboBox`), `CalcToolBar.qml` (number format `Tk.ComboBox`, borders/fill `Tk.ColorButton`s), sheet tabs `Tk.TabStrip { editable; closable }` + add button, find bar, status (sum/avg/count of selection). |
| `calc_rule_dialogs.cpp`, `chart_dialog.cpp`, validation | `CondFormatDialog.qml`, `ValidationDialog.qml`, `ChartDialog.qml` (`Tk.Dialog` + `Tk.PropertyRow`s); charts keep QtCharts through the **QtCharts QML module** (`ChartView`) when `QQO_WITH_CHARTS`, painted into the grid via `grabToImage` for the print path, or through the existing `QChart` → `QImage` render. |
| `calc_structure_*`, `calc_rules_*`, `calc_format_controller`, `calc_clipboard`, `calc_undo`, `calc_recovery*`, `spreadsheet_document` | **keep**; `QAction`s → `ActionSpec`s; `QMenu` context menus → `Tk.Menu` built from the same ids. |

Gates: `tst_calc_window`, `tst_calc_ui`, `tst_calc_rules_ui` ported
(cursor/selection/edit via the item's API and `ActionModel::trigger`);
xlsx/ods round trips unchanged; the squeeze dump shows every toolbar
command.

### 5.3 QindaShow (M30)

| Today | Fate |
|-------|------|
| `slide_canvas*.cpp` (`QGraphicsView` + `ShapeItem : QGraphicsItem`) | `slide_canvas_item.{h,cpp}` + `_input.cpp` + `_snap.cpp` + `_order.cpp`: `SlideCanvasItem : QQuickPaintedItem` painting the current slide through the **same shape painter the player and PDF export use** (extract `paintShape(QPainter*, const Shape&)` from `ShapeItem::paint` into `show_model`-adjacent `shape_painter.{h,cpp}` in the app — Widgets-free), selection outline + 8 handles, marquee, guides; hit-testing/snapping/z-order logic is geometry over `show_model` (the `QGraphics` calls in `_snap`/`_order` are two each: replace with model lookups). In-place text edit: `QQuickTextControl` over the shape's `QTextDocument` (`text_bridge.cpp` already converts) painted by the item — same technique as `PageTextItem`. Hosted in `Tk.Viewport { fitMode: "page" }`. |
| thumbnail sorter | `SlideSorter.qml`: `Tk.Scroll` + `Repeater` of `SlideThumbItem` (paints slide *n* small via the shape painter, cached `QImage`), drag reorder emits `moveSlide(from, to)` → existing `SlideListCommand`. |
| notes/transition/layout dock, shapes toolbar, arrange menus | `Tk.DockHost` panels (`NotesPanel.qml` `Tk.TextArea`, `TransitionPanel.qml`, `LayoutPanel.qml`); `ShowToolBar.qml`; arrange/align/distribute/group as `ActionSpec`s. |
| `slide_player.cpp` (QWidget fullscreen) | `SlidePlayerWindow` (`QQuickWindow` subclass or `Window.qml` + `SlidePlayerItem`): transitions as QML `NumberAnimation`s on two stacked slide items (cut/fade/slide/zoom; disabled under `Tk.Theme.reducedMotion`), pen/laser annotations painted by the item, keys as today, `showFullScreen()` on the chosen `QScreen`. |
| `presenter_window.cpp` | `PresenterWindow.qml` (current/next `SlideThumbItem`s, notes `Tk.Label`, timer/clock `Tk.Mono`). |
| `show_master_dialog.cpp`, find bar, AI | `MasterDialog.qml`, shared `FindBar`, `show_ai` unchanged. |
| `show_document`, `show_commands`, `show_arrange`, `show_design_actions`, `show_clipboard`, `presentation_controller`, `pdf_export` | **keep** (`QAction` → `ActionSpec`). |

Gates: `tst_qindashow`, `tst_show_ui`, `tst_show_design` ported (undo
payloads, snapping, grouping through the item's API); fodp/pptx round
trips unchanged; deck PDF identical on fixtures; player on a second screen
checked manually.

### 5.4 QindaNote (M27, the proving app)

| Today | Fate |
|-------|------|
| `note_page_view.cpp`, `note_page_input.cpp`, `note_page_blocks.cpp` | `note_page_item.{h,cpp}` (`NotePageItem : QQuickPaintedItem`, QML_ELEMENT; paints exactly what `renderNotePage` paints plus the in-progress stroke, the eraser ring, block frames and selection chrome — narrow repaint rects kept), `note_page_ink.cpp` (consumes `Tk.StylusHandler`'s C++ `sampleBegan/Moved/Ended`: pen/highlighter/eraser gestures, barrel button = temporary eraser, `InkCommand` per gesture — the M22 logic moved out of `event()`), `note_page_blocks.cpp` (block hit-testing, tap-to-type, checklist toggle; the **M23 design**: free-floating painted blocks with `rect`, one live editor). The live editor is a `Tk.TextArea` (text) / `Tk.TextField` (checklist) positioned by the item's `editorRect` in `NotePage.qml`; `Tk.StylusHandler { target: page }` and a `Tk.Viewport` host it. |
| `note_page_history.{h,cpp}`, `note_page_render.cpp`, `note_blocks.{h,cpp}`, `notebook.{h,cpp}` | **keep** (add `BlocksCommand`/`BackgroundCommand` per the M23 design). |
| `note_ink_actions.cpp` | `NoteInkActions` keeps the ids and `tools.json`, builds `ActionSpec`s (`radioGroup` per picker); the strip's two popup buttons become `Tk.ColorButton` (pen/highlighter colour with the swatch lists) and a width `Tk.Popover` of `Tk.Segmented`-style presets. |
| `note_tool_strip.cpp`, `note_window*.cpp`, `note_structure.cpp`, `note_find_bar.cpp`, `note_preferences_page.cpp` | `NoteHost` (C++) + `NoteWindow.qml`, `NoteToolStrip.qml` (`ToolBarFromModel`), `NoteNavigator.qml` (`Tk.DockPanel` left: `Tk.SearchField` filter + `Tk.Scroll` of `Tk.TreeRow { editable }` from a flattened `NavigatorModel` (C++ `QAbstractListModel` over the notebook: section/page rows, depth, expanded); drag-and-drop reorder via `Tk.TreeRow` drag → `Notebook::movePageTo` — the M25 design), context menu `Tk.Menu` from the structure ids, shared `FindBar`, `NotePreferencesPage.qml`. Background/zoom → `Tk.ComboBox`/`Tk.ZoomControl` in an island over the viewport (M25's fit-width comes with `Tk.Viewport`). |

Gates: every current note test ported (`tst_qindanote`, `tst_note_blocks`,
`tst_note_input` via `QWindowSystemInterface::handleTabletEvent` on the
item's window, `tst_note_ui` as a `qtk-preview --dump` fixture test,
`tst_note_menu_export` **passing both cases**); an M21 notebook opens and
saves byte-identical; on the desktop: pressure and tilt reach the ink,
palm ignored, panel menu shows the Ink radio groups and **activates**
every command, Quit through the panel exits.

### 5.5 Launcher (M31)

`LauncherWindow.qml`: `Tk.AppWindow` without menus; a `Tk.Grid {
columns: "repeat(4, minmax(160, 1fr))" }` of `Tk.Card`s (icon, name,
description, accessible name) and a recents `Tk.Scroll` of `Tk.ListRow`s
from the existing `LauncherCatalog`; starts apps detached as today.

---

## 6. Milestones

Each milestone: its own branch (`feat/qtk-m27-note` …), all tests green
offscreen, `qtk-preview` dumps in the commit body, docs updated in the
same commit (`docs/index.md` rows, `docs/verification.md` gates, a
`docs/decisions.md` entry), one commit per milestone. Toolkit additions
land in QindaTK first (their own commits there), then the suite consumes
them.

### M27 — Foundation + QindaNote

1. QindaTK: A1 `AppWindow`, A2 `StylusHandler`, A3 `Viewport`, A4
   `MessageDialog`/`PromptDialog` (+ `Tk.Dialog.tertiaryText`), A5
   `ColorSwatches`/`ColorButton`, A6 (a–d). Gallery + tests + docs +
   catalog. Install to the local prefix.
2. `libs/office_quick`: `ActionModel`, `MenuTree`, `ActionModelMenuSource`,
   `GlobalMenuExport` (moved), `standard_actions` (ids + model factory +
   standard tree), `DocumentHost`, `OfficeApplication`, `app_runner`,
   `PrintService`, `Preferences` (+ density/preset), moved singletons; QML
   module `QindaOffice.Shell` with every file in §3.2. `office_shell` stays
   in the tree until M31 so the three unconverted apps keep building
   (CMake option `QQO_WIDGETS_APPS=ON` until then).
3. `office_ai`: `AssistantController` + `AssistantPanel.qml` +
   `ProposalDialog.qml`; keep the Widgets panel compiled behind the same
   option for the unconverted apps.
4. QindaNote per §5.4, including the M23 block model (`rect`/`html`/`level`,
   `BlocksCommand`, one live editor) — the QML port is the moment to
   rebuild blocks, so do not port the 90 px widget stack.
5. Tests: `tests/office_quick/{tst_action_model,tst_menu_export,tst_document_host,tst_print_service}.cpp`;
   note tests per §5.4; every QML window has a `--dump` fixture test
   (`tests/qml_fixtures/<app>_1280.txt`, `<app>_520.txt`, compared modulo
   text content).
6. Docs: `docs/decisions.md` **D-027: The suite's UI is QindaTK on QtQuick;
   paper stays paper; the panel menu is exported from an action model**
   (records V2–V5, V9, the density/preset defaults, PrintSupport-without-
   QApplication); `docs/index.md`; `docs/verification.md` "M27 gates".

**Gate.** QindaNote runs on the QindaQt desktop from the build tree with
the panel menu working end to end (V5 fixed), pen with pressure/tilt,
palm ignored, dark theme paper white, `ctest` green (note, office_quick,
engines), the three Widgets apps still build and pass.

### M28 — QindaCalc (§5.2)
### M29 — QindaWrite (§5.1, + A7)
### M30 — QindaShow (§5.3)

### M31 — Launcher, removal, packaging, rules

1. Launcher (§5.5). Delete `libs/office_shell`, the Widgets assistant
   panel, `WrappingToolBar`, every `Qt6::Widgets`/`WidgetsPrivate` link
   and the `QQO_WIDGETS_APPS` option; `grep -r QApplication` must be empty.
2. `AGENTS.md` rewrite per §0.1 (rules 3, 4, 8; QML rules; verification
   with `qtk-preview`); `docs/verification.md` manual checklist (panel
   activation, portal dialogs, tablet, second-screen player, dark/light
   themes, comfortable density); `docs/decisions.md` **D-028: Widgets are
   gone** (what was removed and why nothing depends on it).
3. Packaging: `packaging/gentoo/gui-apps/qindaoffice/*.ebuild`: `RDEPEND +=
   dev-libs/qindatk >=dev-qt/qtdeclarative-6.11:6=`, keep `qtbase[widgets]`
   (PrintSupport), drop nothing else; QindaTK's own ebuild
   (`~/work_space/QindaTK/packaging/gentoo/dev-libs/qindatk`) gets
   `EGIT_COMMIT` from the tag once §0.4 is done; add both to the `qindaqt`
   overlay (`QindaGentoo`). `tools/make-dist.sh` unchanged. Emerge with no
   `MAKEOPTS` override; all five binaries and desktop files present.
4. `docs/plans/qindanote-ux-overhaul.md`: mark M22 landed in Widgets, M23
   landed inside M27, M24–M26 re-targeted to the QML item (same designs,
   same ids, same tests).

**Gate.** `ctest --preset dev` green with no Widgets in the process;
`emerge -1 gui-apps/qindaoffice` from a snapshot tarball; the on-desktop
checklist signed off.

---

## 7. Test strategy

- **Engines**: unchanged files; their `main()`s switch to
  `QGuiApplication` (or `QCoreApplication` where no fonts are needed).
- **Items**: C++ tests instantiate the QML window offscreen
  (`QQmlApplicationEngine` + `QT_QUICK_BACKEND=software`), find the item by
  objectName, drive it through public C++ API and through
  `QWindowSystemInterface` (tablet) / `QTest::mouseClick(window, …)`, and
  assert on the document — the same assertions as today.
- **Chrome**: `qtk-preview <window>.qml --dump --size WxH` fixtures per
  app at 1280×800 and 520×700, asserting every toolbar action id has a
  visible button inside the bar (D-013's squeeze test), the four window
  bands, and no `0x0` control; plus `--grab` PNGs reviewed with the Read
  tool before each milestone commit (not committed).
- **Menu export**: per app, snapshot → validate → every id resolves →
  `trigger` reaches a handler (V5's class of bug).
- **Journal/recovery/recents/inventory**: `office_core` tests unchanged;
  `tst_document_host` covers consent, pristine reuse, auto-save.
- **Manual (desktop)**: the checklist in `docs/verification.md`.

## 8. Risks and their mitigations

| Risk | Mitigation |
|------|-----------|
| `QQuickTextControl` private API drifts between Qt releases | Same exposure the Widgets build has with `QWidgetTextControl`; isolate behind `PageTextItem`; pin `>=qtbase-6.11` in the ebuild as today. |
| Portal file dialogs unavailable outside the desktop | `QtQuick.Dialogs` QML fallback; verified visibly, never blocking (rule 9). |
| Performance of `QQuickPaintedItem` for big spreadsheets / long ink pages | Narrow `update(rect)` as M22 does; `renderTarget: FramebufferObject` where the software path is slow; measure with 5 000 strokes / 10 000 rows before M28/M27 gates. Move to QSG nodes later only if measured. |
| Accessibility of custom items | `QAccessibleQuickItem` subclasses for the text item; `Accessible.*` on every custom item; Orca smoke test on the desktop. |
| Two copies of QindaTK drifting | §0.4 (git + bare remote) before M27. |
| Kimi-style regressions | Every milestone gate includes the on-desktop panel activation and the `--dump`/`--grab` evidence; no milestone is "done" on green tests alone. |

## 9. Out of scope

Tabs/MDI (rule 1); QSG-node rewrites of the painted items; handwriting
recognition; touch gestures on the page; replacing the desktop's exporter
with `AppShell` (V3 forbids it for this suite); porting the three Widgets
apps in M27 (they keep building until their milestone).

## 10. Definition of done

- No `QApplication`, `QWidget`, `QMenuBar`, `QDialog` or `QGraphicsView`
  in the suite; `office_shell` deleted; every app a `QGuiApplication` with
  a `Tk.AppWindow`.
- Every command reachable from the panel menu **and** activatable from
  it, with radio groups and submenus intact; local bar only when the
  export fails.
- Every document format, renderer, PDF export and engine test unchanged
  and green; every UI test ported; `qtk-preview` fixtures in ctest.
- QindaTK additions A1–A7 merged in the toolkit with docs, gallery and
  tests; the toolkit packaged; the suite's ebuild depends on it.
- `AGENTS.md`, `docs/index.md`, `docs/verification.md`, `docs/decisions.md`
  (D-027, D-028) current.
