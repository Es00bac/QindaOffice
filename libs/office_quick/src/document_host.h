// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DocumentHost: everything the Widgets DocumentWindow it replaced did for
// one document window, minus the widgets (plan
// docs/plans/qindatk-conversion.md §3.2, D-031). One host = one
// Core::Document = one Tk.AppWindow (ADR-0110). The
// host owns the commands (ActionModel), the menu (MenuTree), the panel
// export, the keyboard shortcuts, the recovery journal and auto-save
// timers, the save/load/close flows and the status fields; the QML window
// (QindaOffice.Shell/OfficeWindow.qml) renders them and answers the
// dialogs the host asks for.
//
// AGENT-CONTRACT (dialogs): every question the Widgets shell asked with a
// modal QMessageBox/QFileDialog is now a SIGNAL the window answers with an
// INVOKABLE. Save, Save As and Close are therefore asynchronous: `save()`
// reports through saveFinished(bool); `requestClose()` returns whether the
// window may close now and otherwise emits closeConsentRequested().
//
// AGENT-GUARD (ADR-0117): the host never resizes or moves a shown window.

#include "action_model.h"
#include "menu_tree.h"
#include "print_service.h"

#include "document.h"

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QShortcut;
class QTimer;
namespace QindaTK { class AgentProvider; }

namespace QindaOffice::Quick {

class AppServices;
class GlobalMenuExport;

class DocumentHost : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A DocumentHost is created by the application, never by QML.")
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY titleChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY titleChanged)
    Q_PROPERTY(QString statusInfo READ statusInfo WRITE setStatusInfo NOTIFY statusInfoChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool localMenuVisible READ localMenuVisible NOTIFY localMenuVisibleChanged)
    Q_PROPERTY(bool toolBarsVisible READ toolBarsVisible NOTIFY chromeChanged)
    Q_PROPERTY(bool statusBarVisible READ statusBarVisible NOTIFY chromeChanged)
    // View ▸ Compact Ribbon (D-041); the window's RibbonFromModel follows it.
    Q_PROPERTY(bool ribbonCompact READ ribbonCompact NOTIFY chromeChanged)
    // Contextual ribbon tabs the app currently offers ("table", "shape", …).
    Q_PROPERTY(QStringList ribbonContexts READ ribbonContexts NOTIFY ribbonContextsChanged)
    Q_PROPERTY(bool closeAuthorized READ closeAuthorized NOTIFY closeAuthorizedChanged)
    // True while a worker-thread load (Document::loadStaged) is in flight;
    // the window shows busy feedback and the load's completion arrives by
    // signal, not by loadPath returning.
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QindaOffice::Quick::ActionModel *actions READ actions CONSTANT)
    Q_PROPERTY(QindaOffice::Quick::MenuTree *menuTree READ menuTree CONSTANT)
    Q_PROPERTY(QUrl preferencesPage READ preferencesPage NOTIFY preferencesPageChanged)
    Q_PROPERTY(QString appDisplayName READ appDisplayName CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(int agentAccess READ agentAccess WRITE setAgentAccess NOTIFY agentAccessChanged)
public:
    explicit DocumentHost(QObject *parent = nullptr);
    ~DocumentHost() override;

    // ---- engine ----
    // The host takes ownership. Save/Save As become enabled (D-011: a host
    // that owns a document can save).
    void setDocument(Core::Document *document);
    [[nodiscard]] Core::Document *document() const { return m_document; }
    [[nodiscard]] bool hasDocument() const { return m_document != nullptr; }
    // Borrows a provider on this thread (normally a child of the host).
    // UI grants are session-only; the transport cannot increase them.
    void setAgentProvider(QindaTK::AgentProvider *provider);
    QindaTK::AgentProvider *agentProvider() const { return m_agentProvider; }
    int agentAccess() const { return m_agentAccess; }
    void setAgentAccess(int access); // 0 off, 1 read, 2 edit; app/UI only.

    // ---- window ----
    // Called once by the application after QML created the window. The
    // host binds its shortcuts to it and starts the panel export on show.
    void attachWindow(QQuickWindow *window);
    [[nodiscard]] QQuickWindow *window() const { return m_window; }

    // ---- commands and menus ----
    [[nodiscard]] ActionModel *actions() const { return m_actions; }
    [[nodiscard]] MenuTree *menuTree() const { return m_menuTree; }
    [[nodiscard]] ActionEntry *action(const QString &id) const { return m_actions->entry(id); }
    [[nodiscard]] QString appDisplayName() const;
    [[nodiscard]] QString appVersion() const;

    // ---- printing ----
    // A job over the app's paginating renderer (printRenderCallback()); the
    // host owns it until the next call. Null when the app prints nothing.
    Q_INVOKABLE QindaOffice::Quick::PrintJob *createPrintJob();
    // File ▸ Export PDF… for apps that offer it: same renderer, no dialog.
    [[nodiscard]] bool exportPdf(const QString &path);

    // ---- services ----
    void setAppServices(AppServices *services);
    void setJournalKey(const QString &key) { m_journalKey = key; }
    [[nodiscard]] bool loadPath(const QString &path);
    [[nodiscard]] bool restoreFromJournal(const QString &path, bool untitled,
                                          const QByteArray &payload, const QString &journalKey);
    [[nodiscard]] bool isPristineUntitled() const;

    // ---- status ----
    // Virtual for hosts without a Core::Document (QindaMail's MailHost names
    // the view it shows instead of "No document").
    [[nodiscard]] virtual QString title() const;
    [[nodiscard]] QString windowTitle() const;
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] QString statusInfo() const { return m_statusInfo; }
    void setStatusInfo(const QString &text);
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    Q_INVOKABLE void showStatusMessage(const QString &text, int timeoutMs = 5000);
    [[nodiscard]] bool localMenuVisible() const { return m_localMenuVisible; }
    [[nodiscard]] bool toolBarsVisible() const;
    [[nodiscard]] bool statusBarVisible() const;
    [[nodiscard]] bool ribbonCompact() const;
    [[nodiscard]] QStringList ribbonContexts() const { return m_ribbonContexts; }
    // Apps call this when the selection changes what the ribbon offers.
    void setRibbonContexts(const QStringList &contexts);
    [[nodiscard]] bool closeAuthorized() const { return m_closeAuthorized; }
    [[nodiscard]] bool isLoading() const { return m_loading; }
    [[nodiscard]] QUrl preferencesPage() const { return m_preferencesPage; }
    void setPreferencesPage(const QUrl &url);

    // ---- flows (QML side) ----
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs();
    // true: the window may close now (and `closing` was emitted). false: a
    // consent dialog was requested, or a save is in flight; the window
    // must refuse the close event and wait for closeAuthorized.
    Q_INVOKABLE bool requestClose();
    Q_INVOKABLE void resolveClose(const QString &choice); // "save" | "discard" | "cancel"
    Q_INVOKABLE void openPathsChosen(const QList<QUrl> &urls);
    Q_INVOKABLE void saveAsPathChosen(const QUrl &url, const QString &selectedFilter);
    Q_INVOKABLE void saveAsCancelled();
    Q_INVOKABLE void resolveOverwrite(bool overwrite);
    Q_INVOKABLE void setRestorePreviousDocuments(bool enabled);
    Q_INVOKABLE bool restorePreviousDocumentsAvailable() const { return m_services != nullptr; }
    Q_INVOKABLE bool restorePreviousDocuments() const;

    // Filters as QtQuick.Dialogs wants them ("Text (*.txt)", …).
    [[nodiscard]] Q_INVOKABLE QStringList openNameFilters() const;
    [[nodiscard]] Q_INVOKABLE QStringList saveNameFilters() const;

protected:
    // App hooks (the DocumentWindow virtuals).
    [[nodiscard]] virtual QString filterString() const;
    [[nodiscard]] virtual QString saveFilterString() const { return filterString(); }
    virtual void applyPreferences() {}
    // The document's CONTENT was replaced wholesale (a path loaded, a
    // journal restored): rebuild whatever the app derives from it. Not
    // called for edits — contentChanged is, and it also journals.
    virtual void documentReplaced() {}
    // The paginating renderer Print, Print Preview and Export PDF share
    // (plan 5.2: one callback, so paper and PDF cannot drift). Empty means
    // the app does not print; the commands stay disabled.
    [[nodiscard]] virtual PrintRenderCallback printRenderCallback() const { return {}; }
    // The orientation a new print job (and Export PDF) starts in (M51: a
    // Gantt chart prints landscape).
    [[nodiscard]] virtual bool printPrefersLandscape() const { return false; }
    // AGENT-CONTRACT (OfficeApplication::openPaths, D-042): a host WITHOUT a
    // document (MailHost) receives the paths and URLs the app is launched
    // or handed with here (mailto:, webcal:, .ics, .eml) instead of
    // loadPath's document load; true when it took the path.
    [[nodiscard]] virtual bool openWithoutDocument(const QString &path)
    {
        Q_UNUSED(path)
        return false;
    }
    // Whether such a host may absorb paths like a pristine untitled window
    // (so a hand-off reaches the open window instead of opening another).
    [[nodiscard]] virtual bool reusableWithoutDocument() const { return false; }
    // Runs applyPreferences() once the subclass exists and enables the
    // print commands when printRenderCallback() is non-empty; subclass
    // ctors call it last (the base ctor cannot reach the overrides).
    void finalizeHost();

signals:
    void agentAccessChanged();
    void newWindowRequested();
    void openPathsRequested(const QStringList &paths);
    // Emitted from an ACCEPTED close, while everything is still alive.
    void closing(DocumentHost *host);
    void closeConsentRequested(const QString &title);
    void openDialogRequested(const QString &startDir, const QStringList &nameFilters);
    void saveAsDialogRequested(const QString &startPath, const QStringList &nameFilters);
    void overwriteConsentRequested(const QString &path);
    // variant: "info" | "warning" | "danger"
    void messageRequested(const QString &title, const QString &text, const QString &variant);
    void aboutRequested();
    void preferencesRequested();
    void printRequested();
    void printPreviewRequested();
    // Help ▸ Search Commands… (Alt+Q): the window opens its command palette.
    void commandSearchRequested();
    void ribbonContextsChanged();
    void saveFinished(bool ok);
    void documentChanged();
    void titleChanged();
    void statusInfoChanged();
    void statusMessageChanged();
    void localMenuVisibleChanged();
    void chromeChanged();
    void closeAuthorizedChanged();
    void loadingChanged();
    void preferencesPageChanged();

private:
    enum class Pending { None, Save, SaveAs };
    enum class AfterSave { Nothing, Close };

    void wireStandardCommands();
    void bindShortcuts();
    void bindShortcut(ActionEntry *entry);
    void startMenuExport();
    void applyMenuBarVisibility(bool exported);
    void syncTitle();
    void scheduleJournalSnapshot();
    void clearJournalEntry();
    void commitSuccessfulSave();
    void rebuildRecentMenu();
    void restartAutoSaveTimer();
    void autoSaveTick();
    void applyChromePreferences();
    // The save that runs once a writable target is settled.
    void performSave(const QString &path);
    void finishSave(bool ok);
    // GUI-thread completion of a worker-thread load started by loadPath().
    void finishAsyncLoad(const QString &path, bool ok);
    // Optimistic concurrency: true when the write may proceed now, false
    // when the window was asked (overwriteConsentRequested) first.
    [[nodiscard]] bool ensureWritableTarget(const QString &path);
    void authorizeClose();

    Core::Document *m_document = nullptr;
    QindaTK::AgentProvider *m_agentProvider = nullptr;
    int m_agentAccess = 0;
    ActionModel *m_actions;
    MenuTree *m_menuTree;
    QPointer<QQuickWindow> m_window;
    GlobalMenuExport *m_menuExport = nullptr;
    AppServices *m_services = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
    PrintJob *m_printJob = nullptr;
    QList<QShortcut *> m_shortcuts;
    QString m_journalKey;
    QString m_statusInfo;
    QStringList m_ribbonContexts;
    QString m_statusMessage;
    // Set by the worker half of an async load, read at finishAsyncLoad —
    // the QFutureWatcher completion is the synchronization edge between them.
    QString m_asyncError;
    QUrl m_preferencesPage;
    QString m_pendingPath;
    Pending m_pending = Pending::None;
    AfterSave m_afterSave = AfterSave::Nothing;
    bool m_exported = false;
    bool m_localMenuVisible = true;
    bool m_closeAuthorized = false;
    bool m_loading = false;
    bool m_applyingChromePrefs = false;
    bool m_finalized = false;
};

} // namespace QindaOffice::Quick
