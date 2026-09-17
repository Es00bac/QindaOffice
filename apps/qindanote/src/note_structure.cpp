// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_structure.h"

#include "note_page_view.h"
#include "note_window.h"

#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>

namespace QindaOffice::Apps::Notes {

namespace {

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    // Stable objectName: the dbusmenu activation identity (AGENTS.md rule 5).
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

NoteStructureActions::NoteStructureActions(NoteWindow *window)
    : QObject(window)
    , m_window(window)
{
    createActions();
    syncFromSelection();
}

void NoteStructureActions::createActions()
{
    m_newPage = make(this, "notePageNew", tr("&New Page"),
                     QKeySequence(QStringLiteral("Ctrl+Shift+N")), "list-add");
    m_newSection = make(this, "noteSectionNew", tr("New &Section"), {}, "folder-new");
    m_renamePage = make(this, "notePageRename", tr("&Rename Page…"), QKeySequence(Qt::Key_F2),
                        "edit-rename");
    m_renameSection = make(this, "noteSectionRename", tr("Rename Se&ction…"));
    m_deletePage = make(this, "notePageDelete", tr("&Delete Page"), {}, "edit-delete");
    m_deleteSection = make(this, "noteSectionDelete", tr("Delete Sec&tion"));
    m_movePageUp = make(this, "notePageMoveUp", tr("Move Page &Up"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+Up")), "go-up");
    m_movePageDown = make(this, "notePageMoveDown", tr("Move Page D&own"),
                          QKeySequence(QStringLiteral("Ctrl+Shift+Down")), "go-down");
    m_background = make(this, "notePageBackground", tr("&Background…"));

    connect(m_newPage, &QAction::triggered, this, &NoteStructureActions::newPage);
    connect(m_newSection, &QAction::triggered, this, &NoteStructureActions::newSection);
    connect(m_renamePage, &QAction::triggered, this, [this] { renameCurrent(false); });
    connect(m_renameSection, &QAction::triggered, this, [this] { renameCurrent(true); });
    connect(m_deletePage, &QAction::triggered, this, &NoteStructureActions::deleteCurrentPage);
    connect(m_deleteSection, &QAction::triggered, this,
            &NoteStructureActions::deleteCurrentSection);
    connect(m_movePageUp, &QAction::triggered, this, [this] { movePage(-1); });
    connect(m_movePageDown, &QAction::triggered, this, [this] { movePage(1); });
    connect(m_background, &QAction::triggered, this, [this] {
        NotePage *page = m_window->currentPage();
        if (page == nullptr) {
            return;
        }
        const QStringList choices = {QStringLiteral("blank"), QStringLiteral("ruled"),
                                     QStringLiteral("grid"), QStringLiteral("dotted")};
        bool ok = false;
        const QString choice =
            QInputDialog::getItem(m_window, tr("Background"), tr("Pattern:"), choices,
                                  qMax(0, choices.indexOf(page->envelope.background)), false, &ok);
        if (ok && !choice.isEmpty()) {
            m_window->pageView()->setBackground(choice);
        }
    });
}

void NoteStructureActions::syncFromSelection()
{
    Notebook *notebook = m_window->noteDocument()->notebook();
    NoteSection *section = notebook->sectionById(m_window->currentSectionId());
    const bool havePage = m_window->currentPage() != nullptr;
    m_renamePage->setEnabled(havePage);
    m_deletePage->setEnabled(havePage);
    m_background->setEnabled(havePage);
    m_renameSection->setEnabled(section != nullptr);
    // The last section cannot go: a notebook with no section has nowhere to
    // put the next page.
    m_deleteSection->setEnabled(section != nullptr && notebook->sections().size() > 1);
    int index = -1;
    int count = 0;
    if (section != nullptr) {
        count = section->pages.size();
        for (int i = 0; i < count; ++i) {
            if (section->pages.at(i).id == m_window->currentPageId()) {
                index = i;
            }
        }
    }
    m_movePageUp->setEnabled(index > 0);
    m_movePageDown->setEnabled(index >= 0 && index + 1 < count);
}

void NoteStructureActions::newPage()
{
    Notebook *notebook = m_window->noteDocument()->notebook();
    NoteSection *section = notebook->sectionById(m_window->currentSectionId());
    if (section == nullptr) {
        section = notebook->sections().isEmpty()
            ? &notebook->addSection(tr("Notes"))
            : notebook->sectionById(notebook->sections().first().id);
    }
    if (section == nullptr) {
        return;
    }
    const NotePage &created = notebook->addPage(*section, tr("Page %1").arg(section->pages.size() + 1));
    const QString sectionId = section->id;
    const QString pageId = created.id;
    m_window->noteDocument()->setDirty(true);
    m_window->refreshNavigator();
    m_window->selectPageById(sectionId, pageId);
}

void NoteStructureActions::newSection()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(m_window, tr("New Section"), tr("Section name:"),
                              QLineEdit::Normal, tr("Section"), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    Notebook *notebook = m_window->noteDocument()->notebook();
    NoteSection &section = notebook->addSection(name);
    // A section with no page cannot be written into, so give it its first.
    const NotePage &page = notebook->addPage(section, tr("Page 1"));
    const QString sectionId = section.id;
    const QString pageId = page.id;
    m_window->noteDocument()->setDirty(true);
    m_window->refreshNavigator();
    m_window->selectPageById(sectionId, pageId);
}

void NoteStructureActions::renameCurrent(bool section)
{
    Notebook *notebook = m_window->noteDocument()->notebook();
    const QString sectionId = m_window->currentSectionId();
    const QString pageId = m_window->currentPageId();
    NoteSection *target = notebook->sectionById(sectionId);
    NotePage *page = m_window->currentPage();
    if (section ? target == nullptr : page == nullptr) {
        return;
    }
    const QString current = section ? target->name : page->title;
    bool ok = false;
    const QString name =
        QInputDialog::getText(m_window, section ? tr("Rename Section") : tr("Rename Page"),
                              tr("Name:"), QLineEdit::Normal, current, &ok)
            .trimmed();
    if (!ok || name.isEmpty() || name == current) {
        return;
    }
    const bool changed = section ? notebook->renameSection(sectionId, name)
                                 : notebook->renamePage(sectionId, pageId, name);
    if (!changed) {
        return;
    }
    m_window->noteDocument()->setDirty(true);
    m_window->refreshNavigator();
    m_window->selectPageById(sectionId, pageId);
}

void NoteStructureActions::deleteCurrentPage()
{
    NotePage *page = m_window->currentPage();
    if (page == nullptr) {
        return;
    }
    const QString title = page->title;
    // Not undoable: the notebook has no structural history, so ask.
    if (QMessageBox::warning(m_window, tr("Delete page"),
                             tr("Delete “%1” and its ink? This cannot be undone.").arg(title),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    const QString sectionId = m_window->currentSectionId();
    m_window->clearCurrentPage();
    m_window->noteDocument()->notebook()->removePage(sectionId, m_window->currentPageId());
    m_window->noteDocument()->setDirty(true);
    m_window->refreshNavigator();
}

void NoteStructureActions::deleteCurrentSection()
{
    Notebook *notebook = m_window->noteDocument()->notebook();
    NoteSection *section = notebook->sectionById(m_window->currentSectionId());
    if (section == nullptr || notebook->sections().size() <= 1) {
        return;
    }
    const QString name = section->name;
    const int pages = section->pages.size();
    if (QMessageBox::warning(
            m_window, tr("Delete section"),
            tr("Delete “%1” and its %n page(s)? This cannot be undone.", nullptr, pages).arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    const QString sectionId = section->id;
    m_window->clearCurrentPage();
    notebook->removeSection(sectionId);
    m_window->noteDocument()->setDirty(true);
    m_window->refreshNavigator();
}

void NoteStructureActions::movePage(int delta)
{
    const QString sectionId = m_window->currentSectionId();
    const QString pageId = m_window->currentPageId();
    if (!m_window->noteDocument()->notebook()->movePage(sectionId, pageId, delta)) {
        return;
    }
    m_window->noteDocument()->setDirty(true);
    m_window->refreshNavigator();
    m_window->selectPageById(sectionId, pageId);
}

QList<QAction *> NoteStructureActions::stripActions() const
{
    return {m_newPage, m_newSection};
}

void NoteStructureActions::populatePageMenu(QMenu *menu)
{
    menu->addAction(m_newPage);
    menu->addAction(m_newSection);
    menu->addSeparator();
    menu->addAction(m_renamePage);
    menu->addAction(m_renameSection);
    menu->addSeparator();
    menu->addAction(m_movePageUp);
    menu->addAction(m_movePageDown);
    menu->addSeparator();
    menu->addAction(m_background);
    menu->addSeparator();
    menu->addAction(m_deletePage);
    menu->addAction(m_deleteSection);
}

void NoteStructureActions::showNavigatorMenu(const QPoint &globalPos)
{
    syncFromSelection();
    QMenu menu(m_window);
    menu.setObjectName(QStringLiteral("noteNavigatorMenu"));
    populatePageMenu(&menu);
    menu.exec(globalPos);
}

} // namespace QindaOffice::Apps::Notes
