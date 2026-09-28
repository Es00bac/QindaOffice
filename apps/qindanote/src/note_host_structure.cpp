// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's Page menu: new page and section, duplicate, next/previous,
// rename, delete, reorder, background and the block commands (D-017 kept:
// deletions are not undoable, so each asks first through a QML dialog).

#include "note_host.h"

#include "navigator_model.h"
#include "note_page_item.h"

#include <QKeySequence>
#include <QTimer>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;

void NoteHost::createStructureActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const QString &icon = {}) {
        return model->add(QString::fromLatin1(id), text, shortcut, icon);
    };
    connect(make("notePageNew", tr("&New Page"), QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                 QStringLiteral("file-plus")),
            &ActionEntry::triggered, this, [this] { newPage(); });
    connect(make("noteSectionNew", tr("New &Section…"), {}, QStringLiteral("folder-plus")),
            &ActionEntry::triggered, this, [this] { emit newSectionRequested(); });
    connect(make("notePageDuplicate", tr("D&uplicate Page"),
                 QKeySequence(QStringLiteral("Ctrl+Shift+D")), QStringLiteral("copy")),
            &ActionEntry::triggered, this, [this] { duplicatePage(); });
    connect(make("notePageNext", tr("Next Page"), QKeySequence(QStringLiteral("Ctrl+PgDown")),
                 QStringLiteral("chevron-down")),
            &ActionEntry::triggered, this, [this] { stepPage(1); });
    connect(make("notePagePrevious", tr("Previous Page"),
                 QKeySequence(QStringLiteral("Ctrl+PgUp")), QStringLiteral("chevron-up")),
            &ActionEntry::triggered, this, [this] { stepPage(-1); });
    connect(make("notePageRename", tr("&Rename Page…"), QKeySequence(Qt::Key_F2),
                 QStringLiteral("pencil")),
            &ActionEntry::triggered, this, [this] {
                if (const NotePage *page = currentPage()) {
                    emit renamePageRequested(m_currentSectionId, m_currentPageId, page->title);
                }
            });
    connect(make("noteSectionRename", tr("Rename Se&ction…")), &ActionEntry::triggered, this,
            [this] {
                if (const NoteSection *section = currentSection()) {
                    emit renameSectionRequested(m_currentSectionId, section->name);
                }
            });
    connect(make("notePageMoveUp", tr("Move Page &Up"),
                 QKeySequence(QStringLiteral("Ctrl+Shift+Up")), QStringLiteral("arrow-up")),
            &ActionEntry::triggered, this, [this] { movePage(-1); });
    connect(make("notePageMoveDown", tr("Move Page D&own"),
                 QKeySequence(QStringLiteral("Ctrl+Shift+Down")), QStringLiteral("arrow-down")),
            &ActionEntry::triggered, this, [this] { movePage(1); });
    for (const auto &[id, text] : QList<std::pair<QString, QString>>{
             {QStringLiteral("blank"), tr("&Blank")},
             {QStringLiteral("ruled"), tr("&Ruled")},
             {QStringLiteral("grid"), tr("&Grid")},
             {QStringLiteral("dotted"), tr("&Dotted")}}) {
        ActionEntry *entry = model->add(QStringLiteral("notePageBackground_") + id, text);
        entry->setRadioGroup(QStringLiteral("notePageBackground"));
        connect(entry, &ActionEntry::triggered, this, [this, id] {
            if (!m_applyingInk) {
                setBackground(id);
            }
        });
    }
    connect(make("noteAddTextAction", tr("Add &Text"), {}, QStringLiteral("type")),
            &ActionEntry::triggered, this, [this] {
                if (m_pageItem != nullptr) {
                    m_pageItem->addTextBlock();
                }
            });
    connect(make("noteAddCheckAction", tr("Add Chec&klist Item"), {}, QStringLiteral("square-check")),
            &ActionEntry::triggered, this, [this] {
                if (m_pageItem != nullptr) {
                    m_pageItem->addCheckItem();
                }
            });
    connect(make("noteAddImageAction", tr("Add &Image…"), {}, QStringLiteral("image")),
            &ActionEntry::triggered, this, [this] { emit addImageRequested(); });
    connect(make("noteDeleteBlockAction", tr("Delete Bloc&k"), {}, QStringLiteral("trash-2")),
            &ActionEntry::triggered, this, [this] {
                if (m_pageItem != nullptr && m_pageItem->editingBlock() >= 0) {
                    m_pageItem->deleteBlock(m_pageItem->editingBlock());
                }
            });
    connect(make("notePageDelete", tr("&Delete Page…"), {}, QStringLiteral("trash-2")),
            &ActionEntry::triggered, this, [this] {
                if (const NotePage *page = currentPage()) {
                    emit deletePageRequested(page->title);
                }
            });
    connect(make("noteSectionDelete", tr("Delete Sec&tion…")), &ActionEntry::triggered, this,
            [this] {
                if (const NoteSection *section = currentSection()) {
                    emit deleteSectionRequested(section->name, int(section->pages.size()));
                }
            });
}

void NoteHost::syncStructureEnablement()
{
    Notebook *notebook = m_document->notebook();
    NoteSection *section = currentSection();
    const bool havePage = currentPage() != nullptr;
    Quick::ActionModel *model = actions();
    model->setEnabled(QStringLiteral("notePageRename"), havePage);
    model->setEnabled(QStringLiteral("notePageDelete"), havePage);
    model->setEnabled(QStringLiteral("notePageDuplicate"), havePage);
    model->setEnabled(QStringLiteral("noteSectionRename"), section != nullptr);
    // The last section cannot go: a notebook with no section has nowhere to
    // put the next page.
    model->setEnabled(QStringLiteral("noteSectionDelete"),
                      section != nullptr && notebook->sections().size() > 1);
    int index = -1;
    int count = 0;
    int total = 0;
    for (const NoteSection &s : notebook->sections()) {
        total += int(s.pages.size());
    }
    if (section != nullptr) {
        count = int(section->pages.size());
        for (int i = 0; i < count; ++i) {
            if (section->pages.at(i).id == m_currentPageId) {
                index = i;
            }
        }
    }
    model->setEnabled(QStringLiteral("notePageMoveUp"), index > 0);
    model->setEnabled(QStringLiteral("notePageMoveDown"), index >= 0 && index + 1 < count);
    model->setEnabled(QStringLiteral("notePageNext"), total > 1);
    model->setEnabled(QStringLiteral("notePagePrevious"), total > 1);
    const bool editing = m_pageItem != nullptr && m_pageItem->editingBlock() >= 0;
    model->setEnabled(QStringLiteral("noteDeleteBlockAction"), editing);
    for (const QString &id : backgrounds()) {
        model->setEnabled(QStringLiteral("notePageBackground_") + id, havePage);
    }
    const NotePage *page = currentPage();
    if (page != nullptr) {
        m_applyingInk = true;
        model->setChecked(QStringLiteral("notePageBackground_") + page->envelope.background, true);
        m_applyingInk = false;
    }
}

QStringList NoteHost::backgrounds() const
{
    return {QStringLiteral("blank"), QStringLiteral("ruled"), QStringLiteral("grid"),
            QStringLiteral("dotted")};
}

void NoteHost::setBackground(const QString &background)
{
    NotePage *page = currentPage();
    if (page == nullptr) {
        return;
    }
    if (m_pageItem != nullptr) {
        // Through the item: one undo step, a repaint, pageEdited.
        m_pageItem->setBackground(background);
    } else if (page->envelope.background != background) {
        page->envelope.background = background;
        m_document->setDirty(true);
        m_persistTimer->start();
    }
    syncStructureEnablement();
}

void NoteHost::newPage()
{
    Notebook *notebook = m_document->notebook();
    NoteSection *section = currentSection();
    if (section == nullptr) {
        section = notebook->sections().isEmpty()
            ? &notebook->addSection(tr("Notes"))
            : notebook->sectionById(notebook->sections().first().id);
    }
    if (section == nullptr) {
        return;
    }
    // The new page inherits the background you were writing on.
    const QString background = currentPage() != nullptr ? currentPage()->envelope.background
                                                        : QStringLiteral("blank");
    NotePage &created = notebook->addPage(*section, tr("Page %1").arg(section->pages.size() + 1));
    created.envelope.background = background;
    const QString sectionId = section->id;
    const QString pageId = created.id;
    m_document->setDirty(true);
    m_navigator->rebuild();
    openPage(sectionId, pageId);
}

void NoteHost::createSection(const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        return;
    }
    Notebook *notebook = m_document->notebook();
    NoteSection &section = notebook->addSection(name);
    // A section with no page cannot be written into, so give it its first.
    const NotePage &page = notebook->addPage(section, tr("Page 1"));
    const QString sectionId = section.id;
    const QString pageId = page.id;
    m_document->setDirty(true);
    m_navigator->rebuild();
    openPage(sectionId, pageId);
}

void NoteHost::renamePage(const QString &sectionId, const QString &pageId, const QString &title)
{
    const QString name = title.trimmed();
    if (name.isEmpty() || !m_document->notebook()->renamePage(sectionId, pageId, name)) {
        return;
    }
    m_document->setDirty(true);
    m_navigator->rebuild();
    m_navigator->setCurrent(m_currentSectionId, m_currentPageId);
    updatePageStatus();
    emit currentPageChanged();
}

void NoteHost::renameSection(const QString &sectionId, const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty() || !m_document->notebook()->renameSection(sectionId, name)) {
        return;
    }
    m_document->setDirty(true);
    m_navigator->rebuild();
    m_navigator->setCurrent(m_currentSectionId, m_currentPageId);
    updatePageStatus();
}

void NoteHost::deletePageConfirmed()
{
    if (currentPage() == nullptr) {
        return;
    }
    const QString sectionId = m_currentSectionId;
    const QString pageId = m_currentPageId;
    // AGENT-GUARD: the item holds a NotePage* into the notebook's vector.
    // Deleting the page while it still points there is a use-after-free.
    if (m_pageItem != nullptr) {
        m_pageItem->setPage(nullptr);
    }
    m_currentPageId.clear();
    m_document->notebook()->removePage(sectionId, pageId);
    m_document->setDirty(true);
    rebuildNavigator();
}

void NoteHost::deleteSectionConfirmed()
{
    Notebook *notebook = m_document->notebook();
    if (currentSection() == nullptr || notebook->sections().size() <= 1) {
        return;
    }
    const QString sectionId = m_currentSectionId;
    if (m_pageItem != nullptr) {
        m_pageItem->setPage(nullptr);
    }
    m_currentPageId.clear();
    m_currentSectionId.clear();
    notebook->removeSection(sectionId);
    m_document->setDirty(true);
    rebuildNavigator();
}

void NoteHost::movePage(int delta)
{
    const QString sectionId = m_currentSectionId;
    const QString pageId = m_currentPageId;
    if (!m_document->notebook()->movePage(sectionId, pageId, delta)) {
        return;
    }
    m_document->setDirty(true);
    m_navigator->rebuild();
    m_navigator->setCurrent(sectionId, pageId);
    // The move swapped QVector elements: the item's pointer now addressed
    // the NEIGHBOUR, and the next edit landed on the wrong page.
    if (m_pageItem != nullptr) {
        m_pageItem->rebindPage(currentPage());
    }
    syncStructureEnablement();
    updatePageStatus();
}

void NoteHost::stepPage(int delta)
{
    // Every page of the notebook in order; wraps across sections.
    struct Ref {
        QString sectionId;
        QString pageId;
    };
    QList<Ref> refs;
    int current = -1;
    for (const NoteSection &section : m_document->notebook()->sections()) {
        for (const NotePage &page : section.pages) {
            if (page.id == m_currentPageId) {
                current = int(refs.size());
            }
            refs.append({section.id, page.id});
        }
    }
    if (refs.size() < 2) {
        return;
    }
    const int next = ((current < 0 ? 0 : current) + delta + int(refs.size())) % int(refs.size());
    openPage(refs.at(next).sectionId, refs.at(next).pageId);
}

void NoteHost::duplicatePage()
{
    Notebook *notebook = m_document->notebook();
    NoteSection *section = currentSection();
    const NotePage *source = currentPage();
    if (section == nullptr || source == nullptr) {
        return;
    }
    if (m_pageItem != nullptr) {
        m_pageItem->commitEdit();
    }
    const QString sectionId = section->id;
    const QString sourceId = source->id;
    const QString title = tr("%1 (copy)").arg(source->title);
    const Ink::PageEnvelope envelope = source->envelope;
    NotePage &copy = notebook->addPage(*section, title);
    copy.envelope = envelope;
    copy.envelope.id = copy.id;
    copy.envelope.title = title;
    copy.loaded = true;
    const QString copyId = copy.id;
    // Right after the original: addPage appended it at the end.
    section = notebook->sectionById(sectionId);
    int sourceIndex = 0;
    for (int i = 0; i < section->pages.size(); ++i) {
        if (section->pages.at(i).id == sourceId) {
            sourceIndex = i;
        }
    }
    for (int i = int(section->pages.size()) - 1; i > sourceIndex + 1; --i) {
        static_cast<void>(notebook->movePage(sectionId, copyId, -1));
    }
    m_document->setDirty(true);
    m_navigator->rebuild();
    openPage(sectionId, copyId);
}

void NoteHost::addImageFile(const QUrl &url)
{
    if (m_pageItem == nullptr) {
        return;
    }
    if (!m_pageItem->addImageFile(url)) {
        showStatusMessage(tr("Could not read %1").arg(url.toLocalFile()), 8000);
    }
}

} // namespace QindaOffice::Apps::Notes
