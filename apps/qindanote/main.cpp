// SPDX-License-Identifier: GPL-3.0-or-later
// QindaQt Office application entry point (M0 skeleton).
//
// AGENT-CONTRACT (ADR-0110 / shell spec): the desktop file name must be set
// before any window maps - the compositor reads the app_id/xdg-shell identity
// at first map, and the panel's launcher and menu registrar match on it.
// QQO_DESKTOP_ID is the org.qindaqt.<App> id from the app's .desktop file.

#include "office_application.h"
#include "document_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral(QQO_DESKTOP_ID));

    QindaOffice::Shell::OfficeApplication office;
    const QStringList paths = QCoreApplication::arguments().mid(1);
    if (paths.isEmpty()) {
        office.newWindow();
    } else {
        QString diagnostic;
        office.openPaths(paths, &diagnostic);
        if (!diagnostic.isEmpty()) {
            qWarning().noquote() << diagnostic;
        }
    }
    return QApplication::exec();
}
