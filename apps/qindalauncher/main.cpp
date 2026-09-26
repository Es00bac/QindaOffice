// SPDX-License-Identifier: GPL-3.0-or-later
// QindaQt Office launcher: the suite's front door.
//
// Not a document window, so it does not go through runOfficeApp — but the
// desktop identity is pinned before anything maps, exactly as the rules
// require, or the compositor cannot match the window to its .desktop file
// and the launcher gets no icon in the panel or the task switcher.

#include "src/launcher_controller.h"

#include "desktop_appearance.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral(QQO_DESKTOP_ID));
    QGuiApplication::setApplicationName(QStringLiteral("qindaoffice"));
    QGuiApplication::setApplicationDisplayName(
        QGuiApplication::translate("main", "QindaQt Office"));
    QGuiApplication::setApplicationVersion(QStringLiteral(QQO_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The four apps answer --help and --version through app_runner; the
    // launcher does not go through it, so it has to answer for itself or it
    // would be the one binary in the suite that swallows them and opens a
    // window instead. It takes no paths: its .desktop file declares no
    // MimeType and its Exec has no %f, because the apps own their own files.
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QGuiApplication::translate("main", "QindaQt Office — the suite's front door"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    // The task switcher and the window container both read the window icon
    // when the .desktop file is not installed (a build-tree run).
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(QQO_DESKTOP_ID)));

    // AGENT-GUARD: the engine owns LauncherWindow.qml. It must be destroyed
    // before the controller that fills the window's required property, or
    // its bindings briefly read a null launcher during normal shutdown.
    QindaOffice::Launcher::LauncherController controller;
    QQmlApplicationEngine engine;
#ifdef QQO_QINDATK_QML_ROOT
    engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
#endif
    QString appearanceError;
    if (!QindaOffice::Quick::DesktopAppearance::publishDesktopTheme(engine, &appearanceError)) {
        qInfo("QindaOffice: desktop theme not published (%s)", qPrintable(appearanceError));
    }
    engine.setInitialProperties({{QStringLiteral("launcher"), QVariant::fromValue(&controller)}});
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Launcher/LauncherWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return QGuiApplication::exec();
}
