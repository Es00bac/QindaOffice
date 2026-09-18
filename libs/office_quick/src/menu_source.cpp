// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_source.h"

#include "action_model.h"
#include "menu_tree.h"

#include <qindaqt/shell/global_menu/protocol/menu_limits.h>

namespace QindaOffice::Quick {

namespace {

namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;
namespace Exporter = QindaQt::Shell::GlobalMenu::Exporter;

struct Walk {
    const ActionModel *actions = nullptr;
    int total = 0;
    int separators = 0;
    QString defect;
};

// '&x' marks x as the mnemonic; '&&' is a literal ampersand.
void splitMnemonic(const QString &raw, QString &plain, int &index)
{
    plain.clear();
    plain.reserve(raw.size());
    index = -1;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c == QLatin1Char('&') && i + 1 < raw.size()) {
            if (raw.at(i + 1) == QLatin1Char('&')) {
                plain.append(QLatin1Char('&'));
                ++i;
                continue;
            }
            if (index == -1) {
                index = int(plain.size());
            }
            continue;
        }
        plain.append(c);
    }
}

bool build(const MenuNode &node, int depth, Walk &walk, Protocol::MenuItem &out)
{
    if (++walk.total > Protocol::kMaxTotalItems) {
        walk.defect = QStringLiteral("too-many-items");
        return false;
    }
    if (depth > Protocol::kMaxDepth) {
        walk.defect = QStringLiteral("too-deep");
        return false;
    }
    switch (node.kind) {
    case MenuNode::Kind::Separator:
        out = Protocol::MenuItem{};
        out.kind = Protocol::MenuItemKind::Separator;
        out.id = QStringLiteral("sep:%1").arg(walk.separators++);
        return true;
    case MenuNode::Kind::Action: {
        const ActionEntry *entry = walk.actions->entry(node.id);
        out = Protocol::MenuItem{};
        out.kind = Protocol::MenuItemKind::Action;
        out.id = node.id;
        if (entry == nullptr) {
            // A menu naming a command the host never created: the tree is
            // wrong, not the export. Fail closed so the test suite sees it.
            walk.defect = QStringLiteral("unknown-action:") + node.id;
            return false;
        }
        splitMnemonic(entry->text(), out.text, out.mnemonicIndex);
        out.shortcutText = entry->shortcutText();
        out.enabled = entry->isEnabled();
        out.visible = entry->isVisible();
        out.checkable = entry->isCheckable();
        out.checked = entry->isCheckable() && entry->isChecked();
        out.radioGroup = entry->radioGroup();
        return true;
    }
    case MenuNode::Kind::Submenu: {
        out = Protocol::MenuItem{};
        out.kind = Protocol::MenuItemKind::Submenu;
        out.id = node.id;
        splitMnemonic(node.title, out.text, out.mnemonicIndex);
        if (node.children.size() > Protocol::kMaxChildrenPerItem) {
            walk.defect = QStringLiteral("too-many-children");
            return false;
        }
        for (const MenuNode &child : node.children) {
            Protocol::MenuItem item;
            if (!build(child, depth + 1, walk, item)) {
                return false;
            }
            out.children.append(item);
        }
        return true;
    }
    }
    return false;
}

} // namespace

ActionModelMenuSource::ActionModelMenuSource(const MenuTree *tree, const ActionModel *actions,
                                             QUuid ownerWindowId)
    : m_tree(tree)
    , m_actions(actions)
    , m_ownerWindowId(ownerWindowId)
{
}

Exporter::MenuSnapshot ActionModelMenuSource::snapshot() const
{
    Exporter::MenuSnapshot result;
    result.tree.ownerWindowId = m_ownerWindowId;
    Walk walk;
    walk.actions = m_actions;
    for (const MenuNode &menu : m_tree->topLevel()) {
        Protocol::MenuItem item;
        if (!build(menu, 1, walk, item)) {
            result.complete = false;
            result.defectCode = walk.defect;
            result.tree.items.clear();
            return result;
        }
        result.tree.items.append(item);
    }
    return result;
}

} // namespace QindaOffice::Quick
