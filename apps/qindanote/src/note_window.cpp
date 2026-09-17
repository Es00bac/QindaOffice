// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_window.h"

#include "note_find_bar.h"
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

    auto *tools = new Shell::WrappingToolBar(tr("Note tools"), this);
    // AGENT-GUARD: a QToolBar without an objectName is skipped (with a
    // runtime warning) by QMainWindow::saveState/restoreState, and the
    // platform theme keys toolbars by it — same contract as the shell's
    // "mainToolBar".
    tools->setObjectName(QStringLiteral("noteToolsToolBar"));
    const QList<std::tuple<QString, QLatin1String, NoteTool>> entries = {
        {tr("Pen"), QLatin1String("notePenTool"), NoteTool::Pen},
        {tr("Highlight"), QLatin1String("noteHighlighterTool"), NoteTool::Highlighter},
        {tr("Eraser"), QLatin1String("noteEraserTool"), NoteTool::Eraser},
        {tr("Text"), QLatin1String("noteTextTool"), NoteTool::Text},
    };
    QActionGroup *toolGroup = new QActionGroup(this);
    bool firstTool = true;
    for (const auto &[label, id, tool] : entries) {
        auto *action = new QAction(label, this);
        tools->addStripAction(action);
        // Stable objectName: the dbusmenu/global-menu activation identity
        // (AGENTS.md rule 5) — unnamed actions are unreachable from the panel.
        action->setObjectName(id);
        action->setCheckable(true);
        // Exclusive tool group: the earlier release let every tool appear
        // checked simultaneously and showed none as the initial state.
        toolGroup->addAction(action);
        action->setChecked(firstTool);
        firstTool = false;
        connect(action, &QAction::triggered, this, [this, action, tool] {
            m_view->setTool(tool);
            action->setChecked(true);
        });
    }
    tools->addStripSeparator();

    // M15 polish: pen width/color, page background and zoom live on the
    // tool strip (they are presentation state, not document commands, so
    // they are toolbar widgets rather than ActionId commands).
    auto *width = new QSpinBox(tools);
    width->setObjectName(QStringLiteral("penWidthSpin"));
    width->setRange(1, 24);
    width->setValue(int(m_view->penWidth()));
    width->setSuffix(tr(" px"));
    width->setToolTip(tr("Pen width"));
    tools->addStripWidget(width);
    connect(width, &QSpinBox::valueChanged, m_view,
            [this](int value) { m_view->setPenWidth(value); });

    auto *colorAction = new QAction(tr("Pen color"), this);
    tools->addStripAction(colorAction);
    colorAction->setObjectName(QStringLiteral("notePenColorAction"));
    connect(colorAction, &QAction::triggered, this, [this] {
        const QColor picked =
            QColorDialog::getColor(m_view->penColor(), this, tr("Pen Color"));
        if (picked.isValid()) {
            m_view->setPenColor(picked);
        }
    });

    auto *background = new QComboBox(tools);
    background->setObjectName(QStringLiteral("pageBackgroundCombo"));
    background->addItem(tr("Blank"), QStringLiteral("blank"));
    background->addItem(tr("Ruled"), QStringLiteral("ruled"));
    background->addItem(tr("Grid"), QStringLiteral("grid"));
    background->addItem(tr("Dotted"), QStringLiteral("dotted"));
    tools->addStripWidget(background);
    connect(background, &QComboBox::activated, this, [this, background](int index) {
        m_view->setBackground(background->itemData(index).toString());
    });

    auto *zoom = new QComboBox(tools);
    zoom->setObjectName(QStringLiteral("zoomCombo"));
    for (const int percent : {50, 75, 100, 150, 200}) {
        zoom->addItem(tr("%1%").arg(percent), percent);
    }
    zoom->setCurrentIndex(2);
    tools->addStripWidget(zoom);
    connect(zoom, &QComboBox::activated, this,
            [this, zoom](int index) { m_view->setZoom(zoom->itemData(index).toInt() / 100.0); });
    // View > Zoom In/Out/Reset drive the same view state as the combo, and
    // the combo follows so the two never disagree. All three actions were
    // created disabled and never wired.
    const auto syncZoomCombo = [this, zoom] {
        const int percent = qRound(m_view->zoom() * 100.0);
        const int index = zoom->findData(percent);
        QSignalBlocker blocker(zoom);
        if (index >= 0) {
            zoom->setCurrentIndex(index);
        } else {
            zoom->setCurrentText(tr("%1%").arg(percent));
        }
    };
    // Find was three dead menu entries until the notebook gained a find bar.
    Shell::StandardActions *findActions = standardActions();
    for (QAction *action : {findActions->editFind, findActions->editFindNext,
                            findActions->editFindPrevious}) {
        action->setEnabled(true);
    }
    connect(findActions->editFind, &QAction::triggered, m_findBar, &NoteFindBar::openFind);
    connect(findActions->editFindNext, &QAction::triggered, m_findBar,
            &NoteFindBar::findNext);
    connect(findActions->editFindPrevious, &QAction::triggered, m_findBar,
            &NoteFindBar::findPrevious);
    connect(m_findBar, &NoteFindBar::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });
    connect(m_findBar, &NoteFindBar::pageRequested, this,
            [this](const QString &sectionId, const QString &pageId) {
                selectPage(sectionId, pageId);
            });

    // Print and preview: created disabled and never wired.
    Shell::StandardActions *printActions = standardActions();
    printActions->filePrint->setEnabled(true);
    connect(printActions->filePrint, &QAction::triggered, this,
            [this] { Shell::PrintHelper::print(this, printRenderCallback()); });
    printActions->filePrintPreview->setEnabled(true);
    connect(printActions->filePrintPreview, &QAction::triggered, this,
            [this] { Shell::PrintHelper::printPreview(this, printRenderCallback()); });

    Shell::StandardActions *zoomActions = standardActions();
    for (QAction *action : {zoomActions->viewZoomIn, zoomActions->viewZoomOut,
                            zoomActions->viewZoomReset}) {
        action->setEnabled(true);
    }
    connect(zoomActions->viewZoomIn, &QAction::triggered, this, [this, syncZoomCombo] {
        m_view->setZoom(m_view->zoom() * 1.25);
        syncZoomCombo();
    });
    connect(zoomActions->viewZoomOut, &QAction::triggered, this, [this, syncZoomCombo] {
        m_view->setZoom(m_view->zoom() / 1.25);
        syncZoomCombo();
    });
    connect(zoomActions->viewZoomReset, &QAction::triggered, this, [this, syncZoomCombo] {
        m_view->setZoom(1.0);
        syncZoomCombo();
    });
    tools->addStripSeparator();
    // AGENT-GUARD: put the SHELL's Undo/Redo on the strip, do not clone them.
    // setDocumentHost() deleted the stub editor the base connections pointed
    // at, so Ctrl+Z fired into the void and the strip carried a second,
    // separate "Undo" action that was given the same objectName as the menu
    // one — two actions answering to `editUndoAction` in one window, which is
    // exactly the identity collision rule 5 forbids. One action per command:
    // the strip, the Edit menu and the exported global menu now share it.
    Shell::StandardActions *actions = standardActions();
    tools->addStripAction(actions->editUndo);
    tools->addStripAction(actions->editRedo);
    connect(actions->editUndo, &QAction::triggered, m_view, &NotePageView::undo);
    connect(actions->editRedo, &QAction::triggered, m_view, &NotePageView::redo);
    // Both commands follow the view's real history depth. Undo used to be
    // force-enabled with nothing to undo, and Redo was never wired at all.
    actions->editUndo->setEnabled(false);
    actions->editRedo->setEnabled(false);
    connect(m_view, &NotePageView::undoAvailable, actions->editUndo, &QAction::setEnabled);
    connect(m_view, &NotePageView::redoAvailable, actions->editRedo, &QAction::setEnabled);

    auto *text = new QAction(tr("Add text"), this);
    tools->addStripAction(text);
    text->setObjectName(QStringLiteral("noteAddTextAction"));
    connect(text, &QAction::triggered, m_view, &NotePageView::addTextBlock);

    auto *checklist = new QAction(tr("Add checklist item"), this);
    checklist->setObjectName(QStringLiteral("noteAddCheckAction"));
    checklist->setIconText(tr("Checklist"));
    tools->addStripAction(checklist);
    connect(checklist, &QAction::triggered, m_view, &NotePageView::addCheckItem);

    auto *image = new QAction(tr("Add image"), this);
    image->setObjectName(QStringLiteral("noteAddImageAction"));
    image->setIconText(tr("Image"));
    tools->addStripAction(image);
    connect(image, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Insert Image"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(tr("Could not read %1").arg(path), 8000);
            return;
        }
        // The file's own bytes, verbatim: a notebook page is one
        // self-contained JSON file, so a path reference would break the
        // moment the notebook moved.
        m_view->addImageBlock(file.readAll());
        m_document->setDirty(true);
    });

    setDocumentHost(central, m_document);
    Shell::WrappingToolBar *mainToolBar = addMainToolBar();
    Q_UNUSED(mainToolBar);
    addToolBar(Qt::LeftToolBarArea, tools);

    connect(m_view, &NotePageView::pageEdited, this, [this] {
        m_document->setDirty(true);
        persistCurrentPage();
    });
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
                persistCurrentPage();
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

NoteWindow::~NoteWindow() = default;

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
