// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"

#include "global_menu_export.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

namespace QindaOffice::Shell {

DocumentWindow::DocumentWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createActions();
    createMenus();

    auto *placeholder = new QLabel(
        tr("<h3>%1</h3><p>QindaQt Office preview build (milestone M0).</p>"
           "<p>The document engine lands with the office_core milestone; menus "
           "and window behaviour are already the shipping ones.</p>")
            .arg(qAppName()),
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setMargin(12);
    setCentralWidget(placeholder);

    statusBar()->showMessage(tr("Preview build — M0 skeleton"), 10000);
    resize(960, 640);
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
    connect(m_actions.fileOpen, &QAction::triggered, this, &DocumentWindow::openRequested);
    connect(m_actions.fileCloseWindow, &QAction::triggered, this, &QWidget::close);
    connect(m_actions.fileQuit, &QAction::triggered, this, &QWidget::close);

    connect(m_actions.viewShowMenuBar, &QAction::toggled, this, [this](bool show) {
        // The exported menu lives in the panel; the local bar is a fallback
        // and a user preference. Toggling never touches the export itself.
        menuBar()->setVisible(show);
    });

    connect(m_actions.helpAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("About %1").arg(qAppName()),
            tr("<b>%1</b> — part of QindaQt Office %2.<br/>One document per "
               "window on the QindaQt desktop.")
                .arg(qAppName(), QStringLiteral(QQO_VERSION)));
    });
    connect(m_actions.helpAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);

    // M0 honesty: open has no engine yet; say so in the status bar instead of
    // silently doing nothing (AGENT-NOTE: M1 replaces this connection).
    connect(m_actions.fileOpen, &QAction::triggered, this, [this] {
        statusBar()->showMessage(tr("Open arrives with the office_core milestone (M1)."),
                                 5000);
    });
}

void DocumentWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_actions.fileNew);
    fileMenu->addAction(m_actions.fileOpen);
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
    settingsMenu->addAction(m_actions.settingsPreferences);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_actions.helpAbout);
    helpMenu->addAction(m_actions.helpAboutQt);
}

void DocumentWindow::insertApplicationMenus(QMenu *before)
{
    Q_UNUSED(before);
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
    // M0: windows close directly. M1 (office_core) inserts the per-document
    // Save/Discard/Cancel consent here before accepting the event.
    QMainWindow::closeEvent(event);
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
