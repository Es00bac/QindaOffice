// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

class QString;
class QUrl;

namespace QindaOffice::Quick {

class DocumentHost;

// Shared main() for the office apps. Owns the QGuiApplication and the one
// QQmlApplicationEngine of the process, pins the desktop identity
// (QQO_DESKTOP_ID), parses the common CLI (--new-instance, positional
// paths), wires the single-instance hand-off, then drives
// OfficeApplication startup: journal recovery, explicit CLI paths (highest
// precedence), restore inventory (when enabled), blank window.
//
// `windowUrl` is the app's window component (a Tk.AppWindow with a
// `required property DocumentHost host`); `hostFactory` builds the app's
// DocumentHost subclass with its document engine installed.
[[nodiscard]] int runOfficeApp(int &argc, char *argv[], const QString &desktopId,
                               const QUrl &windowUrl,
                               const std::function<DocumentHost *()> &hostFactory);

} // namespace QindaOffice::Quick
