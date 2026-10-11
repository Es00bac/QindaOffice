// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's M84 page commands (D-093): Search Notebooks (Ctrl+E), File ▸
// Import, page templates, page colour, subpages (OneNote's Make Subpage /
// Promote Subpage), Link to File, and the navigation every result and tag
// summary row uses.
//
// AGENT-CONTRACT (AGENTS.md rule 5): noteSearchAction, noteImportAction,
// notePageTemplate_<id> (note_templates.h ids), notePageColor_<id>,
// notePageColorAction, noteMakeSubpageAction, notePromoteSubpageAction and
// noteLinkFileAction are stable; never rename or reuse one.

#include "note_host.h"

#include "navigator_model.h"
#include "note_import.h"
#include "note_page_item.h"
#include "note_search.h"
#include "note_templates.h"

#include <QDate>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;

namespace {

struct PaperColor {
    const char *id;
    const char *text;
    QRgb rgb; // 0: white (no colour)
};

// Paper tints (D-004: the page is paper, so these are paper colours).
const PaperColor kPaperColors[] = {
    {"none", QT_TRANSLATE_NOOP("NoteHost", "&No Colour"), 0},
    {"yellow", QT_TRANSLATE_NOOP("NoteHost", "&Yellow"), 0xfff8d6},
    {"green", QT_TRANSLATE_NOOP("NoteHost", "&Green"), 0xe6f4e0},
    {"blue", QT_TRANSLATE_NOOP("NoteHost", "&Blue"), 0xe3eefa},
    {"pink", QT_TRANSLATE_NOOP("NoteHost", "&Pink"), 0xfbe4ec},
    {"purple", QT_TRANSLATE_NOOP("NoteHost", "P&urple"), 0xeee6f7},
    {"orange", QT_TRANSLATE_NOOP("NoteHost", "&Orange"), 0xfdebd9},
    {"grey", QT_TRANSLATE_NOOP("NoteHost", "G&rey"), 0xeeeeee},
};

QColor colorOf(const PaperColor &paper)
{
    return paper.rgb == 0 ? QColor() : QColor(QRgb(paper.rgb));
}

} // namespace

void NoteHost::createPageActions()
{
    Quick::ActionModel *model = actions();
    connect(model->add(QStringLiteral("notePageVersionsAction"), tr("Page &Versions…"), {}, QStringLiteral("history")),
            &ActionEntry::triggered, this, &NoteHost::pageVersionsRequested);
    connect(model->add(QStringLiteral("noteSearchAction"), tr("Searc&h Notebooks…"),
                       QKeySequence(QStringLiteral("Ctrl+E")), QStringLiteral("search")),
            &ActionEntry::triggered, this, [this] { emit searchRequested(); });
    connect(model->add(QStringLiteral("noteImportAction"), tr("&Import…"), {}, QStringLiteral("import")),
            &ActionEntry::triggered, this, [this] { emit importRequested(); });
    connect(model->add(QStringLiteral("noteLinkFileAction"), tr("Link to &File…"), {}, QStringLiteral("paperclip")),
            &ActionEntry::triggered, this, [this] { emit fileLinkRequested(); });
    for (const QString &templateId : Templates::ids()) {
        connect(model->add(QStringLiteral("notePageTemplate_") + templateId, Templates::label(templateId), {},
                           Templates::iconName(templateId)),
                &ActionEntry::triggered, this, [this, templateId] { newPageFromTemplate(templateId); });
    }
    for (const PaperColor &paper : kPaperColors) {
        ActionEntry *entry = model->add(QStringLiteral("notePageColor_") + QString::fromLatin1(paper.id), tr(paper.text));
        entry->setRadioGroup(QStringLiteral("notePageColor"));
        connect(entry, &ActionEntry::triggered, this, [this, color = colorOf(paper)] {
            if (!m_applyingInk) {
                applyPageColor(color);
            }
        });
    }
    connect(model->add(QStringLiteral("notePageColorAction"), tr("&More Colours…"), {}, QStringLiteral("paint-bucket")),
            &ActionEntry::triggered, this, [this] {
                const NotePage *page = currentPage();
                emit formatColorRequested(QStringLiteral("page"),
                                          page != nullptr ? pageColor(page->envelope.blocksJson) : QColor(Qt::white));
            });
    connect(model->add(QStringLiteral("noteMakeSubpageAction"), tr("Make &Subpage"),
                       QKeySequence(QStringLiteral("Ctrl+Alt+]")), QStringLiteral("list-indent-increase")),
            &ActionEntry::triggered, this, [this] { changePageLevel(1); });
    connect(model->add(QStringLiteral("notePromoteSubpageAction"), tr("&Promote Subpage"),
                       QKeySequence(QStringLiteral("Ctrl+Alt+[")), QStringLiteral("list-indent-decrease")),
            &ActionEntry::triggered, this, [this] { changePageLevel(-1); });
}

void NoteHost::syncPageActions()
{
    Quick::ActionModel *model = actions();
    const NotePage *page = currentPage();
    model->setEnabled(QStringLiteral("notePageVersionsAction"), page != nullptr);
    const NoteSection *section = currentSection();
    int index = -1;
    if (section != nullptr) {
        for (int i = 0; i < section->pages.size(); ++i) {
            if (section->pages.at(i).id == m_currentPageId) {
                index = i;
            }
        }
    }
    const int level = page != nullptr ? page->level : 0;
    const int above = index > 0 ? section->pages.at(index - 1).level : -1;
    model->setEnabled(QStringLiteral("noteMakeSubpageAction"), page != nullptr && level < 2 && level <= above);
    model->setEnabled(QStringLiteral("notePromoteSubpageAction"), page != nullptr && level > 0);
    m_applyingInk = true;
    const QColor color = page != nullptr ? pageColor(page->envelope.blocksJson) : QColor();
    bool matched = false;
    for (const PaperColor &paper : kPaperColors) {
        const QString id = QStringLiteral("notePageColor_") + QString::fromLatin1(paper.id);
        model->setEnabled(id, page != nullptr);
        const bool on = colorOf(paper) == color;
        model->setChecked(id, on);
        matched = matched || on;
    }
    if (!matched) {
        model->setChecked(QStringLiteral("notePageColor_none"), false);
    }
    m_applyingInk = false;
}

QStringList NoteHost::pageColorIds() const
{
    QStringList ids;
    for (const PaperColor &paper : kPaperColors) {
        ids.append(QString::fromLatin1(paper.id));
    }
    return ids;
}

void NoteHost::applyPageColor(const QColor &color)
{
    NotePage *page = currentPage();
    if (page == nullptr) {
        return;
    }
    if (m_pageItem != nullptr && m_pageItem->page() == page) {
        m_pageItem->setPageColor(color); // one undo step
    } else {
        page->envelope.blocksJson = withPageColor(page->envelope.blocksJson, color);
        m_document->setDirty(true);
        m_persistTimer->start();
    }
    syncPageActions();
}

QString NoteHost::addPageAfterCurrent(const QString &title, const QJsonArray &blocks, const QString &background,
                                      const QString &after)
{
    const QString anchor = after.isEmpty() ? m_currentPageId : after;
    Notebook *notebook = m_document->notebook();
    NoteSection *section = currentSection();
    if (section == nullptr) {
        return {};
    }
    const QString sectionId = section->id;
    NotePage &created = notebook->addPage(*section, title);
    created.envelope.background = background;
    created.envelope.blocksJson = withBlocksArray(QByteArray("{}"), blocks);
    const QString pageId = created.id;
    section = notebook->sectionById(sectionId);
    qsizetype at = section->pages.size() - 2;
    for (qsizetype i = 0; i < section->pages.size(); ++i) {
        if (section->pages.at(i).id == anchor) {
            at = i;
        }
    }
    for (qsizetype i = section->pages.size() - 1; i > at + 1; --i) {
        static_cast<void>(notebook->movePage(sectionId, pageId, -1));
    }
    if (NotePage *page = notebook->pageById(sectionId, pageId); page != nullptr && notebook->isOpen()) {
        static_cast<void>(notebook->persistPage(*notebook->sectionById(sectionId), *page, nullptr));
    }
    m_document->setDirty(true);
    m_navigator->rebuild();
    if (m_pageItem != nullptr) {
        m_pageItem->rebindPage(currentPage()); // see NotePageItem::rebindPage's AGENT-GUARD
    }
    return pageId;
}

void NoteHost::newPageFromTemplate(const QString &templateId)
{
    if (currentSection() == nullptr) {
        return;
    }
    const Templates::PageTemplate made = Templates::make(templateId, QDate::currentDate());
    const QString title =
        made.title.isEmpty() ? tr("Page %1").arg(currentSection()->pages.size() + 1) : made.title;
    const QString pageId = addPageAfterCurrent(title, made.blocks, made.background);
    if (!pageId.isEmpty()) {
        openPage(m_currentSectionId, pageId);
    }
}

void NoteHost::changePageLevel(int delta)
{
    NoteSection *section = currentSection();
    if (section == nullptr) {
        return;
    }
    for (int i = 0; i < section->pages.size(); ++i) {
        NotePage &page = section->pages[i];
        if (page.id != m_currentPageId) {
            continue;
        }
        // A subpage hangs under the page above it, at most two levels deep.
        const int above = i > 0 ? section->pages.at(i - 1).level : -1;
        const int level = qBound(0, page.level + delta, qMin(2, above + 1));
        if (level == page.level) {
            return;
        }
        page.level = level;
        m_document->setDirty(true);
        m_navigator->rebuild();
        m_navigator->setCurrent(m_currentSectionId, m_currentPageId);
        syncPageActions();
        return;
    }
}

QStringList NoteHost::importNameFilters() const
{
    return Import::nameFilters();
}

bool NoteHost::importFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty() || currentSection() == nullptr) {
        return false;
    }
    if (Import::kindOf(path) == Import::Kind::OneNote) {
        emit messageRequested(tr("OneNote files"), Import::oneNoteExplanation(), QStringLiteral("info"));
        return false;
    }
    QList<Import::ImportedPage> pages;
    QString error;
    if (!Import::importFile(path, &pages, &error) || pages.isEmpty()) {
        emit messageRequested(tr("Import"), tr("%1 could not be imported: %2").arg(QFileInfo(path).fileName(), error),
                              QStringLiteral("warning"));
        return false;
    }
    if (m_pageItem != nullptr) {
        m_pageItem->commitEdit();
    }
    flushPersist();
    // Each page after the one before it, so the document keeps its order.
    QString first;
    QString previous = m_currentPageId;
    for (const Import::ImportedPage &imported : std::as_const(pages)) {
        previous = addPageAfterCurrent(imported.title, imported.blocks, QStringLiteral("blank"), previous);
        if (first.isEmpty()) {
            first = previous;
        }
    }
    openPage(m_currentSectionId, first);
    showStatusMessage(tr("Imported %n page(s) from %1", "", int(pages.size())).arg(QFileInfo(path).fileName()), 6000);
    return true;
}

void NoteHost::navigateTo(const QString &sectionId, const QString &pageId, int block, const QString &paragraph)
{
    if (sectionId != m_currentSectionId || pageId != m_currentPageId) {
        openPage(sectionId, pageId);
    }
    if (m_pageItem != nullptr && block >= 0 && m_currentPageId == pageId) {
        m_pageItem->revealBlock(block, paragraph);
    }
}

void NoteHost::applyPendingNavigation()
{
    const std::optional<NoteSearch::Navigation> target =
        NoteSearch::takeNavigation(m_document->notebook()->directory());
    if (target) {
        navigateTo(target->sectionId, target->pageId, target->block);
    }
}

void NoteHost::openLink(const QString &href)
{
    const QUrl url(href);
    if (!url.isValid() || href.trimmed().isEmpty()) {
        return;
    }
    // AGENT-GUARD: QQO_NO_HANDOFF (the tests) never launches anything.
    if (qEnvironmentVariableIsSet("QQO_NO_HANDOFF")) {
        showStatusMessage(tr("Link: %1").arg(href), 4000);
        return;
    }
    // Linked notes: a Word document opens in QindaWrite, the rest in the
    // desktop's handler for it.
    const QString file = url.isLocalFile() ? url.toLocalFile() : QString();
    const QString suffix = QFileInfo(file).suffix().toLower();
    const QString write = QStandardPaths::findExecutable(QStringLiteral("qindawrite"));
    if (!file.isEmpty() && !write.isEmpty()
        && (suffix == QLatin1String("docx") || suffix == QLatin1String("odt") || suffix == QLatin1String("rtf"))) {
        QProcess::startDetached(write, {file});
        return;
    }
    if (!QDesktopServices::openUrl(url)) {
        showStatusMessage(tr("Could not open %1").arg(href), 6000);
    }
}

} // namespace QindaOffice::Apps::Notes
