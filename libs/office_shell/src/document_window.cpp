// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"

#include "app_services.h"
#include "global_menu_export.h"

#include "atomic_file_store.h"
#include "document.h"
#include "plain_text_document.h"
#include "recent_files.h"
#include "recovery_journal.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QTextEdit>

namespace QindaOffice::Shell {

DocumentWindow::DocumentWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createActions();
    createMenus();

    m_editor = new QTextEdit(this);
    setCentralWidget(m_editor);

    setAcceptDrops(true);
    resize(960, 640);
    syncTitle();
}

DocumentWindow::~DocumentWindow()
{
    // The export withdraws its announcement in its own destructor; it is
    // parented to the window so it dies before the menu bar it observes.
    delete m_menuExport;
}

QAction *DocumentWindow::action(const QString &objectName) const
{
    return findChild<QAction *>(objectName);
}

void DocumentWindow::createActions()
{
    m_actions = createStandardActions(this);

    connect(m_actions.fileNew, &QAction::triggered, this, &DocumentWindow::newWindowRequested);
    connect(m_actions.fileOpen, &QAction::triggered, this, [this] {
        const QStringList paths =
            QFileDialog::getOpenFileNames(this, tr("Open"), QString(), filterString());
        if (!paths.isEmpty()) {
            emit openPathsRequested(paths);
        }
    });
    connect(m_actions.fileCloseWindow, &QAction::triggered, this, &QWidget::close);
    connect(m_actions.fileQuit, &QAction::triggered, this, &QWidget::close);
    connect(m_actions.fileSave, &QAction::triggered, this, [this] { saveDocument(); });
    connect(m_actions.fileSaveAs, &QAction::triggered, this, [this] { saveDocumentAs(); });

    connect(m_actions.viewShowMenuBar, &QAction::toggled, this, [this](bool show) {
        // The exported menu lives in the panel; the local bar is a fallback
        // and a user preference. Toggling never touches the export itself.
        menuBar()->setVisible(show);
    });

    connect(m_actions.helpAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("About %1").arg(QApplication::applicationDisplayName()),
            tr("<b>%1</b> — part of QindaQt Office %2.<br/>One document per "
               "window on the QindaQt desktop.")
                .arg(QApplication::applicationDisplayName(), QStringLiteral(QQO_VERSION)));
    });
    connect(m_actions.helpAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void DocumentWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_actions.fileNew);
    fileMenu->addAction(m_actions.fileOpen);
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions.fileCloseWindow);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions.fileSave);
    fileMenu->addAction(m_actions.fileSaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions.filePrint);
    fileMenu->addAction(m_actions.filePrintPreview);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions.fileQuit);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_actions.editUndo);
    editMenu->addAction(m_actions.editRedo);
    editMenu->addSeparator();
    editMenu->addAction(m_actions.editCut);
    editMenu->addAction(m_actions.editCopy);
    editMenu->addAction(m_actions.editPaste);
    editMenu->addSeparator();
    editMenu->addAction(m_actions.editSelectAll);
    editMenu->addSeparator();
    editMenu->addAction(m_actions.editFind);
    editMenu->addAction(m_actions.editReplace);
    editMenu->addAction(m_actions.editFindNext);
    editMenu->addAction(m_actions.editFindPrevious);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_actions.viewZoomIn);
    viewMenu->addAction(m_actions.viewZoomOut);
    viewMenu->addAction(m_actions.viewZoomReset);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actions.viewShowMenuBar);

    // App-specific menus (Format/Insert/Table/Data/Slide/Page) slot in
    // between View and AI per the shell spec: subclasses insert before the
    // menu handed to insertApplicationMenus() via menuBar()->insertMenu(
    // before->menuAction(), ...). The AI title action already anchors the
    // position even while the menu is still empty.
    auto *aiMenu = new QMenu(tr("&AI"), this);
    menuBar()->addMenu(aiMenu);
    insertApplicationMenus(aiMenu);
    aiMenu->addAction(m_actions.aiAssistant);

    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    m_restoreAction = settingsMenu->addAction(tr("Restore &Previous Documents"));
    m_restoreAction->setCheckable(true);
    m_restoreAction->setEnabled(false); // enabled by setAppServices
    settingsMenu->addSeparator();
    settingsMenu->addAction(m_actions.settingsPreferences);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_actions.helpAbout);
    helpMenu->addAction(m_actions.helpAboutQt);
}

void DocumentWindow::insertApplicationMenus(QMenu *before)
{
    Q_UNUSED(before);
}

void DocumentWindow::setDocument(Core::Document *document)
{
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
        delete m_document;
    }
    m_document = document;
    if (m_document == nullptr) {
        return;
    }
    m_document->setParent(this);

    connect(m_document, &Core::Document::dirtyChanged, this, [this] { syncTitle(); });
    connect(m_document, &Core::Document::contentChanged, this,
            [this] { scheduleJournalSnapshot(); });

    // The M1 stub host understands PlainTextDocument; other engines arrive
    // with their app milestones and replace this wiring wholesale.
    auto *plain = qobject_cast<Core::PlainTextDocument *>(m_document);
    m_editor->setEnabled(plain != nullptr);
    if (plain != nullptr) {
        connect(m_editor, &QTextEdit::textChanged, this, [this, plain] {
            if (m_loading) {
                return;
            }
            plain->setText(m_editor->toPlainText());
        });
        connect(m_actions.editUndo, &QAction::triggered, m_editor, &QTextEdit::undo);
        connect(m_actions.editRedo, &QAction::triggered, m_editor, &QTextEdit::redo);
        connect(m_actions.editCut, &QAction::triggered, m_editor, &QTextEdit::cut);
        connect(m_actions.editCopy, &QAction::triggered, m_editor, &QTextEdit::copy);
        connect(m_actions.editPaste, &QAction::triggered, m_editor, &QTextEdit::paste);
        connect(m_actions.editSelectAll, &QAction::triggered, m_editor,
                &QTextEdit::selectAll);
        const QList<QAction *> editable = {m_actions.fileSave,  m_actions.fileSaveAs,
                                           m_actions.editUndo,  m_actions.editRedo,
                                           m_actions.editCut,   m_actions.editCopy,
                                           m_actions.editPaste, m_actions.editSelectAll};
        for (QAction *action : editable) {
            action->setEnabled(true);
        }
    } else {
        m_editor->setPlaceholderText(
            tr("This document type has no host in the M1 shell stub yet."));
    }
    syncEditorFromDocument();
    syncTitle();
}

void DocumentWindow::setAppServices(AppServices *services)
{
    m_services = services;
    const bool wired = m_services != nullptr;
    m_restoreAction->setEnabled(wired);
    if (wired) {
        m_restoreAction->setChecked(m_services->restorePreviousDocuments());
        // setAppServices runs once per window (OfficeApplication::wireWindow),
        // so no duplicate-connect guard is needed here.
        connect(m_restoreAction, &QAction::toggled, this, [this](bool checked) {
            m_services->setRestorePreviousDocuments(checked);
        });
        if (Core::RecentFiles *recent = m_services->recentFiles();
            recent != nullptr && m_recentMenu != nullptr) {
            connect(recent, &Core::RecentFiles::changed, this,
                    [this] { rebuildRecentMenu(); });
        }
    }
    rebuildRecentMenu();
}

bool DocumentWindow::isPristineUntitled() const
{
    return m_document != nullptr && m_document->canonicalPath().isEmpty()
        && !m_document->isDirty();
}

bool DocumentWindow::loadPath(const QString &path)
{
    if (m_document == nullptr) {
        return false;
    }
    QString error;
    if (!m_document->load(path, &error)) {
        QMessageBox::warning(this, tr("Open failed"),
                             tr("Could not open %1:\n%2").arg(path, error));
        return false;
    }
    syncEditorFromDocument();
    syncTitle(); // the path may have changed even though dirty stayed false
    if (m_services != nullptr) {
        m_services->documentSaved(m_document->canonicalPath());
    }
    statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    return true;
}

bool DocumentWindow::restoreFromJournal(const QString &path, bool untitled,
                                        const QByteArray &payload, const QString &journalKey)
{
    Q_UNUSED(untitled);
    if (m_document == nullptr) {
        return false;
    }
    QString error;
    if (!m_document->loadRecovery(payload, &error)) {
        QMessageBox::warning(this, tr("Recovery failed"),
                             tr("Could not restore the document:\n%1").arg(error));
        return false;
    }
    if (!path.isEmpty()) {
        // Present the on-disk path in the title; the byte revision is
        // refreshed so a later save notices external changes.
        m_document->adoptPath(path);
    }
    m_journalKey = journalKey;
    syncEditorFromDocument();
    syncTitle();
    return true;
}

bool DocumentWindow::saveDocument()
{
    if (m_document == nullptr) {
        return true; // nothing to save is not a failure
    }
    const QString path = m_document->canonicalPath();
    if (path.isEmpty()) {
        return saveDocumentAs();
    }
    if (!ensureWritableTarget(path)) {
        return false;
    }
    QString error;
    if (!m_document->save(path, &error)) {
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not save %1:\n%2").arg(path, error));
        return false;
    }
    commitSuccessfulSave();
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    return true;
}

bool DocumentWindow::saveDocumentAs()
{
    if (m_document == nullptr) {
        return false;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save As"), QString(), filterString());
    if (path.isEmpty()) {
        return false; // cancelled
    }
    if (m_services != nullptr
        && m_services->isPathOpenInAnotherWindow(path, this)) {
        QMessageBox::warning(this, tr("Document already open"),
                             tr("%1 is already open in another window.").arg(path));
        return false;
    }
    if (!ensureWritableTarget(path)) {
        return false;
    }
    QString error;
    if (!m_document->save(path, &error)) {
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not save %1:\n%2").arg(path, error));
        return false;
    }
    commitSuccessfulSave();
    statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
    return true;
}

void DocumentWindow::commitSuccessfulSave()
{
    // Cleared on save (plan section 2.7); migrate the key first in case the
    // save renamed the document (Save As).
    const QString newKey = m_services != nullptr
        ? m_services->journalKeyForPath(m_document->canonicalPath())
        : Core::RecoveryJournal::keyForPath(m_document->canonicalPath());
    if (Core::RecoveryJournal *journal = m_services ? m_services->journal() : nullptr;
        journal != nullptr) {
        if (!m_journalKey.isEmpty() && m_journalKey != newKey) {
            journal->clear(m_journalKey);
        }
        journal->clear(newKey);
    }
    m_journalKey = newKey;
    if (m_services != nullptr) {
        m_services->documentSaved(m_document->canonicalPath());
    }
    syncTitle();
}

bool DocumentWindow::ensureWritableTarget(const QString &path)
{
    // Optimistic concurrency (plan section 5.1): refuse to clobber a file
    // changed on disk since load without explicit consent. Revision 0 on
    // either side means "no earlier on-disk state" and always passes.
    const quint64 onDisk = Core::fileRevision(path);
    if (onDisk == 0 || m_document->byteRevision() == 0 || onDisk == m_document->byteRevision()) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this, tr("File changed on disk"),
        tr("%1 was modified by another program since it was opened.\n"
           "Saving will overwrite those changes.")
            .arg(path),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
    return choice == QMessageBox::Save;
}

void DocumentWindow::scheduleJournalSnapshot()
{
    if (m_document == nullptr || m_loading) {
        return;
    }
    Core::RecoveryJournal *journal = m_services != nullptr ? m_services->journal() : nullptr;
    if (journal == nullptr) {
        return;
    }
    if (m_journalKey.isEmpty()) {
        m_journalKey = m_services->allocateUntitledJournalKey();
    }
    const QPointer<Core::Document> doc = m_document;
    journal->schedule(m_journalKey, m_document->canonicalPath(),
                      m_document->canonicalPath().isEmpty(),
                      [doc] { return doc ? doc->recoverySnapshot() : QByteArray(); });
}

void DocumentWindow::clearJournalEntry()
{
    if (m_journalKey.isEmpty()) {
        return;
    }
    if (Core::RecoveryJournal *journal = m_services ? m_services->journal() : nullptr;
        journal != nullptr) {
        journal->clear(m_journalKey);
    }
}

void DocumentWindow::syncEditorFromDocument()
{
    auto *plain = qobject_cast<Core::PlainTextDocument *>(m_document);
    if (plain == nullptr) {
        return;
    }
    m_loading = true;
    m_editor->setPlainText(plain->text());
    m_loading = false;
}

void DocumentWindow::syncTitle()
{
    QString title;
    bool modified = false;
    if (m_document != nullptr) {
        title = m_document->displayTitle();
        modified = m_document->isDirty();
    } else {
        title = tr("No document");
    }
    // AGENT-NOTE: [*] is Qt's window-modified placeholder; setWindowModified
    // feeds it on X11/Wayland window managers that show a dirty mark.
    setWindowTitle(QStringLiteral("[*]%1 — %2")
                       .arg(title, QApplication::applicationDisplayName()));
    setWindowModified(modified);
}

QString DocumentWindow::filterString() const
{
    // M1 stub filter; the real per-app filters arrive with their engines.
    return tr("Documents (*.txt *.md *.html *.htm);;All files (*)");
}

void DocumentWindow::rebuildRecentMenu()
{
    if (m_recentMenu == nullptr) {
        return;
    }
    m_recentMenu->clear();
    Core::RecentFiles *recent = m_services != nullptr ? m_services->recentFiles() : nullptr;
    if (recent == nullptr) {
        m_recentMenu->setEnabled(false);
        return;
    }
    m_recentMenu->setEnabled(true);
    const QStringList paths = recent->paths();
    for (const QString &path : paths) {
        QAction *item = m_recentMenu->addAction(QFileInfo(path).fileName());
        item->setToolTip(path);
        connect(item, &QAction::triggered, this, [this, path] {
            emit openPathsRequested({path});
        });
    }
    if (!paths.isEmpty()) {
        m_recentMenu->addSeparator();
        QAction *clear = m_recentMenu->addAction(tr("Clear Menu"));
        connect(clear, &QAction::triggered, this, [recent] { recent->clear(); });
    }
}

void DocumentWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_menuExportStarted) {
        m_menuExportStarted = true;
        startMenuExport();
    }
}

void DocumentWindow::closeEvent(QCloseEvent *event)
{
    // Per-document Save/Discard/Cancel consent (ADR-0110). The desktop's
    // containers keep every other window alive; closing is never a process
    // decision.
    if (m_document != nullptr && m_document->isDirty()) {
        const auto choice = QMessageBox::warning(
            this, tr("Unsaved changes"),
            tr("Save changes to %1 before closing?").arg(m_document->displayTitle()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Save) {
            if (!saveDocument()) {
                event->ignore();
                return;
            }
        } else if (choice == QMessageBox::Cancel) {
            event->ignore();
            return;
        } else {
            // Discard: the journal entry goes with the close (plan section
            // 2.7: cleared on save/discard).
            clearJournalEntry();
        }
    }
    QMainWindow::closeEvent(event);
}

void DocumentWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void DocumentWindow::dropEvent(QDropEvent *event)
{
    // Dropped files open as ordinary windows via the application (never as
    // tabs of this window) - ADR-0110.
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        emit openPathsRequested(paths);
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dropEvent(event);
}

void DocumentWindow::startMenuExport()
{
    if (windowHandle() == nullptr) {
        return;
    }
    m_menuExport = new GlobalMenuExport(menuBar(), windowHandle(), this);
    connect(m_menuExport, &GlobalMenuExport::exportedChanged, this,
            &DocumentWindow::applyMenuBarVisibility);
    applyMenuBarVisibility(m_menuExport->start());
}

void DocumentWindow::applyMenuBarVisibility(bool exported)
{
    // Fail-closed rule: only a confirmed announcement hides the local bar;
    // the View action keeps local control either way.
    const bool showLocal = !exported && m_actions.viewShowMenuBar->isChecked();
    menuBar()->setVisible(showLocal);
}

} // namespace QindaOffice::Shell
