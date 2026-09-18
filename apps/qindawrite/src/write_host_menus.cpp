// SPDX-License-Identifier: GPL-3.0-or-later
// WriteHost's menus: Format (styles + the controllers' items), Table,
// Insert, Review, Tools and Page inserted before the shell's AI menu; the
// inline AI commands appended to that one menu (never a second "AI"); the
// New from Template submenu; and the page's context menu, which leads
// with spelling suggestions for the word under the pointer.

#include "write_host.h"

#include "format_controller.h"
#include "page_text_control.h"
#include "spell_checker.h"
#include "standard_actions.h"
#include "style_catalog.h"
#include "table_actions.h"
#include "template_loader.h"
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
    formatItems.append(MenuNode::separator());
    formatItems += m_typography->formatMenuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("formatMenu"), tr("F&ormat"), formatItems));

    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("tableMenu"), tr("&Table"),
                                             m_tables->tableMenuItems()));

    QList<MenuNode> insertItems = m_format->insertMenuItems();
    insertItems.append(MenuNode::separator());
    insertItems += m_typography->insertMenuItems();
    insertItems.append(MenuNode::separator());
    insertItems += m_review->insertMenuItems();
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("insertMenu"), tr("&Insert"), insertItems));

    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("writeReviewMenu"), tr("&Review"),
                                             m_review->reviewMenuItems()));
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
                [this, actionId = inlineAction.id] { m_inline->run(actionId, this); });
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

void WriteHost::prepareSpellingMenu(const QPointF &docPos)
{
    for (ActionEntry *entry : m_spellSuggestions) {
        entry->setVisible(false);
    }
    m_spellIgnore->setVisible(false);
    m_spellWord = QTextCursor();
    if (m_spell == nullptr || !m_spell->isActive()) {
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
    return MenuNode::submenu(QStringLiteral("writePageContextMenu"), QString(), children).toVariant();
}

} // namespace QindaOffice::Apps::Write
