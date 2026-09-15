// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_runner.h"

#include "office_application.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

namespace QindaOffice::Shell {

int runOfficeApp(int &argc, char *argv[], const QString &desktopId)
{
    QApplication application(argc, argv);

    const QString binary = QFileInfo(QString::fromLocal8Bit(argv[0])).completeBaseName();
    QApplication::setApplicationName(binary);
    QApplication::setApplicationDisplayName(desktopId.section(QLatin1Char('.'), -1));
    QApplication::setOrganizationName(QStringLiteral("QindaQt"));
    // AGENT-CONTRACT (shell spec): the desktop file name must be pinned
    // before any window maps - the compositor reads the app_id at first map
    // and the panel's launcher/menu registrar match on it.
    QApplication::setDesktopFileName(desktopId);
    QApplication::setApplicationVersion(QStringLiteral(QQO_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::applicationDisplayName() + QStringLiteral(" — QindaQt Office"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption newInstance(QStringLiteral("new-instance"),
                                   QStringLiteral("Launch a second independent process"));
    parser.addOption(newInstance);
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Documents to open"),
                                 QStringLiteral("[paths...]"));
    parser.process(application);
    const QStringList paths = parser.positionalArguments();

    OfficeApplication office;
    if (!parser.isSet(newInstance)) {
        bool handedOff = false;
        if (!office.acquirePrimary(paths, &handedOff) && handedOff) {
            // Another instance owns the name and took the paths: clean exit.
            return 0;
        }
        // Not acquired and not handed off (no session bus): fall through and
        // run as a standalone process - degraded, never blocked.
    }

    office.offerRecovery();
    const bool recovered = office.windowCount() > 0;
    if (!paths.isEmpty()) {
        // Explicit CLI paths take precedence over restore policy (ADR-0110).
        QString diagnostic;
        if (!office.openPaths(paths, &diagnostic)) {
            qWarning().noquote() << diagnostic;
        }
    } else if (!recovered && office.restorePreviousDocuments()) {
        office.restoreFromInventory();
    }
    if (office.windowCount() == 0) {
        office.newWindow();
    }
    return QApplication::exec();
}

} // namespace QindaOffice::Shell
