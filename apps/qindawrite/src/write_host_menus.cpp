// SPDX-License-Identifier: GPL-3.0-or-later
// WriteHost's menus: Format (styles + the controllers' items), Table,
// Insert, Review, Tools and Page inserted before the shell's AI menu; the
// inline AI commands appended to that one menu (never a second "AI"); the
// New from Template submenu; and the page's context menu, which leads
// with spelling suggestions for the word under the pointer.

#include "write_host.h"

#include "docx_text.h"
#include "page_text_item.h"
#include "format_controller.h"
#include "page_picture_tool.h"
#include "page_text_control.h"
#include "spell_checker.h"
#include "standard_actions.h"
#include "style_catalog.h"
#include "table_actions.h"
#include "table_layout.h"
#include "write_borders.h"
#include "write_font.h"
#include "write_paragraph.h"
#include "template_loader.h"
#include "write_comments.h"
#include "write_controls.h"
#include "write_review_actions.h"
#include "write_typography.h"

#include "inline_action_flow.h"

#include <QTextCursor>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

constexpr int kSuggestionSlots = 7;

// Turns a template display name into a stable, valid id suffix.
QString sanitizedId(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}

} // namespace

void WriteHost::insertMenus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    Quick::MenuTree *tree = menuTree();

    // Format: the named styles first (writeStyle<n>, stable ids), then the
    // controllers' character/paragraph items and the typography set.
    StyleCatalog catalog;
    const QStringList names = catalog.names();
    QList<MenuNode> styleItems;
    for (int id = 0; id < names.size(); ++id) {
        const QString actionId = QStringLiteral("writeStyle%1").arg(id);
        connect(actions()->add(actionId, names.at(id)), &ActionEntry::triggered, this,
                [this, id] { applyStyle(id); });
        styleItems.append(MenuNode::action(actionId));
    }
    QList<MenuNode> formatItems{MenuNode::submenu(QStringLiteral("writeStyleMenu"), tr("&Style"), styleItems),
                                MenuNode::separator()};
    formatItems += m_format->formatMenuItems();
    // Format > Picture: size and alternative text of the selected picture.
    m_picture = actions()->add(QStringLiteral("writePictureAction"), tr("&Picture…"), {}, QStringLiteral("image"));
    m_picture->setEnabled(false);
    connect(m_picture, &ActionEntry::triggered, this, &WriteHost::pictureDialogRequested);
    connect(m_control, &PageTextControl::selectionChanged, this, [this] {
        m_picture->setEnabled(m_page != nullptr && m_page->pictures() != nullptr && m_page->pictures()->selected() >= 0);
    });
    formatItems.append(MenuNode::action(m_picture->id()));
    formatItems.append(MenuNode::separator());
    formatItems += m_typography->formatMenuItems();
    // M70 (D-079): Word's Font, Paragraph, Tabs, Borders and Shading.
    formatItems.append(MenuNode::separator());
    formatItems += m_font->menuItems();
    formatItems += m_paragraph->menuItems();
    formatItems += m_borders->menuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("formatMenu"), tr("F&ormat"), formatItems));

    QList<MenuNode> tableItems = m_tables->tableMenuItems();
    tableItems.append(MenuNode::separator());
    tableItems += m_tableLayout->tableMenuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("tableMenu"), tr("&Table"), tableItems));

    QList<MenuNode> insertItems = m_format->insertMenuItems();
    insertItems.append(MenuNode::action(m_tableLayout->textToTableId()));
    insertItems.append(MenuNode::separator());
    insertItems += m_typography->insertMenuItems();
    insertItems.append(MenuNode::separator());
    insertItems += m_review->insertMenuItems();
    insertItems.append(MenuNode::separator());
    insertItems += headerFooterMenuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("insertMenu"), tr("&Insert"), insertItems));

    QList<MenuNode> reviewItems = m_review->reviewMenuItems();
    reviewItems.append(MenuNode::separator());
    reviewItems += commentMenuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("writeReviewMenu"), tr("&Review"), reviewItems));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("writeToolsMenu"), tr("T&ools"),
                                             {MenuNode::action(m_typography->wordCountId())}));

    // AGENT-GUARD: the inline AI commands belong in the SHELL's AI menu,
    // never in a second menu of the same title (two "AI" menus once sat
    // side by side in the exported tree).
    tree->appendTo(QStringLiteral("aiMenu"), MenuNode::separator());
    for (const Ai::InlineAction &inlineAction : Ai::inlineActions()) {
        const QString id = QStringLiteral("writeAi%1").arg(inlineAction.id);
        connect(actions()->add(id, inlineAction.displayName), &ActionEntry::triggered, this,
                [this, actionId = inlineAction.id] { m_inline->run(actionId); });
        tree->appendTo(QStringLiteral("aiMenu"), MenuNode::action(id));
    }

    // Page: setup and the PDF export.
    connect(actions()->add(QStringLiteral("writePageSetupAction"), tr("&Page Setup…")),
            &ActionEntry::triggered, this, [this] { emit pageSetupRequested(); });
    connect(actions()->add(QStringLiteral("writeExportPdfAction"), tr("&Export PDF…")),
            &ActionEntry::triggered, this, [this] {
                QString suggested = m_document->canonicalPath();
                suggested = suggested.isEmpty() ? tr("Untitled.pdf")
                                                : suggested.section(QLatin1Char('.'), 0, -2) + QStringLiteral(".pdf");
                emit exportPdfRequested(suggested);
            });
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("pageMenu"), tr("&Page"),
                                             {a("writePageSetupAction"), a("writeExportPdfAction")}));

    // New from Template: one entry per shipped template, generated ids
    // (writeTemplate_<name>) that stay stable (plan rule 4).
    const QList<Quick::TemplateEntry> templates = Quick::templatesFor(QStringLiteral("QindaWrite"));
    if (!templates.isEmpty()) {
        MenuNode submenu = MenuNode::submenu(QStringLiteral("fileTemplateMenu"), tr("New from &Template"));
        for (const Quick::TemplateEntry &entry : templates) {
            const QString id = QStringLiteral("writeTemplate_%1").arg(sanitizedId(entry.name));
            connect(actions()->add(id, entry.name), &ActionEntry::triggered, this,
                    [this, path = entry.path] { loadTemplate(path); });
            submenu.children.append(MenuNode::action(id));
        }
        tree->insertInto(QStringLiteral("fileMenu"), QString::fromLatin1(ActionId::kFileCloseWindow), submenu);
    }
}

// ---- context menu with spelling -------------------------------------------------

void WriteHost::createSpellingEntries()
{
    for (int i = 0; i < kSuggestionSlots; ++i) {
        ActionEntry *entry = actions()->add(QStringLiteral("writeSpellSuggest%1").arg(i), QString());
        entry->setVisible(false);
        connect(entry, &ActionEntry::triggered, this, [this, entry] {
            if (m_spellWord.isNull() || entry->text().isEmpty()) {
                return;
            }
            QTextCursor word = m_spellWord;
            word.insertText(entry->text());
            m_document->setDirty(true);
        });
        m_spellSuggestions.append(entry);
    }
    m_spellIgnore = actions()->add(QStringLiteral("writeSpellIgnoreAction"), QString());
    m_spellIgnore->setVisible(false);
    connect(m_spellIgnore, &ActionEntry::triggered, this, [this] {
        // Session-scoped ignore (no custom dictionary management); the
        // checker rehighlights so the squiggle drops immediately.
        if (!m_spellWord.isNull()) {
            m_spell->ignoreWord(m_spellWord.selectedText());
        }
    });
}

QList<MenuNode> WriteHost::headerFooterMenuItems()
{
    const auto make = [this](const char *id, const QString &text, const char *icon) {
        return actions()->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
    };
    ActionEntry *header = make("writeEditHeaderAction", tr("Edit &Header"), "panel-top");
    ActionEntry *footer = make("writeEditFooterAction", tr("Edit &Footer"), "panel-bottom");
    ActionEntry *number = make("writeInsertPageNumberAction", tr("Page &Number"), "hash");
    ActionEntry *count = make("writeInsertPageCountAction", tr("Number of &Pages"), "sigma");
    m_closeStory = make("writeCloseHeaderFooterAction", tr("&Close Header and Footer"), "x");
    m_closeStory->setEnabled(false);
    connect(header, &ActionEntry::triggered, this, [this] {
        if (m_page != nullptr) {
            m_page->editHeader();
        }
    });
    connect(footer, &ActionEntry::triggered, this, [this] {
        if (m_page != nullptr) {
            m_page->editFooter();
        }
    });
    connect(number, &ActionEntry::triggered, this, [this] { insertPageField(false); });
    connect(count, &ActionEntry::triggered, this, [this] { insertPageField(true); });
    connect(m_closeStory, &ActionEntry::triggered, this, [this] {
        if (m_page != nullptr) {
            m_page->closeStory();
        }
    });
    return {MenuNode::action(header->id()), MenuNode::action(footer->id()), MenuNode::action(number->id()),
            MenuNode::action(count->id()), MenuNode::action(m_closeStory->id())};
}

void WriteHost::insertPageField(bool total)
{
    if (m_page == nullptr) {
        return;
    }
    // Word's Page Number goes to the footer when the body has the caret.
    if (!m_page->isEditingStory()) {
        m_page->editFooter();
    }
    if (!m_page->isEditingStory()) {
        return;
    }
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    if (m_control->document()->isEmpty()) {
        QTextBlockFormat centred = cursor.blockFormat();
        centred.setAlignment(Qt::AlignHCenter);
        cursor.setBlockFormat(centred);
    }
    const QTextCharFormat next = Docx::insertPageField(cursor, total);
    cursor.endEditBlock();
    m_control->setTextCursor(cursor);
    m_control->setCurrentCharFormat(next);
}

// While a header or footer is open: Close is available, and what has no
// place in one (a page break, a footnote, the table of contents) is not.
void WriteHost::syncStoryActions()
{
    const bool story = m_page != nullptr && m_page->isEditingStory();
    if (m_closeStory != nullptr) {
        m_closeStory->setEnabled(story);
    }
    for (const char *id : {ActionId::kInsertPageBreak, ActionId::kInsertFootnote, ActionId::kInsertToc,
                           ActionId::kUpdateToc}) {
        if (ActionEntry *entry = action(QString::fromLatin1(id))) {
            entry->setEnabled(!story);
        }
    }
    if (story) {
        showStatusMessage(tr("Editing the %1 — Esc or a double-click on the page returns to the text")
                              .arg(m_page->storyLabel().toLower()),
                          0);
    } else {
        showStatusMessage(QString(), 1);
    }
}

QObject *WriteHost::commentsObject() const
{
    return m_comments;
}

QObject *WriteHost::controlsObject() const
{
    return m_controls;
}

QObject *WriteHost::bordersObject() const
{
    return m_borders;
}

QObject *WriteHost::paragraphObject() const
{
    return m_paragraph;
}

QObject *WriteHost::fontObject() const
{
    return m_font;
}

QObject *WriteHost::tableLayoutObject() const
{
    return m_tableLayout;
}

// Review > New Comment, Delete Comment, Previous / Next Comment, Show
// Comments (write_comments.h); the pane itself is QML (CommentsPanel.qml).
QList<MenuNode> WriteHost::commentMenuItems()
{
    const auto make = [this](const char *id, const QString &text, const char *icon, const QKeySequence &keys = {}) {
        return actions()->add(QString::fromLatin1(id), text, keys, QString::fromLatin1(icon));
    };
    ActionEntry *add = make("writeNewCommentAction", tr("New &Comment"), "message-square-plus",
                            QKeySequence(QStringLiteral("Ctrl+Alt+M")));
    ActionEntry *remove = make("writeDeleteCommentAction", tr("&Delete Comment"), "message-square-x");
    ActionEntry *previous = make("writePreviousCommentAction", tr("Pre&vious Comment"), "chevron-up");
    ActionEntry *next = make("writeNextCommentAction", tr("Ne&xt Comment"), "chevron-down");
    ActionEntry *show = make("writeShowCommentsAction", tr("Show Co&mments"), "message-square");
    show->setCheckable(true);
    connect(add, &ActionEntry::triggered, this, [this] { emit commentsPaneToggleRequested(true, true); });
    connect(remove, &ActionEntry::triggered, this, [this] { m_comments->remove(m_comments->activeId()); });
    connect(previous, &ActionEntry::triggered, m_comments, &WriteComments::previous);
    connect(next, &ActionEntry::triggered, m_comments, &WriteComments::next);
    connect(show, &ActionEntry::toggled, this, [this](bool on) { emit commentsPaneToggleRequested(on, false); });
    const auto sync = [this, add, remove, previous, next] {
        add->setEnabled(m_comments->canAdd());
        remove->setEnabled(!m_comments->activeId().isEmpty());
        previous->setEnabled(m_comments->count() > 0);
        next->setEnabled(m_comments->count() > 0);
    };
    connect(m_comments, &WriteComments::canAddChanged, this, sync);
    connect(m_comments, &WriteComments::activeChanged, this, sync);
    connect(m_comments, &WriteComments::changed, this, sync);
    sync();
    return {MenuNode::action(add->id()), MenuNode::action(remove->id()), MenuNode::action(previous->id()),
            MenuNode::action(next->id()), MenuNode::action(show->id())};
}

QVariantMap WriteHost::pictureInfo() const
{
    const PictureTool *tool = m_page != nullptr ? m_page->pictures() : nullptr;
    const int position = tool != nullptr ? tool->selected() : -1;
    if (position < 0) {
        return {};
    }
    const QSizeF size = tool->pictureSize(position);
    const QSizeF natural = tool->naturalSize(position);
    return {{QStringLiteral("width"), size.width()},
            {QStringLiteral("height"), size.height()},
            {QStringLiteral("naturalWidth"), natural.width()},
            {QStringLiteral("naturalHeight"), natural.height()},
            {QStringLiteral("altText"), tool->altText(position)}};
}

bool WriteHost::applyPicture(const QVariantMap &picture)
{
    PictureTool *tool = m_page != nullptr ? m_page->pictures() : nullptr;
    const int position = tool != nullptr ? tool->selected() : -1;
    if (position < 0) {
        return false;
    }
    const QSizeF size(picture.value(QStringLiteral("width")).toReal(), picture.value(QStringLiteral("height")).toReal());
    if (!tool->setPicture(position, size, picture.value(QStringLiteral("altText")).toString())) {
        return false;
    }
    m_page->update();
    return true;
}

void WriteHost::prepareSpellingMenu(const QPointF &docPos)
{
    for (ActionEntry *entry : m_spellSuggestions) {
        entry->setVisible(false);
    }
    m_spellIgnore->setVisible(false);
    m_spellWord = QTextCursor();
    if (m_spell == nullptr || !m_spell->isActive() || docPos.x() < 0) {
        return;
    }
    QTextCursor word = m_control->cursorForPosition(docPos);
    word.select(QTextCursor::WordUnderCursor);
    const QString text = word.selectedText();
    if (text.size() < 2 || !m_spell->isMisspelled(text)) {
        return;
    }
    m_spellWord = word;
    const QStringList suggestions = m_spell->suggestions(text);
    for (int i = 0; i < suggestions.size() && i < kSuggestionSlots; ++i) {
        m_spellSuggestions.at(i)->setText(suggestions.at(i));
        m_spellSuggestions.at(i)->setVisible(true);
    }
    m_spellIgnore->setText(tr("Ignore '%1'").arg(text));
    m_spellIgnore->setVisible(true);
}

QVariantMap WriteHost::contextMenuNode() const
{
    QList<MenuNode> children;
    for (const ActionEntry *entry : m_spellSuggestions) {
        children.append(MenuNode::action(entry->id()));
    }
    children.append(MenuNode::action(m_spellIgnore->id()));
    children.append(MenuNode::separator());
    for (const char *id : {ActionId::kEditUndo, ActionId::kEditRedo}) {
        children.append(MenuNode::action(QString::fromLatin1(id)));
    }
    children.append(MenuNode::separator());
    for (const char *id : {ActionId::kEditCut, ActionId::kEditCopy, ActionId::kEditPaste}) {
        children.append(MenuNode::action(QString::fromLatin1(id)));
    }
    children.append(MenuNode::separator());
    children.append(MenuNode::action(QString::fromLatin1(ActionId::kEditSelectAll)));
    // Word's context menu: Font…, Paragraph…, and in a table its properties.
    children.append(MenuNode::separator());
    for (const QString &id : {m_font->fontId(), m_paragraph->paragraphId(), QStringLiteral("writeBordersShadingAction"),
                              QStringLiteral("writeTablePropertiesAction")}) {
        children.append(MenuNode::action(id));
    }
    return MenuNode::submenu(QStringLiteral("writePageContextMenu"), QString(), children).toVariant();
}

} // namespace QindaOffice::Apps::Write
