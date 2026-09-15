// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QString;

namespace QindaOffice::Shell {

// Shared main() for the four office apps. Owns the QApplication, pins the
// desktop identity (QQO_DESKTOP_ID), parses the common CLI (--new-instance,
// positional paths), wires the single-instance hand-off, then drives
// OfficeApplication startup: journal recovery offers, explicit CLI paths
// (highest precedence), restore inventory (when enabled), blank window.
[[nodiscard]] int runOfficeApp(int &argc, char *argv[], const QString &desktopId);

} // namespace QindaOffice::Shell
