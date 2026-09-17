// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_window.h"

#include "note_find_bar.h"
#include "note_ink_actions.h"
#include "note_page_render.h"
#include "note_page_view.h"
#include "note_structure.h"

#include "standard_actions.h"
#include "wrapping_tool_bar.h"

#include <QColorDialog>
#include <QPainter>
#include <QPrinter>
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QActionGroup>
#include <QMenu>
#include <QSpinBox>
#include <QMenuBar>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace QindaOffice::Apps::Notes {

NoteDocument::NoteDocument()
{
    // A pristine untitled notebook: lazily created on first save, but NOT
    // empty. An empty notebook showed an empty navigator and a page surface
    // bound to nothing, so a fresh QindaNote window could not be written in
    // at all until the user found Page ▸ New Page.
    NoteSection &section = m_notebook.addSection(tr("Notes"));
    m_notebook.addPage(section, tr("Page 1"));
}

bool NoteDocument::load(const QString &path, QString *error)
{
    if (!m_notebook.load(path, error)) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    return true;
}

bool NoteDocument::save(const QString &path, QString *error)
{
    // AGENT-GUARD: Save As must honour its argument for every notebook state.
    // The earlier release ignored `path` whenever a notebook was already
    // open, silently writing to the OLD directory and reporting the new one.
    const QString target = path;
    if (m_notebook.directory().isEmpty()) {
        if (!m_notebook.create(target, error)) {
            return false;
        }
    } else if (QFileInfo(target).absolutePath() != QFileInfo(m_notebook.directory()).absolutePath()
               && !m_notebook.copyStructure(target, error)) {
        // Save As to a different directory: materialise the notebook there.
        return false;
    }
    if (!m_notebook.save(error)) {
        return false;
    }
    setCanonicalPath(m_notebook.directory());
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList NoteDocument::mimeTypes() const
{
    return {QStringLiteral("application/x-qinda-notebook")};
}

NoteWindow::NoteWindow()
    : m_document(new NoteDocument)
    , m_view(new NotePageView)
    , m_navigator(new QTreeWidget)
{
    m_navigator->setHeaderHidden(true);
    auto *central = new QWidget(this);
    // Navigator beside the page, with a find bar underneath both that stays
    // hidden until Ctrl+F (the shape QindaWrite and QindaCalc use).
    auto *stack = new QVBoxLayout(central);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);
    auto *row = new QWidget(central);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_navigator, 1);
    // AGENT-GUARD: the page lives in a scroll area. The page widget is as
    // tall as a full sheet (and taller once it grows, or when zoomed in);
    // putting it straight in the layout made that height the WINDOW's
    // minimum, so QindaNote could not be resized below ~1100 px.
    m_scroll = new QScrollArea(row);
    m_scroll->setObjectName(QStringLiteral("notePageScroll"));
    m_scroll->setWidget(m_view);
    m_scroll->setWidgetResizable(false);
    m_scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_scroll, 4);
    stack->addWidget(row, 1);
    m_findBar = new NoteFindBar(m_document, central);
    m_findBar->hide();
    stack->addWidget(m_findBar, 0);

    // The Ink commands exist before the strip and the menus: both hang off
    // the same actions (rule 5: one command = one action).
    m_inkActions = new NoteInkActions(this, m_view);
    Shell::WrappingToolBar *tools = buildToolStrip();
    setDocumentHost(central, m_document);
    Shell::WrappingToolBar *mainToolBar = addMainToolBar();
    Q_UNUSED(mainToolBar);
    addToolBar(Qt::TopToolBarArea, tools);

    // Per-keystroke page writes are debounced (defect T10: every keystroke
    // and every stroke rewrote the whole page JSON, base64 images and
    // all). The recovery journal covers the window in between; flushPersist()
    // forces the write at the points where it must not lag (page switch,
    // export, replace sweep, close).
    m_persistTimer = new QTimer(this);
    m_persistTimer->setSingleShot(true);
    m_persistTimer->setInterval(500);
    connect(m_persistTimer, &QTimer::timeout, this, &NoteWindow::persistCurrentPage);
    connect(m_view, &NotePageView::pageEdited, this, [this] {
        m_document->setDirty(true);
        m_persistTimer->start();
    });
    connect(m_view, &NotePageView::presentationChanged, this,
            [this] { updatePageStatus(); });
    connect(m_navigator, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (item == nullptr) {
                    return;
                }
                // Resolve through ids: the notebook's containers mutate
                // freely, raw pointers into them dangle.
                const QString sectionId = item->data(0, Qt::UserRole).toString();
                const QString pageId = item->data(0, Qt::UserRole + 1).toString();
                if (pageId.isEmpty()) {
                    return; // section header
                }
                Notebook *notebook = m_document->notebook();
                NoteSection *section = notebook->sectionById(sectionId);
                NotePage *page = notebook->pageById(sectionId, pageId);
                if (section == nullptr || page == nullptr) {
                    return;
                }
                // The outgoing page's debounced write must land before the
                // ids move on, or its last 500 ms of edits never reach disk.
                flushPersist();
                // AGENT-GUARD: read from disk ONLY for a page that has not
                // been loaded yet. Re-reading a loaded page threw away edits
                // that had not reached disk, and an untitled notebook has no
                // disk copy at all — its pages live only in memory.
                if (!page->loaded) {
                    QString error;
                    if (!notebook->loadPage(*section, *page, &error)) {
                        // A page file that fails to load must not silently
                        // blank the view and later overwrite the payload.
                        statusBar()->showMessage(tr("Page load failed: %1").arg(error), 8000);
                        return;
                    }
                }
                setCurrentIds(sectionId, pageId);
                m_view->setPage(page);
            });
    m_structure = new NoteStructureActions(this);
    tools->addStripSeparator();
    for (QAction *action : m_structure->stripActions()) {
        tools->addStripAction(action);
    }
    rebuildNavigator();
    wireNavigatorInteraction();
    wireEditActions();
    wireAssistant();

    // The shell's loadPath (File ▸ Open, drag-drop, command line) only calls
    // NoteDocument::load; there is no app hook — so watch contentChanged and
    // rebuild here. Without this the navigator stayed empty and every input
    // event was dropped: a notebook opened through the UI was unusable.
    connect(m_document, &Core::Document::contentChanged, this, [this] {
        m_currentSectionId.clear();
        m_currentPageId.clear();
        m_view->setPage(nullptr);
        rebuildNavigator();
    });

    // App menus (Page) need the fully constructed window - the base ctor's
    // hook dispatch only reaches the base no-op.
    finalizeAppMenuHooks();
}

NoteWindow::~NoteWindow()
{
    // A debounced write that never fired would silently drop the last half
    // second of edits; land it while the notebook is still fully alive.
    flushPersist();
}

void NoteWindow::flushPersist()
{
    if (m_persistTimer != nullptr && m_persistTimer->isActive()) {
        m_persistTimer->stop();
        persistCurrentPage();
    }
}

void NoteWindow::setCurrentIds(const QString &sectionId, const QString &pageId)
{
    m_currentSectionId = sectionId;
    m_currentPageId = pageId;
    if (m_findBar != nullptr) {
        // Keep Find Next advancing from where the user actually is.
        m_findBar->setCurrentPage(sectionId, pageId);
    }
}

void NoteWindow::selectPage(const QString &sectionId, const QString &pageId)
{
    for (int s = 0; s < m_navigator->topLevelItemCount(); ++s) {
        QTreeWidgetItem *sectionItem = m_navigator->topLevelItem(s);
        for (int p = 0; p < sectionItem->childCount(); ++p) {
            QTreeWidgetItem *pageItem = sectionItem->child(p);
            if (pageItem->data(0, Qt::UserRole).toString() == sectionId
                && pageItem->data(0, Qt::UserRole + 1).toString() == pageId) {
                m_navigator->setCurrentItem(pageItem);
                return;
            }
        }
    }
}

void NoteWindow::persistCurrentPage()
{
    if (m_currentSectionId.isEmpty() || m_currentPageId.isEmpty()) {
        return;
    }
    Notebook *notebook = m_document->notebook();
    NoteSection *section = notebook->sectionById(m_currentSectionId);
    NotePage *page = notebook->pageById(m_currentSectionId, m_currentPageId);
    if (section == nullptr || page == nullptr) {
        return;
    }
    if (!notebook->isOpen()) {
        // Untitled: nothing to write to yet. The edit is already in memory
        // and covered by the recovery journal until the first real save.
        return;
    }
    QString error;
    if (!notebook->persistPage(*section, *page, &error)) {
        // Fail visibly: a page write that fails must surface, not vanish
        // (the earlier release collected the error into a local and dropped it).
        statusBar()->showMessage(tr("Save failed: %1").arg(error), 8000);
    }
}

Shell::PrintRenderCallback NoteWindow::printRenderCallback() const
{
    // Snapshot the pages up front: the render runs inside the print dialog
    // and must not read a notebook the user could still be editing. Pages
    // that were never opened are read from disk first, so Print covers the
    // whole notebook and not just the one page on screen.
    QList<NotePage> pages;
    Notebook *notebook = m_document->notebook();
    for (const NoteSection &constSection : notebook->sections()) {
        NoteSection *section = notebook->sectionById(constSection.id);
        if (section == nullptr) {
            continue;
        }
        for (NotePage &page : section->pages) {
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            if (page.loaded) {
                pages.append(page);
            }
        }
    }
    const QSizeF pageSize = m_view->pageRect().size() / m_view->zoom();
    return [pages, pageSize](QPrinter *printer, QPainter *painter) {
        if (pages.isEmpty()) {
            return;
        }
        const QRectF sheet(0, 0, printer->width(), printer->height());
        const qreal scale = qMin(sheet.width() / pageSize.width(),
                                 sheet.height() / pageSize.height());
        for (int i = 0; i < pages.size(); ++i) {
            if (i > 0) {
                printer->newPage();
            }
            painter->save();
            painter->translate(sheet.center()
                               - QPointF(pageSize.width() * scale / 2.0,
                                         pageSize.height() * scale / 2.0));
            painter->scale(scale, scale);
            renderNotePage(painter, pages.at(i), pageSize);
            painter->restore();
        }
    };
}

void NoteWindow::openNotebookPath(const QString &path)
{
    QString error;
    if (m_document->load(path, &error)) {
        statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Open failed: %1").arg(error), 8000);
    }
}

void NoteWindow::rebuildNavigator()
{
    // AGENT-GUARD: items carry ids (UserRole: section id, UserRole+1: page
    // id) resolved through Notebook::sectionById/pageById at activation time.
    QTreeWidgetItem *firstPage = nullptr;
    {
        // The blocker covers the TEARDOWN and rebuild only: clear() fires
        // currentItemChanged for every item it destroys, and the handler
        // would try to persist pages that no longer exist.
        const QSignalBlocker blocker(m_navigator);
        m_navigator->clear();
        Notebook *notebook = m_document->notebook();
        for (const NoteSection &section : notebook->sections()) {
            auto *sectionItem = new QTreeWidgetItem(m_navigator, {section.name});
            sectionItem->setData(0, Qt::UserRole, section.id);
            for (const NotePage &page : section.pages) {
                auto *pageItem = new QTreeWidgetItem(sectionItem, {page.title});
                pageItem->setData(0, Qt::UserRole, section.id);
                pageItem->setData(0, Qt::UserRole + 1, page.id);
            }
            sectionItem->setExpanded(true);
        }
        // The first page, never a section header.
        for (int i = 0; i < m_navigator->topLevelItemCount() && firstPage == nullptr; ++i) {
            QTreeWidgetItem *sectionItem = m_navigator->topLevelItem(i);
            if (sectionItem->childCount() > 0) {
                firstPage = sectionItem->child(0);
            }
        }
    }
    // AGENT-GUARD: select OUTSIDE the blocker. The selection used to happen
    // inside it, so currentItemChanged never fired for it — the navigator
    // showed a highlighted first page that was never actually opened, and
    // the page surface stayed bound to nothing until the user clicked.
    if (firstPage != nullptr) {
        m_navigator->setCurrentItem(firstPage);
    }
}

void NoteWindow::insertApplicationMenus(QMenu *before)
{
    // &Ink before &Page (plan section 3.2's menu row); both before the AI
    // anchor the shell hands us.
    auto *inkMenu = new QMenu(tr("&Ink"), this);
    inkMenu->setObjectName(QStringLiteral("noteInkMenu"));
    menuBar()->insertMenu(before->menuAction(), inkMenu);
    m_inkActions->populateInkMenu(inkMenu);

    auto *pageMenu = new QMenu(tr("&Page"), this);
    pageMenu->setObjectName(QStringLiteral("notePageMenu"));
    menuBar()->insertMenu(before->menuAction(), pageMenu);
    m_structure->populatePageMenu(pageMenu);
    pageMenu->addSeparator();
    QAction *exportAction = pageMenu->addAction(tr("&Export PDF…"));
    exportAction->setObjectName(QStringLiteral("noteExportPdf"));
    connect(exportAction, &QAction::triggered, this, &NoteWindow::exportPdf);
}

QString NoteWindow::filterString() const
{
    return tr("QindaNote notebooks (notebook.json);;All files (*)");
}

} // namespace QindaOffice::Apps::Notes
