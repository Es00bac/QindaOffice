// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the launcher knows about the suite: its apps, and the recent
// documents each of them has kept.
//
// AGENT-CONTRACT: the launcher READS the other apps' state and never writes
// it. Recents live in each app's own config dir (rule 7: bounded
// persistence, per app, never shared), so this walks one file per app rather than
// introducing a shared one — a shared list would be a new cross-app store
// that rule exists to prevent.

#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Launcher {

struct AppEntry {
    QString id;          // "QindaWrite"
    QString desktopId;   // "org.qindaqt.QindaWrite"
    QString binary;      // "qindawrite"
    QString name;        // "QindaWrite"
    QString role;        // "Word processor"
    QString blurb;       // one line, what it is for
    QString newLabel;    // what "new" makes here
};

struct RecentEntry {
    QString path;
    QString fileName;
    QString folder;
    QString appId;
};

// The suite's apps, in suite order.
[[nodiscard]] QList<AppEntry> officeApps();

// A URL for the app's mascot, or empty when it is not installed (the
// launcher then draws its own initial-letter tile rather than a blank
// square). The suite installs its mascots into hicolor, which is where a
// theme lookup would have found them too; QML needs a URL, so the file is
// located directly.
[[nodiscard]] QString iconSourceFor(const AppEntry &app);

// Every app's recent list, newest first, bounded to `limit` in total.
// Paths that no longer exist are skipped: offering a document that is gone
// is worse than a shorter list.
[[nodiscard]] QList<RecentEntry> recentDocuments(int limit = 12);

// Which app owns a path. The suffix is the only thing the launcher can go
// on, and it is the same thing every app's own load() dispatches on; a
// notebook is a DIRECTORY, so anything with no known suffix is QindaNote's.
[[nodiscard]] QString appIdForPath(const QString &path);

// The file-dialog filters covering every type the four apps open, in the
// form QtQuick.Dialogs wants ("Spreadsheets (*.xlsx *.ods)").
[[nodiscard]] QStringList openNameFilters();

// Absolute path to the app's binary: the install prefix first, then the
// launcher's own directory so a build tree runs without installing.
[[nodiscard]] QString resolveBinary(const AppEntry &app);

// Starts `app`, optionally on `path`. False when the binary is missing.
[[nodiscard]] bool launch(const AppEntry &app, const QString &path = {});

} // namespace QindaOffice::Launcher
