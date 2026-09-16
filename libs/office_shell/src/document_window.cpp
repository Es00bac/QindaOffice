// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"

#include "app_services.h"
#include "global_menu_export.h"
#include "wrapping_tool_bar.h"

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
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

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
        // Open next to the current document (or in Documents) rather than in
        // whatever directory the process happened to be launched from.
        const QString current =
            m_document != nullptr ? m_document->canonicalPath() : QString();
        const QString startDir = current.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : QFileInfo(current).absolutePath();
        const QStringList paths =
            QFileDialog::getOpenFileNames(this, tr("Open"), startDir, filterString());
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
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
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
    aiMenu->setObjectName(QStringLiteral("aiMenu"));
    menuBar()->addMenu(aiMenu);
    // App menus are inserted in two passes: the base no-op here (virtual
    // dispatch during construction), then the real insertion when the
    // subclass ctor calls insertApplicationMenus(aiMenu()) after the shell
    // frame exists.
    insertApplicationMenus(aiMenu);
    aiMenu->addAction(m_actions.aiAssistant);

    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    m_restoreAction = settingsMenu->addAction(tr("Restore &Previous Documents"));
    // Stable objectName: the dbusmenu activation identity (AGENTS.md rule 5).
    m_restoreAction->setObjectName(QString::fromLatin1(ActionId::kSettingsRestorePrevious));
    m_restoreAction->setCheckable(true);
    m_restoreAction->setEnabled(false); // enabled by setAppServices
    settingsMenu->addSeparator();
    settingsMenu->addAction(m_actions.settingsPreferences);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_actions.helpAbout);
    helpMenu->addAction(m_actions.helpAboutQt);
}

void DocumentWindow::insertFileMenuActions(QMenu *fileMenu)
{
    Q_UNUSED(fileMenu);
}

void DocumentWindow::insertFileMenuActionsDeferred()
{
    if (auto *fileMenu = findChild<QMenu *>(QStringLiteral("fileMenu"))) {
        insertFileMenuActions(fileMenu);
    }
}

void DocumentWindow::insertApplicationMenus(QMenu *before)
{
    Q_UNUSED(before);
}

void DocumentWindow::finalizeAppMenuHooks()
{
    if (m_appMenusFinalized) {
        return;
    }
    m_appMenusFinalized = true;
    insertFileMenuActionsDeferred();
    if (auto *aiMenu = findChild<QMenu *>(QStringLiteral("aiMenu"))) {
        insertApplicationMenus(aiMenu);
    }
}

WrappingToolBar *DocumentWindow::addMainToolBar()
{
    if (m_mainToolBar != nullptr) {
        return m_mainToolBar;
    }
    m_mainToolBar = new WrappingToolBar(tr("Main toolbar"), this);
    // Stable objectName: QMainWindow state restore and the platform theme
    // key toolbars by it.
    m_mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_mainToolBar->addStripAction(m_actions.fileNew);
    m_mainToolBar->addStripAction(m_actions.fileOpen);
    m_mainToolBar->addStripAction(m_actions.fileSave);
    m_mainToolBar->addStripSeparator();
    m_mainToolBar->addStripAction(m_actions.editUndo);
    m_mainToolBar->addStripAction(m_actions.editRedo);
    m_mainToolBar->addStripSeparator();
    m_mainToolBar->addStripAction(m_actions.editCut);
    m_mainToolBar->addStripAction(m_actions.editCopy);
    m_mainToolBar->addStripAction(m_actions.editPaste);
    m_mainToolBar->addStripSeparator();
    m_mainToolBar->addStripAction(m_actions.editFind);
    m_mainToolBar->addStripSeparator();
    m_mainToolBar->addStripAction(m_actions.viewZoomOut);
    m_mainToolBar->addStripAction(m_actions.viewZoomIn);
    addToolBar(Qt::TopToolBarArea, m_mainToolBar);
    return m_mainToolBar;
}

void DocumentWindow::setDocumentHost(QWidget *widget, Core::Document *document)
{
    if (m_editor != nullptr) {
        delete m_editor;
        m_editor = nullptr;
    }
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
        delete m_document;
    }
    m_document = document;
    if (m_document != nullptr) {
        m_document->setParent(this);
        connect(m_document, &Core::Document::dirtyChanged, this, [this] { syncTitle(); });
        connect(m_document, &Core::Document::contentChanged, this,
                [this] { scheduleJournalSnapshot(); });
    }
    setCentralWidget(widget);
    // AGENT-GUARD: a real document host CAN save, so Save/Save As must leave
    // the createStandardActions() "no engine yet" disabled state here. Only
    // setDocument()'s PlainTextDocument branch used to enable them, and all
    // four apps install their engine through setDocumentHost() instead — so
    // File > Save, File > Save As, the toolbar Save button and Ctrl+S were
    // dead in every app while saveDocument() itself worked fine (the close
    // consent dialog's Save button reached it).
    m_actions.fileSave->setEnabled(m_document != nullptr);
    m_actions.fileSaveAs->setEnabled(m_document != nullptr);
    syncTitle();
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
    // AGENT-GUARD: items are RECREATED on every open/save, so their
    // objectNames must be stable across rebuilds for the desktop global
    // menu to address them; encode the recent-list position.
    int index = 0;
    for (const QString &path : paths) {
        QAction *item = m_recentMenu->addAction(QFileInfo(path).fileName());
        item->setObjectName(QStringLiteral("fileRecentItem%1").arg(index));
        item->setToolTip(path);
        connect(item, &QAction::triggered, this, [this, path] {
            emit openPathsRequested({path});
        });
        ++index;
    }
    if (!paths.isEmpty()) {
        m_recentMenu->addSeparator();
        QAction *clear = m_recentMenu->addAction(tr("Clear Menu"));
        clear->setObjectName(QStringLiteral("fileRecentClear"));
        connect(clear, &QAction::triggered, this, [recent] { recent->clear(); });
    }
}

void DocumentWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // Safety net for any subclass that forgot the ctor call: the app menus
    // must exist before the global-menu export snapshots them below.
    finalizeAppMenuHooks();
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
    emit closing(this);
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
