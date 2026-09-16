// SPDX-License-Identifier: GPL-3.0-or-later
#include "global_menu_export.h"

#include <qindaqt/shell/global_menu/dbusmenu/dbusmenu_server.h>
#include <qindaqt/shell/global_menu/exporter/menu_exporter.h>
#include <qindaqt/shell/global_menu/qt_widgets_adapter/qmenubar_menu_source.h>

#include <QAction>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QWindow>
#include <QtGui/private/qdesktopunixservices_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformintegration.h>

#include <limits>

namespace QindaOffice::Shell {
namespace {

// Per-process monotonic endpoint counter so each window owns a distinct
// object path on the shared bus connection.
quint32 nextEndpointIndex()
{
    static quint32 index = 0;
    return ++index;
}

// AGENT-CONTRACT: one dbusmenu object path per window on the shared session
// bus: /org/qindaqt/QindaOffice/<App>/Menu/<n>. <App> is the desktop-file
// leaf (org.qindaqt.QindaWrite -> QindaWrite), matching the plan's shell spec.
QString endpointPath()
{
    QString leaf = QGuiApplication::desktopFileName().section(QLatin1Char('.'), -1);
    if (leaf.isEmpty()) {
        leaf = QStringLiteral("App");
    }
    return QStringLiteral("/org/qindaqt/QindaOffice/%1/Menu/%2")
        .arg(leaf)
        .arg(nextEndpointIndex());
}

void collectMenus(QMenu *menu, QList<QMenu *> &into)
{
    if (menu == nullptr || into.contains(menu)) {
        return;
    }
    into.append(menu);
    for (QAction *action : menu->actions()) {
        if (action->menu() != nullptr) {
            collectMenus(action->menu(), into);
        }
    }
}

} // namespace

GlobalMenuExport::GlobalMenuExport(QMenuBar *menuBar, QWindow *window, QObject *parent)
    : QObject(parent),
      m_menuBar(menuBar),
      m_window(window),
      m_bus(QDBusConnection::sessionBus()),
      m_windowUuid(QUuid::createUuid()),
      m_objectPath(endpointPath())
{
}

GlobalMenuExport::~GlobalMenuExport()
{
    stop();
}

bool GlobalMenuExport::start()
{
    if (m_menuBar == nullptr || m_window == nullptr || m_bus.isConnected() == false) {
        return false;
    }
    if (m_server != nullptr) {
        return m_exported;
    }

    m_server = new QindaQt::Shell::GlobalMenu::DbusMenu::DbusMenuServer(this);
    if (m_bus.registerObject(m_objectPath, m_server,
                             QDBusConnection::ExportScriptableContents)
        == false) {
        qWarning().noquote() << qGuiApp->desktopFileName()
                             << "could not register menu endpoint" << m_objectPath;
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    // AGENT-GUARD: the activation callback must resolve by the stable
    // objectName StandardActions stamped; no other id space is valid here.
    connect(m_server,
            &QindaQt::Shell::GlobalMenu::DbusMenu::DbusMenuServer::actionActivated, this,
            [this](const QString &stableActionId) {
                if (QAction *target = m_menuBar->findChild<QAction *>(stableActionId);
                    target != nullptr) {
                    target->trigger();
                }
            });

    m_source =
        std::make_unique<QindaQt::Shell::GlobalMenu::QtWidgetsAdapter::QMenuBarMenuSource>(
            m_menuBar, m_windowUuid);
    m_exporter = std::make_unique<QindaQt::Shell::GlobalMenu::Exporter::MenuExporter>(
        *m_source, m_lineage);

    observeMenuTree();

    if (announce() == false) {
        // Fail closed: no announcement means the panel can never show this
        // menu; keep the endpoint so a later retry could succeed, but report
        // the local bar as authoritative.
        return false;
    }
    refresh();
    m_exported = true;
    emit exportedChanged(true);
    return true;
}

void GlobalMenuExport::stop()
{
    if (m_server == nullptr) {
        return;
    }
    withdraw();
    m_bus.unregisterObject(m_objectPath);
    m_source.reset();
    m_exporter.reset();
    m_server->deleteLater();
    m_server = nullptr;
    if (m_exported) {
        m_exported = false;
        emit exportedChanged(false);
    }
}

void GlobalMenuExport::observeMenuTree()
{
    // Any structural or state change re-snapshots. QMenu::aboutToShow covers
    // property flips made right before a menu opens; QAction::changed covers
    // enable/checked/text changes made programmatically. Coalesced into one
    // snapshot per event-loop pass.
    //
    // AGENT-GUARD: Qt::UniqueConnection, because this runs again on every
    // refresh(). Menus and actions created AFTER start() — the Open Recent
    // items are destroyed and rebuilt on every open and save — were never
    // observed, so their changes reached the panel only by accident and the
    // exported tree drifted out of step with the window's own menu bar.
    QList<QMenu *> menus;
    for (QAction *titleAction : m_menuBar->actions()) {
        if (titleAction->menu() != nullptr) {
            collectMenus(titleAction->menu(), menus);
        }
    }
    for (QMenu *menu : menus) {
        connect(menu, &QMenu::aboutToShow, this, &GlobalMenuExport::scheduleRefresh,
                Qt::UniqueConnection);
    }
    const QList<QAction *> actions = m_menuBar->findChildren<QAction *>();
    for (QAction *action : actions) {
        connect(action, &QAction::changed, this, &GlobalMenuExport::scheduleRefresh,
                Qt::UniqueConnection);
    }
}

void GlobalMenuExport::scheduleRefresh()
{
    QTimer::singleShot(0, this, &GlobalMenuExport::refresh);
}

void GlobalMenuExport::refresh()
{
    if (m_exporter == nullptr || m_server == nullptr) {
        return;
    }
    // Pick up menus/actions added since the last pass before snapshotting.
    observeMenuTree();
    const auto result = m_exporter->refresh();
    switch (result.outcome) {
    case QindaQt::Shell::GlobalMenu::Exporter::ExportOutcome::Published: {
        const auto accepted = m_exporter->lastAccepted();
        if (accepted.has_value() && m_server->publish(*accepted) == false) {
            // Validation/exhaustion inside the server; the previous tree
            // stays authoritative on the wire.
            qWarning().noquote() << qGuiApp->desktopFileName() << "dbusmenu publish failed";
        }
        break;
    }
    case QindaQt::Shell::GlobalMenu::Exporter::ExportOutcome::Unchanged:
        break;
    default:
        qWarning().noquote() << qGuiApp->desktopFileName()
                             << "menu export rejected:" << result.defectCode;
        break;
    }
}

bool GlobalMenuExport::announce()
{
    const QString service = m_bus.baseService();
    if (service.isEmpty()) {
        return false;
    }
    const QString platform = QGuiApplication::platformName();
    if (platform.startsWith(QLatin1String("wayland"))) {
        // AGENT-CONTRACT: Qt's Wayland services owns the KDE appmenu protocol
        // object tied to this wl_surface; this is the announcement the
        // qindaqt-shell registrar resolves and PID-authenticates. Same
        // mechanism as the first-party menu export (Qt 6.11 private API; the
        // ebuild pins >=qtbase-6.11 so the symbol always exists).
        auto *services = dynamic_cast<QDesktopUnixServices *>(
            QGuiApplicationPrivate::platformIntegration()->services());
        if (services == nullptr) {
            return false;
        }
        services->registerDBusMenuForWindow(m_window, service, m_objectPath);
        m_announcedOnWayland = true;
        return true;
    }
    if (platform == QLatin1String("xcb")) {
        const auto nativeId = m_window->winId();
        if (nativeId == 0 || nativeId > std::numeric_limits<quint32>::max()) {
            return false;
        }
        m_x11WindowId = static_cast<quint32>(nativeId);
        QDBusMessage registration = QDBusMessage::createMethodCall(
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("/com/canonical/AppMenu/Registrar"),
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("RegisterWindow"));
        registration.setArguments(
            {m_x11WindowId, QVariant::fromValue(QDBusObjectPath(m_objectPath))});
        QDBusMessage reply = m_bus.call(registration);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning().noquote() << qGuiApp->desktopFileName()
                                 << "appmenu registrar unavailable:" << reply.errorMessage();
            return false;
        }
        m_registeredOnX11 = true;
        return true;
    }
    return false;
}

void GlobalMenuExport::withdraw()
{
    if (m_announcedOnWayland) {
        if (auto *services = dynamic_cast<QDesktopUnixServices *>(
                QGuiApplicationPrivate::platformIntegration()->services());
            services != nullptr) {
            services->unregisterDBusMenuForWindow(m_window);
        }
        m_announcedOnWayland = false;
    }
    if (m_registeredOnX11) {
        QDBusMessage unregistration = QDBusMessage::createMethodCall(
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("/com/canonical/AppMenu/Registrar"),
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("UnregisterWindow"));
        unregistration.setArguments({m_x11WindowId});
        m_bus.asyncCall(unregistration);
        m_registeredOnX11 = false;
    }
}

} // namespace QindaOffice::Shell
