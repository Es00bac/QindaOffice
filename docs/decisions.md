// SPDX-License-Identifier: GPL-3.0-or-later
# Decisions (ADR-lite)

Durable cross-cutting choices, newest first. Each entry: context, decision,
consequences. For desktop-level architecture see
`container-wm/docs/wiki/adr/`.

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
