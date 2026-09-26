// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-window global menu export for the QindaQt desktop panel.
//
// AGENT-CONTRACT (ported verbatim from QindaQt_Apps/src/dbus/global_menu_export,
// mirrors the first-party composition documented under docs/wiki shell/global-menu):
// the export is OPT-IN and FAIL-CLOSED. start() returns false whenever the
// endpoint cannot be published and announced; the caller must then keep the
// local menu bar visible. The panel binds the menu only for the focused
// window and authenticates the endpoint by bus-owner PID, which trivially
// matches because DbusMenuServer lives in this process.
//
// Composition: ActionModelMenuSource (snapshot of the window's MenuTree +
// ActionModel) -> MenuExporter (validates + stamps epoch/revision lineage)
// -> DbusMenuServer (standard com.canonical.dbusmenu v4 endpoint) ->
// per-window Wayland announcement through Qt's private
// QDesktopUnixServices (no X11 id, no XWayland), or the canonical
// registrar under xcb.
//
// AGENT-GUARD: publication is HELD while the window is not active. The
// panel dismisses an open menu whenever the app publishes a changed tree
// (container-wm fc6b1468: a gesture must never reach a retired item), and
// opening that menu moves keyboard focus to the panel — which is exactly
// when the suite changes state (Wayland re-announces the clipboard, so
// Paste flips; an inline editor loses focus and commits, so Undo flips).
// Publishing then closed every Office menu the instant it opened. Changes
// made while inactive are published once, when the window is active again.
//
// AGENT-GUARD: an activation resolves by id IN THE ACTION MODEL. The
// Widgets-era export resolved it with menuBar()->findChild<QAction*>(), and
// because the suite parented its actions to the window, 71 of 72 panel
// commands did nothing when clicked (plan V5). The model is the single
// authority for every route: local menu, tool strip, keyboard, panel.

#include <qindaqt/shell/global_menu/exporter/menu_exporter.h>

#include <QDBusConnection>
#include <QObject>
#include <QTimer>
#include <QUuid>

class QWindow;

namespace QindaQt::Shell::GlobalMenu::DbusMenu {
class DbusMenuServer;
}

namespace QindaOffice::Quick {

class ActionModel;
class ActionModelMenuSource;
class MenuTree;

class GlobalMenuExport final : public QObject {
    Q_OBJECT
public:
    // tree, actions and window must outlive this object (MenuSource
    // lifetime contract); the DocumentHost owns all of them.
    GlobalMenuExport(MenuTree *tree, ActionModel *actions, QWindow *window,
                     QObject *parent = nullptr);
    ~GlobalMenuExport() override;

    // Builds and registers the dbusmenu endpoint, then announces it for the
    // window. Returns true only when the announcement succeeded; the local
    // menu bar stays visible otherwise.
    [[nodiscard]] bool start();

    // Withdraws the announcement and unregisters the endpoint.
    void stop();

    [[nodiscard]] bool isExported() const { return m_exported; }

    // Re-snapshots and republishes when the content changed. Runs once per
    // event-loop pass after any tree or entry change; also callable.
    void refresh();
    // The stable object path of this window's endpoint (tests, diagnostics).
    [[nodiscard]] QString objectPath() const { return m_objectPath; }
    // Test seams: coalesced refresh passes that ran, and whether a change is
    // waiting for the window to become active again.
    [[nodiscard]] int refreshPasses() const { return m_refreshPasses; }
    [[nodiscard]] bool isHoldingChanges() const { return m_heldWhileInactive; }

    // How long changes coalesce before a publish: long enough that the
    // window's focus-out (a queued window-system event) has landed when a
    // focus-driven change is checked against isActive().
    static constexpr int kCoalesceMs = 120;

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

    void scheduleRefresh();
    void coalescedRefresh();
    void windowActiveChanged();
    [[nodiscard]] bool announce();
    void withdraw();

    MenuTree *m_tree = nullptr;
    ActionModel *m_actions = nullptr;
    QWindow *m_window = nullptr;
    QDBusConnection m_bus;
    QUuid m_windowUuid;
    QString m_objectPath;
    QindaQt::Shell::GlobalMenu::DbusMenu::DbusMenuServer *m_server = nullptr;
    std::unique_ptr<ActionModelMenuSource> m_source;
    QTimer *m_refreshTimer = nullptr;
    bool m_heldWhileInactive = false;
    int m_refreshPasses = 0;
    LineageSource m_lineage;
    std::unique_ptr<QindaQt::Shell::GlobalMenu::Exporter::MenuExporter> m_exporter;
    bool m_exported = false;
    bool m_announcedOnWayland = false;
    bool m_registeredOnX11 = false;
    quint32 m_x11WindowId = 0;
};

} // namespace QindaOffice::Quick
