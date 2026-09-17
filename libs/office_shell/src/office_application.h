// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_services.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <memory>

namespace QindaOffice::Core {
class JsonSettings;
class RecentFiles;
class RecoveryJournal;
class RestoreInventory;
}

namespace QindaOffice::Shell {

class DocumentWindow;
class SingleInstance;

using WindowFactory = std::function<DocumentWindow *()>;

// GUI-thread owner of the process's ordinary document windows (mirrors
// container-wm/src/apps/text_editor/ui/editor_application.h per ADR-0110)
// and the application's AppServices provider. One process hosts at most
// kMaxWindows windows; New always opens a new window, never a tab; the
// process exits when its last window closes.
//
// M1 scope: single-instance hand-off (org.qindaqt.<App>.OpenPaths),
// canonical-path uniqueness with pristine-untitled reuse, recent files
// (≤20), paths-only restore inventory (default off), crash-recovery journal
// (default on, cleared on save/discard) with startup Restore/Discard offers.
class OfficeApplication final : public QObject, public AppServices {
    Q_OBJECT
public:
    static constexpr int kMaxWindows = 32;

    explicit OfficeApplication(QObject *parent = nullptr);
    ~OfficeApplication() override;

    // Lets an app (QindaCalc, QindaShow...) create its own window subclass;
    // the default builds a plain DocumentWindow.
    void setWindowFactory(WindowFactory factory);

    // Tries to become the D-Bus primary (call before creating windows).
    // Returns false when this process should hand off and exit
    // (*handedOff == true) or run standalone degraded (*handedOff == false,
    // e.g. no session bus); true when this process owns the name (publish()
    // done, openPathsRequested wired to openPaths()).
    [[nodiscard]] bool acquirePrimary(const QStringList &initialPaths, bool *handedOff);

    // Journal sweep: for every pending entry asks Restore/Discard and opens
    // windows for restored documents. Call before CLI-path handling.
    void offerRecovery();

    [[nodiscard]] DocumentWindow *newWindow(const QString &path = {});
    // `requestingWindow` is the window the Open came from, when there is
    // one (File ▸ Open, a drop, a recent-menu pick). AGENT-CONTRACT: a
    // pristine untitled REQUESTING window absorbs the first path, so
    // opening from a blank document replaces that blank document instead
    // of leaving it behind — ADR-0110's pristine reuse, aimed at the window
    // the user is actually looking at rather than whichever blank window
    // happens to be first in the list. Null (D-Bus hand-off, CLI paths,
    // restore) falls back to any pristine window.
    [[nodiscard]] bool openPaths(const QStringList &paths, QString *diagnostic = nullptr,
                                 DocumentWindow *requestingWindow = nullptr);
    [[nodiscard]] QList<DocumentWindow *> windows() const;
    [[nodiscard]] int windowCount() const;

    // Restore-inventory policy (persisted; default off).
    [[nodiscard]] bool restorePreviousDocuments() const override;
    void setRestorePreviousDocuments(bool enabled) override;
    // Opens the inventory's paths (used at startup when enabled, no CLI
    // paths and no recovery-restored windows).
    void restoreFromInventory();

    // Rewrites the restore inventory from the current windows (ADR-0110:
    // a nonfinal close drops its path; the final close preserves the set).
    void persistWindowInventory();

    // AppServices implementation.
    [[nodiscard]] Core::RecoveryJournal *journal() override;
    [[nodiscard]] Core::RecentFiles *recentFiles() override;
    [[nodiscard]] bool isPathOpenInAnotherWindow(const QString &canonicalPath,
                                                 const DocumentWindow *self) const override;
    [[nodiscard]] QString allocateUntitledJournalKey() override;
    [[nodiscard]] QString journalKeyForPath(const QString &canonicalPath) const override;
    void documentSaved(const QString &canonicalPath) override;

signals:
    void windowCreated(DocumentWindow *window);
    void diagnosticReported(const QString &message);

private:
    // Lazily builds the persisted stores (config/state dirs) so a bare
    // OfficeApplication (tests, tool reuse) never touches user directories.
    void ensureStores();
    DocumentWindow *createWindow();
    void present(DocumentWindow *window);
    void wireWindow(DocumentWindow *window);
    void windowClosed(QObject *object);
    void pruneClosed();
    void updateInventoryOnClose(const QString &closedPath);
    [[nodiscard]] QString canonical(const QString &path) const;
    [[nodiscard]] QString serviceName() const;
    [[nodiscard]] QString stateDir() const;

    QList<QPointer<DocumentWindow>> m_windows;
    std::unique_ptr<SingleInstance> m_single;
    std::unique_ptr<Core::RecentFiles> m_recent;
    std::unique_ptr<Core::RestoreInventory> m_inventory;
    std::unique_ptr<Core::RecoveryJournal> m_journal;
    std::unique_ptr<Core::JsonSettings> m_settings;
    WindowFactory m_windowFactory;
    // Path of the window currently going away, captured from closing()
    // (destroyed() can no longer provide it — see wireWindow).
    QString m_lastClosingPath;
};

} // namespace QindaOffice::Shell
