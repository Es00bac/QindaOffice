// SPDX-License-Identifier: GPL-3.0-or-later
// QindaQt Office launcher: the suite's front door.
//
// Not a document window, so it does not go through runOfficeApp — but the
// desktop identity is pinned before anything maps, exactly as rule 2
// requires, or the compositor cannot match the window to its .desktop file
// and the launcher gets no icon in the panel or the task switcher.

#include "launcher_window.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral(QQO_DESKTOP_ID));
    QApplication::setApplicationName(QStringLiteral("qindaoffice"));
    QApplication::setApplicationDisplayName(QApplication::translate("main", "QindaQt Office"));
    QApplication::setApplicationVersion(QStringLiteral(QQO_VERSION));

    // The task switcher and the window container both read the window icon
    // when the .desktop file is not installed (a build-tree run).
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(QQO_DESKTOP_ID)));

    QindaOffice::Launcher::LauncherWindow window;
    window.show();
    return QApplication::exec();
}
