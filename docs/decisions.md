// SPDX-License-Identifier: GPL-3.0-or-later
# Decisions (ADR-lite)

Durable cross-cutting choices, newest first. Each entry: context, decision,
consequences. For desktop-level architecture see
`container-wm/docs/wiki/adr/`.

## D-003: App-menu hooks are finalized explicitly after subclass construction

**Context.** `DocumentWindow::createMenus()` runs inside the base
constructor, so its `insertApplicationMenus()` / `insertFileMenuActions()`
calls dispatch to the base no-ops — the subclass vtable does not exist
yet. The contract said "the subclass ctor calls the hooks again", but only
QindaShow did: at runtime QindaWrite, QindaCalc and QindaNote had **no app
menus at all** (no Format/Insert/Page/Sheet), and the template submenu was
never inserted anywhere. Surfaced by the M11 UI-completeness report.

**Decision.** `DocumentWindow::finalizeAppMenuHooks()` (public,
idempotent) runs both hooks against the named `fileMenu`/`aiMenu` anchors.
Every app ctor calls it once its own state exists, and `showEvent` calls
it again as a safety net before the global-menu export snapshots the menu
bar.

**Consequences.** All four apps reliably have their app menus before first
show; a future app that forgets the ctor call still works via the
showEvent net. The two-pass comment in `createMenus()` still describes
why the base pass is a no-op.

## D-004: The page is paper, not chrome — pinned light palette for the document surface

**Context.** `PageTextView` filled the page with `palette().base()` and
let the text control inherit the desktop palette. Under the QindaQt dark
platform theme that made the *page* dark, while .docx imports (LibreOffice
HTML bridge) carry explicit white block backgrounds — the "white boxes
floating on dark" rendering bug. New typed text also came out light-on-dark.

**Decision.** ADR-0115 (stock Qt + platform theme) governs window chrome;
a document page is *paper* and must match the print/PDF path. The view
pins a paper palette (white Base/Window, black Text/WindowText) on the
viewport and the `QWidgetTextControl`; only the canvas between pages
follows the theme. Recorded with an AGENT-NOTE in `page_text_view.cpp`.

**Consequences.** Documents render identically under light and dark
desktop themes and match exported PDFs. Any future editor surface for
paged documents must make the same pin (QindaNote's page view already owns
its paper color).

## D-005: QTextDocument equivalents for word-processor features

**Context.** The M11 formatting toolbar needed column layouts and exposed
two Qt behaviors that leak into the product.

**Decision.** (a) Multi-column text: `QTextDocument` has no native
column-section layout (ODF `text:columns`), so *Insert ▸ Columns* inserts
a borderless 1×N table — the serialization-safe equivalent that
round-trips through HTML/ODT/DOCX (AGENT-NOTE in
`format_controller.cpp`). (b) `QTextDocument::setPageSize()` records an
undo item (Qt 6.11), so every pristine document was born with a phantom
undo step; `WriteDocument` clears the stacks after its initial paged
setup (AGENT-NOTE in `write_document.cpp`).

**Consequences.** Pristine documents have an empty undo stack (Undo
disabled until the first real edit); column layouts survive every export
format. Revisit (a) if Qt ever grows native text columns.

## D-001: Repository directory is `QindaOffice`, not `QindaQt_Office`

**Context.** The implementation plan (§1, §4) names the new repo
`~/work_SPaC3/QindaQt_Office`. The user's prepared workspace is
`~/work_SPaC3/QindaOffice` (this directory), which is where the session and
the writable sandbox root point.

**Decision.** The repository lives in `QindaOffice`. The CMake project
name, desktop IDs, namespace and suite name stay exactly as planned
(`qindaoffice`, `org.qindaqt.Qinda*`, `QindaOffice`, "QindaQt Office").

**Consequences.** The ebuild `EGIT_REPO_URI` points at this checkout. If a
remote is created later it can be named either way; nothing in the build
depends on the directory name.

## D-002: D-Bus hand-off uses raw method calls; QDBusInterface introspection can hang

**Context.** The single-instance hand-off (ported from QindaQt_Apps) used
`QDBusInterface` to call `OpenPaths`. In this development environment the
session bus accepts connections but denies Qt name ownership
(`registerService` returns `ServiceNotRegistered` where `busctl` succeeds),
and the `QDBusInterface` constructor's blocking introspection then parked
the calling thread forever — observed as a main-thread futex wait with no
bus socket ever opened.

**Decision.** `SingleInstance::invokeOpenPaths` builds a raw
`QDBusMessage::createMethodCall` and `QDBusConnection::call(message,
QDBus::Block, 2000)` — no introspection, explicit timeout, fails fast. When
the bus denies registration or the primary is unreachable,
`acquireOrHandOff` returns "not acquired, not handed off" and the runner
falls through to a standalone degraded process (never blocks). The
single-instance test spawns its own private `dbus-daemon` so it is hermetic
and skips with an explicit message where buses are unavailable.

**Consequences.** Degraded D-Bus environments degrade visibly instead of
hanging — the same rule as the global-menu export. QQTerm carries the same
`QDBusInterface` hand-off pattern and may need the same hardening if it ever
runs against such a bus.
