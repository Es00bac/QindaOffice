// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace QindaOffice::Core {

// Per-app, never-shared directory roots (plan section 2.7). Each call
// creates the directory, so a missing XDG home degrades to a clear fs error
// at the call site rather than a silent fallback.
//
// AGENT-NOTE: Qt 6.11 has no QStandardPaths::AppStateDataLocation; the state
// root is composed from GenericStateLocation exactly as the XDG spec's
// default (~/.local/state) does.
[[nodiscard]] QString configDirFor(const QString &appLeaf);
[[nodiscard]] QString stateDirFor(const QString &appLeaf);

// Desktop-file leaf of the running application (org.qindaqt.QindaWrite ->
// QindaWrite); empty when desktopFileName was not pinned.
[[nodiscard]] QString appLeaf();

} // namespace QindaOffice::Core
