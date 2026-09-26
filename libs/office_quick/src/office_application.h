// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_services.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <memory>

class QQmlEngine;

namespace QindaOffice::Core {
class JsonSettings;
class RecentFiles;
class RecoveryJournal;
class RestoreInventory;
}

namespace QindaOffice::Quick {

class DocumentHost;
class SingleInstance;

using HostFactory = std::function<DocumentHost *()>;

// GUI-thread owner of the process's ordinary document windows (ADR-0110)
// and the application's AppServices provider. One process hosts at most
// kMaxWindows windows; New always opens a new window, never a tab; the
// process exits when its last window closes. Each window is a DocumentHost
// (the app's subclass, from the factory) shown through ONE QML component —
// the app's Tk.AppWindow with a `required property DocumentHost host`.
//
// Single-instance hand-off (org.qindaqt.<App>.OpenPaths), canonical-path
// uniqueness with pristine-untitled reuse, recent files (≤20), paths-only
// restore inventory (default off), crash-recovery journal (default on,
// cleared on save/discard).
//
// AGENT-NOTE (D-055, replacing D-027's one-window-per-entry restore): the
// pending entries are offered in ONE RecoveryDialog.qml window before the
// first document window; the recovered ones open marked dirty (the journal
// entry kept until the user saves or closes with Discard), every other
// entry is discarded for good — including when the dialog is just closed.
// Restoring each entry into its own window put up to 32 windows on screen
// at every launch.
class OfficeApplication final : public QObject, public AppServices {
    Q_OBJECT
public:
    static constexpr int kMaxWindows = 32;

    explicit OfficeApplication(QObject *parent = nullptr);
    ~OfficeApplication() override;

    void setHostFactory(HostFactory factory);
    // The window component and the engine that instantiates it.
    void setWindowComponent(QQmlEngine *engine, const QUrl &url);

    [[nodiscard]] bool acquirePrimary(const QStringList &initialPaths, bool *handedOff);
    // Recovered documents (office_application_recovery.cpp, D-055): the
    // journal's entries as rows for RecoveryDialog.qml — {key, title,
    // detail, preview} — with byte-identical untitled duplicates dropped.
    [[nodiscard]] QVariantList pendingRecovery();
    // Restores each key into its own window (up to kMaxWindows).
    void recoverEntries(const QStringList &keys);
    // Dismissed: the entries never come back (RecoveryJournal::discard).
    void discardEntries(const QStringList &keys);
    // RecoveryDialog.qml's answer: recover, discard, then recoveryResolved.
    Q_INVOKABLE void resolveRecovery(const QStringList &recoverKeys,
                                     const QStringList &discardKeys);

    [[nodiscard]] DocumentHost *newWindow(const QString &path = {});
    // `requestingHost` is the window the Open came from; a pristine
    // untitled requesting window absorbs the first path (ADR-0110 reuse,
    // aimed at the window the user is looking at). Null falls back to any
    // pristine window.
    [[nodiscard]] bool openPaths(const QStringList &paths, QString *diagnostic = nullptr,
                                 DocumentHost *requestingHost = nullptr);
    [[nodiscard]] QList<DocumentHost *> hosts() const;
    [[nodiscard]] int windowCount() const;

    [[nodiscard]] bool restorePreviousDocuments() const override;
    void setRestorePreviousDocuments(bool enabled) override;
    void restoreFromInventory();
    void persistWindowInventory();

    [[nodiscard]] Core::RecoveryJournal *journal() override;
    [[nodiscard]] Core::RecentFiles *recentFiles() override;
    [[nodiscard]] bool isPathOpenInAnotherWindow(const QString &canonicalPath,
                                                 const DocumentHost *self) const override;
    [[nodiscard]] QString allocateUntitledJournalKey() override;
    [[nodiscard]] QString journalKeyForPath(const QString &canonicalPath) const override;
    void documentSaved(const QString &canonicalPath) override;

signals:
    // The recovered-documents question is answered; startup continues.
    void recoveryResolved(bool anyRecovered);
    void hostCreated(DocumentHost *host);
    void diagnosticReported(const QString &message);

private:
    void ensureStores();
    [[nodiscard]] DocumentHost *createHost();
    [[nodiscard]] bool instantiateWindow(DocumentHost *host);
    void present(DocumentHost *host);
    void wireHost(DocumentHost *host);
    void hostClosed();
    void pruneClosed();
    void updateInventoryOnClose(const QString &closedPath);
    [[nodiscard]] QString canonical(const QString &path) const;
    [[nodiscard]] QString serviceName() const;
    void activate(DocumentHost *host);

    QList<QPointer<DocumentHost>> m_hosts;
    std::unique_ptr<SingleInstance> m_single;
    std::unique_ptr<Core::RecentFiles> m_recent;
    std::unique_ptr<Core::RestoreInventory> m_inventory;
    std::unique_ptr<Core::RecoveryJournal> m_journal;
    std::unique_ptr<Core::JsonSettings> m_settings;
    HostFactory m_hostFactory;
    QQmlEngine *m_engine = nullptr;
    QUrl m_windowUrl;
    QString m_lastClosingPath;
};

} // namespace QindaOffice::Quick
