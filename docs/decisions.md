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
