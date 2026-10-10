// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's tags (M84, D-093): OneNote's Home ▸ Tags — the thirteen
// built-in tags (Ctrl+1 … Ctrl+9 for the first nine),
// custom tags, Find Tags (the summary pane) and its summary page.
//
// AGENT-CONTRACT (AGENTS.md rule 5): noteTag_<id> for the built-in ids of
// note_tags.h, noteTagMoreAction, noteTagCustomizeAction,
// noteTagRemoveAction, noteFindTagsAction and noteTagSummaryPageAction are
// stable; never rename or reuse one. Custom tags have no command of their
// own (they come and go); they are applied through the Tags dialog.

#include "note_host.h"

#include "note_page_item.h"
#include "note_rich_import.h"
#include "note_tag_summary.h"
#include "note_tags.h"

#include <qindatk/rich_text_document.h>

#include <QHash>
#include <QJsonObject>
#include <QTimer>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;

namespace {

// OneNote's shortcuts for its first nine tags.
const QHash<QString, QString> &tagShortcuts()
{
    static const QHash<QString, QString> keys{
        {QStringLiteral("todo"), QStringLiteral("Ctrl+1")},      {QStringLiteral("important"), QStringLiteral("Ctrl+2")},
        {QStringLiteral("question"), QStringLiteral("Ctrl+3")},  {QStringLiteral("remember"), QStringLiteral("Ctrl+4")},
        {QStringLiteral("definition"), QStringLiteral("Ctrl+5")}, {QStringLiteral("highlight"), QStringLiteral("Ctrl+6")},
        {QStringLiteral("contact"), QStringLiteral("Ctrl+7")},   {QStringLiteral("address"), QStringLiteral("Ctrl+8")},
        {QStringLiteral("phone"), QStringLiteral("Ctrl+9")}};
    return keys;
}

QString actionIcon(const Tags::TagDef &def)
{
    if (def.todo) {
        return QStringLiteral("square-check");
    }
    if (def.id == QLatin1String("remember")) {
        return QStringLiteral("bookmark");
    }
    if (def.id == QLatin1String("definition")) {
        return QStringLiteral("book-open");
    }
    return def.icon;
}

} // namespace

void NoteHost::createTagActions()
{
    Quick::ActionModel *model = actions();
    for (const Tags::TagDef &def : Tags::builtIn()) {
        ActionEntry *entry = model->add(QStringLiteral("noteTag_") + def.id, def.name,
                                        QKeySequence(tagShortcuts().value(def.id)), actionIcon(def));
        connect(entry, &ActionEntry::triggered, this, [this, id = def.id] { applyTag(id); });
    }
    connect(model->add(QStringLiteral("noteTagMoreAction"), tr("Apply &Tag…"), {}, QStringLiteral("tag")),
            &ActionEntry::triggered, this, [this] { emit tagsDialogRequested(false); });
    connect(model->add(QStringLiteral("noteTagCustomizeAction"), tr("&Customize Tags…"), {},
                       QStringLiteral("settings-2")),
            &ActionEntry::triggered, this, [this] { emit tagsDialogRequested(true); });
    // AGENT-NOTE: OneNote's Ctrl+0 is the suite's Reset Zoom; no key here.
    connect(model->add(QStringLiteral("noteTagRemoveAction"), tr("&Remove Tag"), {}, QStringLiteral("circle-off")),
            &ActionEntry::triggered, this, [this] {
                if (m_pageItem != nullptr) {
                    m_pageItem->removeTags();
                }
            });
    ActionEntry *findTags = model->add(QStringLiteral("noteFindTagsAction"), tr("&Find Tags"), {},
                                       QStringLiteral("list-checks"));
    findTags->setCheckable(true);
    connect(findTags, &ActionEntry::toggled, this, [this](bool visible) {
        if (visible) {
            m_tagSummary->refresh();
        }
        emit findTagsToggleRequested(visible);
    });
    connect(model->add(QStringLiteral("noteTagSummaryPageAction"), tr("Create Tag &Summary Page"), {},
                       QStringLiteral("file-plus")),
            &ActionEntry::triggered, this, [this] { createTagSummaryPage(); });

    m_tagSummary = new NoteTagSummary(this);
    // Edits refresh an open Find Tags pane, at most every 400 ms.
    m_tagRefresh = new QTimer(this);
    m_tagRefresh->setSingleShot(true);
    m_tagRefresh->setInterval(400);
    connect(m_tagRefresh, &QTimer::timeout, this, [this] {
        if (actions()->entry(QStringLiteral("noteFindTagsAction"))->isChecked()) {
            m_tagSummary->refresh();
        }
    });
}

void NoteHost::syncTagActions()
{
    const bool havePage = currentPage() != nullptr;
    QStringList ids{QStringLiteral("noteTagMoreAction"), QStringLiteral("noteTagRemoveAction")};
    for (const Tags::TagDef &def : Tags::builtIn()) {
        ids.append(QStringLiteral("noteTag_") + def.id);
    }
    actions()->setEnabled(ids, havePage);
    actions()->setEnabled(QStringLiteral("noteTagRemoveAction"),
                          m_pageItem != nullptr && m_pageItem->editingBlock() >= 0);
}

QVariantList NoteHost::tagCatalog() const
{
    QVariantList out;
    const auto add = [&out](const Tags::TagDef &def, bool custom) {
        out.append(QVariantMap{{QStringLiteral("id"), def.id},
                               {QStringLiteral("name"), def.name},
                               {QStringLiteral("icon"), def.icon.isEmpty() ? actionIcon(def) : def.icon},
                               {QStringLiteral("color"), def.color},
                               {QStringLiteral("todo"), def.todo},
                               {QStringLiteral("highlight"), def.highlight},
                               {QStringLiteral("custom"), custom}});
    };
    for (const Tags::TagDef &def : Tags::builtIn()) {
        add(def, false);
    }
    for (const Tags::TagDef &def : Tags::customTags()) {
        add(def, true);
    }
    return out;
}

QStringList NoteHost::customTagIcons() const
{
    return Tags::customIcons();
}

void NoteHost::applyTag(const QString &tagId)
{
    const Tags::TagDef def = Tags::byId(tagId);
    if (!def.isValid() || m_pageItem == nullptr || currentPage() == nullptr) {
        return;
    }
    m_pageItem->toggleTag(def);
    emit editorFocusRequested();
}

bool NoteHost::addCustomTag(const QString &name, const QString &icon, const QColor &color)
{
    if (!Tags::addCustomTag(name, icon, color)) {
        showStatusMessage(tr("The tag needs a name, and at most %1 custom tags fit.").arg(Tags::kMaxCustomTags), 6000);
        return false;
    }
    emit tagCatalogChanged();
    return true;
}

void NoteHost::removeCustomTag(const QString &tagId)
{
    // Tags already on pages keep their look (it is saved with them).
    if (Tags::removeCustomTag(tagId)) {
        emit tagCatalogChanged();
    }
}

bool NoteHost::setTagDone(const QString &sectionId, const QString &pageId, int block, int entry, bool done)
{
    if (sectionId == m_currentSectionId && pageId == m_currentPageId && m_pageItem != nullptr) {
        return m_pageItem->setTagDone(block, entry, done); // one undo step
    }
    Notebook *notebook = m_document->notebook();
    NoteSection *section = notebook->sectionById(sectionId);
    NotePage *page = notebook->pageById(sectionId, pageId);
    if (section == nullptr || page == nullptr || (!page->loaded && !notebook->loadPage(*section, *page, nullptr))) {
        return false;
    }
    QJsonArray blocks = blocksArray(page->envelope.blocksJson);
    if (block < 0 || block >= blocks.size()) {
        return false;
    }
    QJsonObject object = blocks.at(block).toObject();
    object.insert(QStringLiteral("tags"), Tags::withDone(object.value(QStringLiteral("tags")).toArray(), entry, done));
    blocks.replace(block, object);
    page->envelope.blocksJson = withBlocksArray(page->envelope.blocksJson, blocks);
    m_document->setDirty(true);
    m_document->notifyContentChanged();
    if (notebook->isOpen()) {
        static_cast<void>(notebook->persistPage(*section, *page, nullptr));
    }
    return true;
}

void NoteHost::createTagSummaryPage()
{
    // OneNote's "Create Summary Page": every tagged item in scope, grouped
    // by tag, as tagged lines of a new page after this one.
    m_tagSummary->refresh();
    const QList<TaggedItem> &items = m_tagSummary->items();
    if (items.isEmpty()) {
        showStatusMessage(tr("No tagged notes to summarise."), 5000);
        return;
    }
    QString html;
    QList<QJsonObject> lineTags; // one per item paragraph, in order
    QStringList seen;
    for (const TaggedItem &first : items) {
        if (seen.contains(first.item.def.id)) {
            continue;
        }
        seen.append(first.item.def.id);
        html += QStringLiteral("<h2>%1</h2>").arg(first.item.def.name.toHtmlEscaped());
        for (const TaggedItem &tagged : items) {
            if (tagged.item.def.id != first.item.def.id) {
                continue;
            }
            html += QStringLiteral("<p>%1 <span style=\"color:#595959\">(%2)</span></p>")
                        .arg(tagged.item.text.toHtmlEscaped(), tagged.pageTitle.toHtmlEscaped());
            QJsonObject entry = Tags::entryFor(tagged.item.def, QString());
            if (tagged.item.def.todo) {
                entry.insert(QStringLiteral("done"), tagged.item.done);
            }
            lineTags.append(entry);
        }
    }
    RichImport::Imported imported = RichImport::fromHtml(html);
    // The item paragraphs are the ones that are not headings, in order.
    QJsonArray tags;
    qsizetype next = 0;
    for (const QindaTK::RichText::Block &body : imported.doc->blocks()) {
        if (body.paragraph && body.paragraph->format.headingLevel == 0 && next < lineTags.size()) {
            QJsonObject entry = lineTags.at(next++);
            entry.insert(QStringLiteral("p"), body.paragraph->id.toString(QUuid::WithoutBraces));
            tags.append(entry);
        }
    }
    imported.tags = tags;
    const QString pageId = addPageAfterCurrent(tr("Tag Summary"), QJsonArray{RichImport::textBlock(imported)});
    if (!pageId.isEmpty()) {
        selectPage(m_currentSectionId, pageId);
    }
}

} // namespace QindaOffice::Apps::Notes
