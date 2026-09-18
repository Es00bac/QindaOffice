// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The panel export's view of the window's menu (plan §3.4).
//
// AGENT-CONTRACT (desktop exporter/menu_source.h): snapshot() is
// synchronous, side-effect-free, GUI thread; an incomplete traversal is
// reported with `complete = false` and a defect code, never as a prefix.
// Ids: action ids for actions, MenuNode ids for submenus, "sep:<n>" for
// separators; radio groups from ActionEntry::radioGroup(); the '&'
// mnemonic becomes `mnemonicIndex`. The desktop validates the result
// (unique ids, ≤ 6 deep, ≤ 128 children, one checked per radio group).

#include <qindaqt/shell/global_menu/exporter/menu_source.h>

#include <QUuid>

namespace QindaOffice::Quick {

class ActionModel;
class MenuTree;

class ActionModelMenuSource final : public QindaQt::Shell::GlobalMenu::Exporter::MenuSource {
public:
    // Both must outlive the source (the DocumentHost owns all three).
    ActionModelMenuSource(const MenuTree *tree, const ActionModel *actions, QUuid ownerWindowId);

    [[nodiscard]] QindaQt::Shell::GlobalMenu::Exporter::MenuSnapshot snapshot() const override;

private:
    const MenuTree *m_tree;
    const ActionModel *m_actions;
    QUuid m_ownerWindowId;
};

} // namespace QindaOffice::Quick
