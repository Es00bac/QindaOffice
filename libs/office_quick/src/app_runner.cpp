// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_runner.h"

#include "desktop_appearance.h"

#include "document_host.h"
#include "office_application.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>

// AGENT-GUARD: QindaOffice.Shell is a STATIC QML module. Its type
// registration lives in the plugin's init object library, which CMake
// propagates to every target that links `office_quickplugin` — so every
// app and every QML test links BOTH office_quick and office_quickplugin,
// or `import QindaOffice.Shell` fails with "module not installed".

namespace QindaOffice::Quick {

int runOfficeApp(int &argc, char *argv[], const QString &desktopId, const QUrl &windowUrl,
                 const std::function<DocumentHost *()> &hostFactory)
{
    QGuiApplication application(argc, argv);

    const QString binary = QFileInfo(QString::fromLocal8Bit(argv[0])).completeBaseName();
    QGuiApplication::setApplicationName(binary);
    QGuiApplication::setApplicationDisplayName(desktopId.section(QLatin1Char('.'), -1));
    QGuiApplication::setOrganizationName(QStringLiteral("QindaQt"));
    // AGENT-CONTRACT (shell spec): the desktop file name must be pinned
    // before any window maps - the compositor reads the app_id at first map
    // and the panel's launcher/menu registrar match on it.
    QGuiApplication::setDesktopFileName(desktopId);
    QGuiApplication::setApplicationVersion(QStringLiteral(QQO_VERSION));
    // The toolkit's controls are QtQuick.Templates with their own visuals;
    // the Controls style only matters for the QtQuick.Dialogs fallbacks.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QGuiApplication::applicationDisplayName() + QStringLiteral(" — QindaQt Office"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption newInstance(QStringLiteral("new-instance"),
                                   QStringLiteral("Launch a second independent process"));
    parser.addOption(newInstance);
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Documents to open"),
                                 QStringLiteral("[paths...]"));
    parser.process(application);
    const QStringList paths = parser.positionalArguments();

    QQmlApplicationEngine engine;
#ifdef QQO_QINDATK_QML_ROOT
    // The toolkit's QML modules, from wherever the build found them
    // (system Qt import path, or a local install prefix during development).
    engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
#endif
    // The desktop's theme, published before any window derives its
    // palette; without a desktop the windows keep the Preferences preset.
    QString appearanceError;
    if (!DesktopAppearance::publishDesktopTheme(engine, &appearanceError)) {
        qInfo("QindaOffice: desktop theme not published (%s)", qPrintable(appearanceError));
    }

    OfficeApplication office;
    office.setHostFactory(hostFactory);
    office.setWindowComponent(&engine, windowUrl);
    QObject::connect(&office, &OfficeApplication::diagnosticReported, &office,
                     [](const QString &message) { qWarning().noquote() << message; });
    if (!parser.isSet(newInstance)) {
        bool handedOff = false;
        if (!office.acquirePrimary(paths, &handedOff) && handedOff) {
            return 0; // another instance owns the name and took the paths
        }
        // Not acquired and not handed off (no session bus): run standalone.
    }

    office.offerRecovery();
    const bool recovered = office.windowCount() > 0;
    if (!paths.isEmpty()) {
        QString diagnostic;
        if (!office.openPaths(paths, &diagnostic)) {
            qWarning().noquote() << diagnostic;
        }
    } else if (!recovered && office.restorePreviousDocuments()) {
        office.restoreFromInventory();
    }
    if (office.windowCount() == 0 && office.newWindow() == nullptr) {
        qWarning().noquote() << "could not create the first window";
        return 1;
    }
    return QGuiApplication::exec();
}

} // namespace QindaOffice::Quick
