// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-window global menu export for the QindaQt desktop panel.
//
// AGENT-CONTRACT (ported verbatim from QindaQt_Apps/src/dbus/global_menu_export,
// mirrors the first-party composition documented under docs/wiki shell/global-menu):
// the export is OPT-IN and FAIL-CLOSED. start() returns false whenever the
// endpoint cannot be published and announced; the caller must then keep the
// local QMenuBar visible. The panel binds the menu only for the focused
// window and authenticates the endpoint by bus-owner PID, which trivially
// matches because DbusMenuServer lives in this process.
//
// Composition: QMenuBarMenuSource (snapshot of the live QMenuBar) ->
// MenuExporter (validates + stamps epoch/revision lineage) -> DbusMenuServer
// (standard com.canonical.dbusmenu v4 endpoint) -> per-window Wayland
// announcement through Qt's private QDesktopUnixServices (no X11 id, no
// XWayland), or the canonical registrar under xcb.

#include <qindaqt/shell/global_menu/exporter/menu_exporter.h>

#include <QDBusConnection>
#include <QObject>
#include <QUuid>

class QMenuBar;
class QWindow;

namespace QindaQt::Shell::GlobalMenu::DbusMenu {
class DbusMenuServer;
}

namespace QindaQt::Shell::GlobalMenu::QtWidgetsAdapter {
class QMenuBarMenuSource;
}

namespace QindaOffice::Shell {

class GlobalMenuExport final : public QObject {
    Q_OBJECT
public:
    // menuBar and window must outlive this object (MenuSource lifetime
    // contract). menuBar is the authoritative action tree to export.
    GlobalMenuExport(QMenuBar *menuBar, QWindow *window, QObject *parent = nullptr);
    ~GlobalMenuExport() override;

    // Builds and registers the dbusmenu endpoint, then announces it for the
    // window. Returns true only when the announcement succeeded; the local
    // menu bar stays visible otherwise.
    [[nodiscard]] bool start();

    // Withdraws the announcement and unregisters the endpoint.
    void stop();

    [[nodiscard]] bool isExported() const { return m_exported; }

    // Re-snapshots and republishes when the content changed. Called
    // automatically on menu changes; also useful after programmatic edits.
    void refresh();

signals:
    void exportedChanged(bool exported);

private:
    // MenuExporter::ExportLineageSource: one epoch per window, revision
    // advancing on every pull. MenuExporter only accepts advancing revisions
    // with changed content, so a monotone counter is exactly right here.
    class LineageSource final
        : public QindaQt::Shell::GlobalMenu::Exporter::ExportLineageSource {
    public:
        [[nodiscard]] std::optional<QindaQt::Shell::GlobalMenu::Exporter::ExportLineage>
        lineageFor(const QUuid &) const override
        {
            return QindaQt::Shell::GlobalMenu::Exporter::ExportLineage{
                m_epoch, static_cast<quint64>(++m_revision)};
        }

    private:
        QUuid m_epoch = QUuid::createUuid();
        mutable quint64 m_revision = 0;
    };

    void observeMenuTree();
    void scheduleRefresh();
    [[nodiscard]] bool announce();
    void withdraw();

    QMenuBar *m_menuBar = nullptr;
    QWindow *m_window = nullptr;
    QDBusConnection m_bus;
    QUuid m_windowUuid;
    QString m_objectPath;
    QindaQt::Shell::GlobalMenu::DbusMenu::DbusMenuServer *m_server = nullptr;
    std::unique_ptr<QindaQt::Shell::GlobalMenu::QtWidgetsAdapter::QMenuBarMenuSource> m_source;
    LineageSource m_lineage;
    std::unique_ptr<QindaQt::Shell::GlobalMenu::Exporter::MenuExporter> m_exporter;
    bool m_exported = false;
    bool m_announcedOnWayland = false;
    bool m_registeredOnX11 = false;
    quint32 m_x11WindowId = 0;
};

} // namespace QindaOffice::Shell
