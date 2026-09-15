// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "standard_actions.h"

#include <QMainWindow>
#include <QPointer>

class QMenu;
class QTextEdit;

namespace QindaOffice::Core {
class Document;
}

namespace QindaOffice::Shell {

class AppServices;
class GlobalMenuExport;

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
    [[nodiscard]] StandardActions *standardActions() { return &m_actions; }

    // The window takes ownership. The M1 stub hosts PlainTextDocument;
    // other document types show a disabled editor placeholder until their
    // app milestones land (fail honestly).
    void setDocument(Core::Document *document);
    [[nodiscard]] Core::Document *document() const { return m_document; }

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

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    // App hook: subclasses insert their app-specific menus (Format/Insert/
    // Table/Data/Slide/Page) before the AI menu. Default inserts nothing.
    virtual void insertApplicationMenus(QMenu *before);

private:
    void createActions();
    void createMenus();
    void startMenuExport();
    void applyMenuBarVisibility(bool exported);

    void syncEditorFromDocument();
    void syncTitle();
    void scheduleJournalSnapshot();
    void clearJournalEntry();
    void commitSuccessfulSave();
    void rebuildRecentMenu();
    [[nodiscard]] QString filterString() const;
    [[nodiscard]] bool ensureWritableTarget(const QString &path);

    StandardActions m_actions;
    GlobalMenuExport *m_menuExport = nullptr;
    bool m_menuExportStarted = false;
    AppServices *m_services = nullptr;
    Core::Document *m_document = nullptr;
    QTextEdit *m_editor = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_restoreAction = nullptr;
    QString m_journalKey;
    bool m_loading = false;
};

} // namespace QindaOffice::Shell
