// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_host.h"

#include "navigator_model.h"
#include "note_live_session.h"
#include "note_blocks.h"
#include "note_page_item.h"
#include "note_page_render.h"
#include "note_search.h"
#include "note_tag_summary.h"
#include "note_tags.h"
#include "note_templates.h"
#include "preferences.h"
#include "standard_actions.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPrinter>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;


NoteHost::NoteHost()
    : m_document(new NoteDocument)
    , m_navigator(new NavigatorModel(this))
{
    setDocument(m_document);
    m_navigator->setNotebook(m_document->notebook());
    // Per-keystroke page writes are debounced (500 ms); flushPersist()
    // forces the write where it must not lag (page switch, export, close).
    m_persistTimer = new QTimer(this);
    m_persistTimer->setSingleShot(true);
    m_persistTimer->setInterval(500);
    connect(m_persistTimer, &QTimer::timeout, this, &NoteHost::persistCurrentPage);

    createInkActions();
    createStructureActions();
    // M84 (D-093): formatted typing, tags, search, import and page polish.
    createFormatActions();
    createTagActions();
    createPageActions();
    m_search = new NoteSearchModel(this);
    NoteSearch::registerHost(this);
    createAssistant();
    insertMenus();
    // After insertMenus: live notes add to the Page menu too.
    createLiveNotes();
    wireEditCommands();
    rebuildNavigator();
    syncFormatActions();
    syncTagActions();
    syncPageActions();
    finalizeHost();
}

NoteHost::~NoteHost()
{
    NoteSearch::unregisterHost(this);
    // AGENT-GUARD: end a running session while this host is still whole —
    // as a QObject child it would be destroyed after ~NoteHost, and its
    // destructor writes the transcript through the host.
    delete m_live;
    m_live = nullptr;
    // A debounced write that never fired would silently drop the last
    // half second of edits; land it while the notebook is still alive.
    flushPersist();
}

void NoteHost::insertMenus()
{
    MenuNode ink = MenuNode::submenu(QStringLiteral("noteInkMenu"), tr("&Ink"));
    ink.children = {
        MenuNode::action(QStringLiteral("notePenTool")),
        MenuNode::action(QStringLiteral("noteHighlighterTool")),
        MenuNode::action(QStringLiteral("noteEraserTool")),
        MenuNode::action(QStringLiteral("noteTextTool")),
        MenuNode::separator(),
        MenuNode::action(QStringLiteral("noteTouchDrawAction")),
        MenuNode::separator(),
        MenuNode::submenu(QStringLiteral("noteInkPenColorMenu"), tr("Pen &Colour")),
        MenuNode::submenu(QStringLiteral("noteInkPenWidthMenu"), tr("Pen &Width")),
        MenuNode::submenu(QStringLiteral("noteInkHighlighterColorMenu"), tr("&Highlighter Colour")),
        MenuNode::submenu(QStringLiteral("noteInkHighlighterWidthMenu"), tr("Highlighter Wi&dth")),
        MenuNode::submenu(QStringLiteral("noteInkEraserMenu"), tr("&Eraser")),
    };
    for (const QString &prefix : {QStringLiteral("notePenColor_"), QStringLiteral("notePenWidth_"),
                                  QStringLiteral("noteHighlighterColor_"),
                                  QStringLiteral("noteHighlighterWidth_"),
                                  QStringLiteral("noteEraserMode_"), QStringLiteral("noteEraserSize_")}) {
        const QString menuId = prefix == QLatin1String("notePenColor_") ? QStringLiteral("noteInkPenColorMenu")
            : prefix == QLatin1String("notePenWidth_") ? QStringLiteral("noteInkPenWidthMenu")
            : prefix == QLatin1String("noteHighlighterColor_") ? QStringLiteral("noteInkHighlighterColorMenu")
            : prefix == QLatin1String("noteHighlighterWidth_") ? QStringLiteral("noteInkHighlighterWidthMenu")
            : QStringLiteral("noteInkEraserMenu");
        for (MenuNode &child : ink.children) {
            if (child.id != menuId) {
                continue;
            }
            if (prefix == QLatin1String("noteEraserSize_") && !child.children.isEmpty()) {
                child.children.append(MenuNode::separator());
            }
            for (const QString &id : actions()->ids()) {
                if (id.startsWith(prefix)) {
                    child.children.append(MenuNode::action(id));
                }
            }
        }
    }
    // children[7] is Pen Colour (tools 0-3, separator, Draw with Finger,
    // separator).
    ink.children[7].children.append(MenuNode::separator());
    ink.children[7].children.append(MenuNode::action(QStringLiteral("notePenColorAction")));
    menuTree()->insertMenuBefore(QStringLiteral("aiMenu"), ink);

    MenuNode page = MenuNode::submenu(
        QStringLiteral("notePageMenu"), tr("&Page"),
        {MenuNode::action(QStringLiteral("notePageNew")),
         MenuNode::action(QStringLiteral("noteSectionNew")),
         MenuNode::action(QStringLiteral("notePageDuplicate")), MenuNode::separator(),
         MenuNode::action(QStringLiteral("notePageVersionsAction")), MenuNode::separator(),
         MenuNode::action(QStringLiteral("notePageNext")),
         MenuNode::action(QStringLiteral("notePagePrevious")), MenuNode::separator(),
         MenuNode::action(QStringLiteral("notePageRename")),
         MenuNode::action(QStringLiteral("noteSectionRename")), MenuNode::separator(),
         MenuNode::action(QStringLiteral("notePageMoveUp")),
         MenuNode::action(QStringLiteral("notePageMoveDown")),
         MenuNode::action(QStringLiteral("noteMakeSubpageAction")),
         MenuNode::action(QStringLiteral("notePromoteSubpageAction")), MenuNode::separator(),
         MenuNode::submenu(QStringLiteral("notePageBackground"), tr("&Rule Lines")),
         MenuNode::submenu(QStringLiteral("notePageColorMenu"), tr("Page Co&lour")),
         MenuNode::separator(), MenuNode::action(QStringLiteral("noteAddTextAction")),
         MenuNode::action(QStringLiteral("noteAddCheckAction")),
         MenuNode::action(QStringLiteral("noteAddImageAction")),
         MenuNode::action(QStringLiteral("noteDeleteBlockAction")), MenuNode::separator(),
         MenuNode::action(QStringLiteral("notePageDelete")),
         MenuNode::action(QStringLiteral("noteSectionDelete")), MenuNode::separator(),
         MenuNode::action(QStringLiteral("noteExportPdf"))});
    for (MenuNode &child : page.children) {
        if (child.id == QLatin1String("notePageBackground")) {
            for (const QString &background : backgrounds()) {
                child.children.append(MenuNode::action(QStringLiteral("notePageBackground_") + background));
            }
        } else if (child.id == QLatin1String("notePageColorMenu")) {
            for (const QString &color : pageColorIds()) {
                child.children.append(MenuNode::action(QStringLiteral("notePageColor_") + color));
            }
            child.children += {MenuNode::separator(), MenuNode::action(QStringLiteral("notePageColorAction"))};
        }
    }
    menuTree()->insertMenuBefore(QStringLiteral("aiMenu"), page);
    menuTree()->insertInto(QStringLiteral("viewMenu"), QString(),
                           MenuNode::action(QStringLiteral("noteShowNavigatorAction")));
    insertM84Menus();
}

// M84 (D-093): File ▸ Import, Edit ▸ Search Notebooks, and the Insert,
// Format and Tags (with Find Tags) menus before Ink; the Page menu gains
// templates, page colour, rule lines and subpages (note_host_structure.cpp
// builds it). Every entry is also on the ribbon (NoteRibbonLayout.qml).
void NoteHost::insertM84Menus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    menuTree()->insertInto(QStringLiteral("fileMenu"), QString::fromLatin1(ActionId::kFileCloseWindow),
                           a("noteImportAction"));
    menuTree()->insertInto(QStringLiteral("editMenu"), QString(), MenuNode::separator());
    menuTree()->insertInto(QStringLiteral("editMenu"), QString(), a("noteSearchAction"));
    MenuNode table = MenuNode::submenu(
        QStringLiteral("noteTableMenu"), tr("Ta&ble"),
        {a(ActionId::kTableInsertRowAbove), a(ActionId::kTableInsertRowBelow), a(ActionId::kTableInsertColumnLeft),
         a(ActionId::kTableInsertColumnRight), MenuNode::separator(), a(ActionId::kTableDeleteRow),
         a(ActionId::kTableDeleteColumn), a("noteTableDeleteAction")});
    MenuNode templates = MenuNode::submenu(QStringLiteral("notePageTemplateMenu"), tr("Page &Templates"));
    for (const QString &id : Templates::ids()) {
        templates.children.append(MenuNode::action(QStringLiteral("notePageTemplate_") + id));
    }
    menuTree()->insertMenuBefore(
        QStringLiteral("noteInkMenu"),
        MenuNode::submenu(QStringLiteral("noteInsertMenu"), tr("&Insert"),
                          {a(ActionId::kInsertTable), table, MenuNode::separator(), a(ActionId::kInsertHyperlink),
                           a("noteLinkFileAction"), MenuNode::separator(), templates}));
    MenuNode styles = MenuNode::submenu(QStringLiteral("noteStyleMenu"), tr("&Styles"));
    for (const char *id : {"normal", "title", "heading1", "heading2", "heading3", "heading4", "heading5", "heading6",
                           "quote", "code"}) {
        styles.children.append(MenuNode::action(QStringLiteral("noteStyle_") + QString::fromLatin1(id)));
    }
    menuTree()->insertMenuBefore(
        QStringLiteral("noteInkMenu"),
        MenuNode::submenu(QStringLiteral("noteFormatMenu"), tr("F&ormat"),
                          {a(ActionId::kFormatBold), a(ActionId::kFormatItalic), a(ActionId::kFormatUnderline),
                           a(ActionId::kFormatStrikeOut), a(ActionId::kFormatSubscript), a(ActionId::kFormatSuperscript),
                           MenuNode::separator(), a(ActionId::kFormatGrowFont), a(ActionId::kFormatShrinkFont),
                           a(ActionId::kFormatTextColor), a(ActionId::kFormatHighlight), a(ActionId::kFormatClearFormat),
                           MenuNode::separator(), styles, MenuNode::separator(), a(ActionId::kFormatListBullet),
                           a(ActionId::kFormatListNumbered), a(ActionId::kFormatIndentMore),
                           a(ActionId::kFormatIndentLess), MenuNode::separator(), a(ActionId::kFormatAlignLeft),
                           a(ActionId::kFormatAlignCenter), a(ActionId::kFormatAlignRight)}));
    MenuNode tags = MenuNode::submenu(QStringLiteral("noteTagsMenu"), tr("&Tags"));
    for (const Tags::TagDef &def : Tags::builtIn()) {
        tags.children.append(MenuNode::action(QStringLiteral("noteTag_") + def.id));
    }
    tags.children += {MenuNode::separator(), a("noteTagMoreAction"), a("noteTagCustomizeAction"),
                      a("noteTagRemoveAction"), MenuNode::separator(), a("noteFindTagsAction"),
                      a("noteTagSummaryPageAction")};
    menuTree()->insertMenuBefore(QStringLiteral("noteInkMenu"), tags);
}

// A notebook loaded or restored: every id the host held is stale.
// AGENT-GUARD: this is NOT contentChanged — that fires on every stroke (it
// journals), and resetting the page there wiped the history and cut every
// pen and eraser gesture short.
void NoteHost::documentReplaced()
{
    m_currentSectionId.clear();
    m_currentPageId.clear();
    if (m_pageItem != nullptr) {
        m_pageItem->setPage(nullptr);
    }
    rebuildNavigator();
    // M84: a search result in this notebook asked for it to be opened.
    applyPendingNavigation();
}

void NoteHost::wireEditCommands()
{
    // Undo/Redo follow the page item's ONE history (bound in attachPageItem).
    connect(action(QString::fromLatin1(ActionId::kEditUndo)), &ActionEntry::triggered, this, [this] {
        if (m_pageItem != nullptr) {
            m_pageItem->undo();
        }
    });
    connect(action(QString::fromLatin1(ActionId::kEditRedo)), &ActionEntry::triggered, this, [this] {
        if (m_pageItem != nullptr) {
            m_pageItem->redo();
        }
    });
    connect(action(QString::fromLatin1(ActionId::kEditFind)), &ActionEntry::triggered, this,
            [this] { emit findRequested(); });
    connect(action(QString::fromLatin1(ActionId::kEditFindNext)), &ActionEntry::triggered, this,
            [this] { emit findRequested(); });
    connect(action(QString::fromLatin1(ActionId::kEditFindPrevious)), &ActionEntry::triggered, this,
            [this] { emit findRequested(); });
    connect(action(QString::fromLatin1(ActionId::kEditReplace)), &ActionEntry::triggered, this,
            [this] { emit replaceRequested(); });
    actions()->setEnabled({QString::fromLatin1(ActionId::kEditFind),
                           QString::fromLatin1(ActionId::kEditFindNext),
                           QString::fromLatin1(ActionId::kEditFindPrevious),
                           QString::fromLatin1(ActionId::kEditReplace)},
                          true);
    // Cut/Copy/Paste/Select All belong to the live editor: the plain fields
    // take their own keys, and the formatted-text editor's are routed and
    // enabled in note_host_format.cpp (M84). Zoom goes to the viewport
    // (enabled here, routed in NoteWindow.qml).
    actions()->setEnabled({QString::fromLatin1(ActionId::kViewZoomIn),
                           QString::fromLatin1(ActionId::kViewZoomOut),
                           QString::fromLatin1(ActionId::kViewZoomReset)},
                          true);
    ActionEntry *exportPdf = actions()->add(QStringLiteral("noteExportPdf"), tr("&Export PDF…"));
    connect(exportPdf, &ActionEntry::triggered, this, [this] {
        emit exportPdfRequested(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                + QLatin1Char('/') + m_document->displayTitle() + QStringLiteral(".pdf"));
    });
    // AGENT-NOTE (M84, D-093): F9 is live notes' "This Matters" (D-061);
    // while a session listened, the two were one ambiguous shortcut and F9
    // did nothing. The navigator takes OneNote's page-list key instead.
    ActionEntry *navigator = actions()->add(QStringLiteral("noteShowNavigatorAction"),
                                            tr("Show &Navigator"), QKeySequence(QStringLiteral("Ctrl+Shift+G")),
                                            QStringLiteral("panel-left"));
    navigator->setCheckable(true);
    navigator->setChecked(true);
    connect(navigator, &ActionEntry::toggled, this,
            [this](bool visible) { emit navigatorToggleRequested(visible); });
}

void NoteHost::attachPageItem(NotePageItem *item)
{
    m_pageItem = item;
    if (item == nullptr) {
        return;
    }
    connect(item, &NotePageItem::pageEdited, this, [this] {
        m_document->setDirty(true);
        m_document->notifyContentChanged();
        m_persistTimer->start();
        m_tagRefresh->start(); // an open Find Tags pane follows edits
    });
    // M84: the ribbon's checked states and enabled commands follow the open
    // text; links open in their apps.
    connect(item, &NotePageItem::richStateChanged, this, [this] { syncFormatActions(); });
    connect(item, &NotePageItem::editingChanged, this, [this] {
        syncFormatActions();
        syncTagActions();
    });
    connect(item, &NotePageItem::linkActivated, this, &NoteHost::openLink);
    connect(item, &NotePageItem::historyChanged, this, [this] {
        actions()->setEnabled(QString::fromLatin1(ActionId::kEditUndo), m_pageItem->canUndo());
        actions()->setEnabled(QString::fromLatin1(ActionId::kEditRedo), m_pageItem->canRedo());
    });
    connect(item, &NotePageItem::presentationChanged, this, [this] {
        syncInkChecks();
        updatePageStatus();
    });
    connect(item, &NotePageItem::pageChanged, this, [this] {
        syncStructureEnablement();
        syncPageActions();
    });
    connect(item, &NotePageItem::pageTitleChanged, this, [this] {
        m_navigator->rebuild();
        emit currentPageChanged();
        updatePageStatus();
    });
    loadToolState();
    applyPreferences();
    if (!m_currentPageId.isEmpty()) {
        openPage(m_currentSectionId, m_currentPageId);
    }
}

void NoteHost::rebuildNavigator()
{
    m_navigator->rebuild();
    if (m_currentPageId.isEmpty()) {
        // The first page, never a section header.
        for (const NoteSection &section : m_document->notebook()->sections()) {
            if (!section.pages.isEmpty()) {
                openPage(section.id, section.pages.first().id);
                break;
            }
        }
    } else {
        m_navigator->setCurrent(m_currentSectionId, m_currentPageId);
    }
    updatePageStatus();
    syncStructureEnablement();
}

void NoteHost::selectRow(int row)
{
    const QString pageId = m_navigator->pageIdAt(row);
    const QString sectionId = m_navigator->sectionIdAt(row);
    if (pageId.isEmpty()) {
        m_navigator->toggleSection(sectionId);
        return;
    }
    selectPage(sectionId, pageId);
}

void NoteHost::selectPage(const QString &sectionId, const QString &pageId)
{
    if (sectionId == m_currentSectionId && pageId == m_currentPageId) {
        return;
    }
    openPage(sectionId, pageId);
}

void NoteHost::openPage(const QString &sectionId, const QString &pageId)
{
    Notebook *notebook = m_document->notebook();
    NoteSection *section = notebook->sectionById(sectionId);
    NotePage *page = notebook->pageById(sectionId, pageId);
    if (section == nullptr || page == nullptr) {
        return;
    }
    // The outgoing page's debounced write must land before the ids move
    // on, or its last 500 ms of edits never reach disk.
    flushPersist();
    // AGENT-GUARD: read from disk ONLY for a page that has not been loaded
    // yet. Re-reading a loaded page threw away edits that had not reached
    // disk, and an untitled notebook has no disk copy at all.
    if (!page->loaded) {
        QString error;
        if (!notebook->loadPage(*section, *page, &error)) {
            showStatusMessage(tr("Page load failed: %1").arg(error), 8000);
            return;
        }
    }
    m_currentSectionId = sectionId;
    m_currentPageId = pageId;
    m_navigator->setCurrent(sectionId, pageId);
    if (m_pageItem != nullptr) {
        m_pageItem->setPage(page);
    }
    emit currentPageChanged();
    updatePageStatus();
    syncStructureEnablement();
    syncInkChecks();
    syncFormatActions();
    syncTagActions();
    syncPageActions();
}

NotePage *NoteHost::currentPage() const
{
    return m_document->notebook()->pageById(m_currentSectionId, m_currentPageId);
}

NoteSection *NoteHost::currentSection() const
{
    return m_document->notebook()->sectionById(m_currentSectionId);
}

QString NoteHost::currentPageTitle() const
{
    const NotePage *page = currentPage();
    return page != nullptr ? page->title : QString();
}

void NoteHost::persistCurrentPage()
{
    if (m_currentSectionId.isEmpty() || m_currentPageId.isEmpty()) {
        return;
    }
    Notebook *notebook = m_document->notebook();
    NoteSection *section = notebook->sectionById(m_currentSectionId);
    NotePage *page = notebook->pageById(m_currentSectionId, m_currentPageId);
    if (section == nullptr || page == nullptr || !notebook->isOpen()) {
        return; // untitled: in memory, covered by the recovery journal
    }
    QString error;
    if (!notebook->persistPage(*section, *page, &error)) {
        showStatusMessage(tr("Save failed: %1").arg(error), 8000);
    }
}

void NoteHost::flushPersist()
{
    if (m_persistTimer != nullptr && m_persistTimer->isActive()) {
        m_persistTimer->stop();
        persistCurrentPage();
    }
}

void NoteHost::updatePageStatus()
{
    const NoteSection *section = currentSection();
    if (section == nullptr) {
        setStatusInfo(tr("No page"));
        return;
    }
    int index = 0;
    for (int i = 0; i < section->pages.size(); ++i) {
        if (section->pages.at(i).id == m_currentPageId) {
            index = i + 1;
        }
    }
    QString tool;
    if (m_pageItem != nullptr) {
        switch (m_pageItem->tool()) {
        case NoteTool::Pen:
            tool = tr("Pen %1").arg(m_pageItem->penWidth());
            break;
        case NoteTool::Highlighter:
            tool = tr("Highlighter %1").arg(m_pageItem->highlighterWidth());
            break;
        case NoteTool::Eraser:
            tool = tr("Eraser %1").arg(m_pageItem->eraserRadius());
            break;
        case NoteTool::Text:
            tool = tr("Text");
            break;
        }
    }
    setStatusInfo(tr("%1 — page %2 of %3 · %4")
                      .arg(section->name)
                      .arg(index)
                      .arg(section->pages.size())
                      .arg(tool));
}

QString NoteHost::filterString() const
{
    return tr("QindaNote notebooks (notebook.json);;All files (*)");
}

void NoteHost::applyPreferences()
{
    if (m_pageItem == nullptr) {
        return;
    }
    const Quick::Preferences &prefs = Quick::Preferences::instance();
    m_pageItem->setPressureEnabled(prefs.notePressureEnabled());
    m_pageItem->setTouchDrawing(prefs.noteTouchDrawing());
    actions()->setChecked(QStringLiteral("noteTouchDrawAction"), prefs.noteTouchDrawing());
    const QString smoothing = prefs.noteSmoothing();
    m_pageItem->setStrokeSmoothing(smoothing == QLatin1String("off") ? 0.0
                                   : smoothing == QLatin1String("strong") ? 0.5
                                                                          : 0.25);
}

// ---- find / replace ---------------------------------------------------------

} // namespace QindaOffice::Apps::Notes
