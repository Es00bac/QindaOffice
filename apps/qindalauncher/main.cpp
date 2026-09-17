// SPDX-License-Identifier: GPL-3.0-or-later
// QindaQt Office launcher: the suite's front door.
//
// Not a document window, so it does not go through runOfficeApp — but the
// desktop identity is pinned before anything maps, exactly as rule 2
// requires, or the compositor cannot match the window to its .desktop file
// and the launcher gets no icon in the panel or the task switcher.

#include "launcher_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral(QQO_DESKTOP_ID));
    QApplication::setApplicationName(QStringLiteral("qindaoffice"));
    QApplication::setApplicationDisplayName(QApplication::translate("main", "QindaQt Office"));
    QApplication::setApplicationVersion(QStringLiteral(QQO_VERSION));

    // The four apps answer --help and --version through app_runner; the
    // launcher does not go through it, so it has to answer for itself or it
    // would be the one binary in the suite that swallows them and opens a
    // window instead. It takes no paths: its .desktop file declares no
    // MimeType and its Exec has no %f, because the apps own their own files.
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::translate("main", "QindaQt Office — the suite's front door"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    // The task switcher and the window container both read the window icon
    // when the .desktop file is not installed (a build-tree run).
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(QQO_DESKTOP_ID)));

    QindaOffice::Launcher::LauncherWindow window;
    window.show();
    return QApplication::exec();
}
