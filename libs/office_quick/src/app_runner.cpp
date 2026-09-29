// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_runner.h"
#include "agent_runtime.h"

#include "desktop_appearance.h"

#include "document_host.h"
#include "menu_tree.h"
#include "office_application.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>

#include <memory>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

// AGENT-GUARD: QindaOffice.Shell is a STATIC QML module. Its type
// registration lives in the plugin's init object library, which CMake
// propagates to every target that links `office_quickplugin` — so every
// app and every QML test links BOTH office_quick and office_quickplugin,
// or `import QindaOffice.Shell` fails with "module not installed".

namespace QindaOffice::Quick {

namespace {

// --list-commands: one line per command of a fresh window's menu tree —
// id, label, shortcut, icon, menu path — tab-separated. The ids are the
// stable activation identities (AGENTS.md rule 5), so this is the table
// keybinding docs, panel scripts and ribbon layouts are written against.
void listCommands(const QList<MenuNode> &nodes, const ActionModel *actions, const QString &path,
                  QTextStream &out)
{
    for (const MenuNode &node : nodes) {
        if (node.kind == MenuNode::Kind::Submenu) {
            QString title = node.title;
            title.remove(QLatin1Char('&'));
            listCommands(node.children, actions, path.isEmpty() ? title : path + QStringLiteral(" > ") + title,
                         out);
        } else if (node.kind == MenuNode::Kind::Action) {
            const ActionEntry *entry = actions->entry(node.id);
            if (entry != nullptr) {
                out << node.id << '\t' << entry->plainText() << '\t' << entry->shortcutText() << '\t'
                    << entry->iconName() << '\t' << path << '\n';
            }
        }
    }
}

} // namespace

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
    QCommandLineOption agentRead(QStringLiteral("agent-read"),
        QStringLiteral("Allow local agents to read documents in this new process"));
    QCommandLineOption agentEdit(QStringLiteral("agent-edit"),
        QStringLiteral("Allow local agents to read and edit documents in this new process"));
    parser.addOption(agentRead);
    parser.addOption(agentEdit);
    QCommandLineOption listCommandsOption(QStringLiteral("list-commands"),
                                          QStringLiteral("Print every command (id, label, shortcut, icon, menu) and exit"));
    parser.addOption(listCommandsOption);
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Documents to open"),
                                 QStringLiteral("[paths...]"));
    parser.process(application);
    const QStringList paths = parser.positionalArguments();
    if (parser.isSet(agentRead) && parser.isSet(agentEdit)) {
        qWarning("Choose --agent-read or --agent-edit");
        return 2;
    }
    if (parser.isSet(listCommandsOption)) {
        std::unique_ptr<DocumentHost> host(hostFactory());
        QTextStream out(stdout);
        listCommands(host->menuTree()->topLevel(), host->actions(), {}, out);
        return 0;
    }

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
    AgentRuntime agent(&office, parser.isSet(agentEdit) ? 2 : parser.isSet(agentRead) ? 1 : 0);
    QString agentError;
    if (!agent.start(&agentError)) {
        qWarning("Local agent endpoint unavailable: %s", qPrintable(agentError));
        if (parser.isSet(agentRead) || parser.isSet(agentEdit)) return 2;
    }
    QObject::connect(&office, &OfficeApplication::diagnosticReported, &office,
                     [](const QString &message) { qWarning().noquote() << message; });
    if (!parser.isSet(newInstance) && !parser.isSet(agentRead) && !parser.isSet(agentEdit)) {
        bool handedOff = false;
        if (!office.acquirePrimary(paths, &handedOff) && handedOff) {
            return 0; // another instance owns the name and took the paths
        }
        // Not acquired and not handed off (no session bus): run standalone.
    }

    // The documents the launch asked for, the restore inventory, or a blank
    // window — after the recovered-documents question, when there is one.
    const auto openStartDocuments = [&office, paths](bool recovered) {
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
            return false;
        }
        return true;
    };

    // AGENT-NOTE (D-055): unsaved documents from an earlier run are offered
    // in ONE dialog instead of one window each (up to 32 windows at once).
    const QVariantList pending = office.pendingRecovery();
    if (pending.isEmpty()) {
        if (!openStartDocuments(false)) {
            return 1;
        }
        return QGuiApplication::exec();
    }
    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Shell/RecoveryDialog.qml")));
    QObject *dialog = component.createWithInitialProperties(
        {{QStringLiteral("entries"), pending},
         {QStringLiteral("office"), QVariant::fromValue(static_cast<QObject *>(&office))}});
    if (dialog == nullptr) {
        // Fail visibly but keep the work: without the dialog, reopen them.
        qWarning().noquote() << "recovered-documents dialog failed:" << component.errorString();
        QStringList keys;
        for (const QVariant &row : pending) {
            keys.append(row.toMap().value(QStringLiteral("key")).toString());
        }
        office.recoverEntries(keys);
        if (!openStartDocuments(office.windowCount() > 0)) {
            return 1;
        }
        return QGuiApplication::exec();
    }
    QObject::connect(&office, &OfficeApplication::recoveryResolved, &office,
                     [openStartDocuments, dialog](bool recovered) {
                         if (!openStartDocuments(recovered)) {
                             QCoreApplication::exit(1);
                         }
                         dialog->deleteLater();
                     });
    return QGuiApplication::exec();
}

} // namespace QindaOffice::Quick
