// SPDX-License-Identifier: GPL-3.0-or-later
// The menus QindaShow's arrange commands are reached through. Split from
// show_arrange.cpp to keep both files inside the line budget; every entry
// is the persistent one, so a command's enabled state is identical in the
// menu bar, the strip and the canvas context menu.

#include "show_arrange.h"

#include "menu_tree.h"

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {
MenuNode a(const ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}
} // namespace

QList<MenuNode> ShowArrangeActions::arrangeMenuItems() const
{
    return {a(m_bringToFront), a(m_bringForward), a(m_sendBackward), a(m_sendToBack), MenuNode::separator(),
            MenuNode::submenu(QStringLiteral("showAlignMenu"), tr("&Align"),
                              {a(m_alignLeft), a(m_alignHCenter), a(m_alignRight), MenuNode::separator(),
                               a(m_alignTop), a(m_alignVCenter), a(m_alignBottom), MenuNode::separator(),
                               a(m_distributeH), a(m_distributeV)}),
            MenuNode::separator(), a(m_duplicate), a(m_deleteShape)};
}

QList<MenuNode> ShowArrangeActions::slideMenuItems() const
{
    return {a(m_slideMoveUp), a(m_slideMoveDown)};
}

QStringList ShowArrangeActions::stripIds() const
{
    return {m_bringToFront->id(), m_sendToBack->id(), m_alignLeft->id(), m_alignHCenter->id(),
            m_alignRight->id(),   m_alignTop->id(),   m_alignVCenter->id(), m_alignBottom->id()};
}

QList<MenuNode> ShowArrangeActions::contextMenuItems() const
{
    return {a(m_duplicate), a(m_deleteShape), MenuNode::separator(), a(m_bringToFront), a(m_bringForward),
            a(m_sendBackward), a(m_sendToBack)};
}

} // namespace QindaOffice::Apps::Show
