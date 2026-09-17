// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "standard_actions.h"

#include <QMainWindow>
#include <QPointer>

class QLabel;
class QMenu;
class QTextEdit;
class QTimer;
class QToolBar;

namespace QindaOffice::Core {
class Document;
}

namespace QindaOffice::Shell {

class AppServices;
class GlobalMenuExport;
class PreferencesDialog;
class WrappingToolBar;

// One document = one ordinary top-level window (ADR-0110): no tab strip, no
// MDI, no document switcher. DocumentWindow is the suite's shared frame -
// standard menus/actions, per-window global-menu export, status bar - plus
// the M1 stub document host (a QTextEdit over a PlainTextDocument) that the
// four apps replace with their real engines per milestone.
//
// AGENT-GUARD (ADR-0117): never resize()/move()/setFixedSize() a shown main
// window; the desktop's window containers own grouped geometry. Only the
// initial pre-show size may be set here.
class DocumentWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DocumentWindow(QWidget *parent = nullptr);
    ~DocumentWindow() override;

    [[nodiscard]] QAction *action(const QString &objectName) const;

    // The permanent right-hand status-bar field. Every app fills it with
    // what its document is doing right now — word and character counts,
    // the aggregates of the selected cells, "Slide 3 of 12", the current
    // page. It is a PERMANENT widget, so a transient showMessage() on the
    // left never hides it, and the status bar exists from construction so
    // the first message cannot make the window's contents jump.
    void setStatusInfo(const QString &text);
    [[nodiscard]] QString statusInfo() const;

    // Insertion point for app entries in the File menu ("New from
    // Template…"). AGENT-GUARD: app entries must be inserted BEFORE this
    // separator. Appending to the menu instead put them under Quit, which
    // is where all three apps that use the hook had ended up.
    [[nodiscard]] QAction *fileMenuAnchor() const { return m_fileMenuAnchor; }
    void insertFileMenuActionsDeferred();
    [[nodiscard]] StandardActions *standardActions() { return &m_actions; }

    // Runs the two app-menu hooks (insertFileMenuActions + the real
    // insertApplicationMenus against the AI anchor). The base ctor can only
    // dispatch the no-op base versions (the subclass does not exist yet),
    // so every subclass ctor must call this once its own state exists;
    // showEvent also calls it as a safety net. Idempotent.
    void finalizeAppMenuHooks();

    // The suite-standard icon strip (New/Open/Save, Undo/Redo/Cut/Copy/
    // Paste, Find, Zoom). Idempotent: the first call creates it, later
    // calls return the same toolbar so apps can append their own actions
    // with addStripAction()/addStripSeparator().
    // Buttons mirror their action's real enabled state - an app that has
    // not wired a command shows it disabled, never hidden. The strip WRAPS
    // rather than pushing overflow behind an extension chevron, so a
    // command is never invisible just because the window is narrow.
    [[nodiscard]] WrappingToolBar *addMainToolBar();

    // The window takes ownership. The M1 stub hosts PlainTextDocument;
    // other document types show a disabled editor placeholder until their
    // app milestones land (fail honestly).
    void setDocument(Core::Document *document);
    // App-specific hosts (QindaCalc's grid, QindaShow's canvas) replace the
    // stub editor wholesale; title/dirty/journal wiring is kept.
    void setDocumentHost(QWidget *widget, Core::Document *document);
    [[nodiscard]] Core::Document *document() const { return m_document; }
    [[nodiscard]] bool hasDocument() const { return m_document != nullptr; }

    // Wired by OfficeApplication after construction; may stay null (tests,
    // degraded runs) - journaling/recents/path-uniqueness degrade visibly.
    void setAppServices(AppServices *services);
    void setJournalKey(const QString &key) { m_journalKey = key; }

    // Loads a path into this window's document. On failure the user saw a
    // message; the caller decides whether to close the window.
    [[nodiscard]] bool loadPath(const QString &path);
    // Restores a recovery-journal entry; keeps the journal entry (the
    // restored document is unsaved until the user saves or discards).
    [[nodiscard]] bool restoreFromJournal(const QString &path, bool untitled,
                                          const QByteArray &payload, const QString &journalKey);

    // Settings ▸ Preferences…: builds the shell pages, lets the subclass
    // add its own through addPreferencePages(), and re-applies on OK.
    void showPreferences();

    // Save paths shared by actions and close consent. false means "not
    // saved" (cancelled or failed) - callers must not treat it as success.
    [[nodiscard]] bool saveDocument();
    [[nodiscard]] bool saveDocumentAs();
    [[nodiscard]] bool isPristineUntitled() const;

signals:
    // Raised by file.new; OfficeApplication turns this into a new ordinary
    // window (never a tab of this one).
    void newWindowRequested();
    // Raised by Open/drops/recent-menu; OfficeApplication applies canonical
    // uniqueness, pristine reuse and window creation.
    void openPathsRequested(const QStringList &paths);
    // Emitted from an ACCEPTED closeEvent, while the object is still fully
    // alive. AGENT-GUARD: path bookkeeping must connect here, not to
    // destroyed() — after ~DocumentWindow began, qobject_cast<DocumentWindow*>
    // on the destroyed QObject is null and document() is gone. Consumers:
    // OfficeApplication (final-close path preservation, inventory update).
    void closing(DocumentWindow *window);

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    // App hook: subclasses insert their app-specific menus (Format/Insert/
    // Table/Data/Slide/Page) before the AI menu. Default inserts nothing.
    virtual void insertApplicationMenus(QMenu *before);
    // Subclass hook: append entries to the shell's File menu (New from
    // Template…). Default inserts nothing; reach the menu through
    // insertFileMenuActionsDeferred() in the subclass ctor (the shell
    // creates menus before the subclass exists, same two-phase pattern as
    // insertApplicationMenus + the named aiMenu anchor). Insert before
    // fileMenuAnchor(), never append.
    virtual void insertFileMenuActions(QMenu *fileMenu);

    // Subclass hook: add app pages to the Preferences dialog (spell check,
    // grid options, AI defaults). Default adds none.
    virtual void addPreferencePages(PreferencesDialog *dialog);
    // Subclass hook: re-read Preferences into the live view (document font,
    // default zoom). Called once the subclass exists (finalizeAppMenuHooks)
    // and again whenever the store changes — including from another window
    // of the same app. Default does nothing.
    virtual void applyPreferences();

protected:
    StandardActions m_actions;

private:
    void createActions();
    void createMenus();
    // Status bar, View chrome toggles, Preferences and the auto-save timer
    // (document_window_chrome.cpp).
    void createChrome();
    void applyChromePreferences();
    void restartAutoSaveTimer();
    void autoSaveTick();
    void startMenuExport();
    void applyMenuBarVisibility(bool exported);

    void syncEditorFromDocument();
    void syncTitle();
    void scheduleJournalSnapshot();
    void clearJournalEntry();
    void commitSuccessfulSave();
    void rebuildRecentMenu();
    [[nodiscard]] virtual QString filterString() const;
    // Save-As filters default to the open filter; apps with a default
    // save format (QindaWrite: ODT) override.
    [[nodiscard]] virtual QString saveFilterString() const { return filterString(); }
    [[nodiscard]] bool ensureWritableTarget(const QString &path);

    GlobalMenuExport *m_menuExport = nullptr;
    WrappingToolBar *m_mainToolBar = nullptr;
    bool m_appMenusFinalized = false;
    bool m_menuExportStarted = false;
    AppServices *m_services = nullptr;
    Core::Document *m_document = nullptr;
    QTextEdit *m_editor = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_restoreAction = nullptr;
    QAction *m_fileMenuAnchor = nullptr;
    QLabel *m_statusInfo = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    bool m_applyingChromePrefs = false;
    QString m_journalKey;
    bool m_loading = false;
};

} // namespace QindaOffice::Shell
