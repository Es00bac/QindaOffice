// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_tree.h"

#include <QVariantMap>

namespace QindaOffice::Quick {

QVariantMap MenuNode::toVariant() const
{
    QVariantMap map;
    switch (kind) {
    case Kind::Action:
        map.insert(QStringLiteral("kind"), QStringLiteral("action"));
        map.insert(QStringLiteral("id"), id);
        break;
    case Kind::Separator:
        map.insert(QStringLiteral("kind"), QStringLiteral("separator"));
        break;
    case Kind::Submenu: {
        map.insert(QStringLiteral("kind"), QStringLiteral("menu"));
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("title"), title);
        QVariantList list;
        list.reserve(children.size());
        for (const MenuNode &child : children) {
            list.append(child.toVariant());
        }
        map.insert(QStringLiteral("children"), list);
        break;
    }
    }
    return map;
}

MenuTree::MenuTree(QObject *parent)
    : QObject(parent)
{
}

QVariantList MenuTree::menus() const
{
    QVariantList list;
    list.reserve(m_menus.size());
    for (const MenuNode &menu : m_menus) {
        list.append(menu.toVariant());
    }
    return list;
}

void MenuTree::setTopLevel(QList<MenuNode> menus)
{
    m_menus = std::move(menus);
    emit menusChanged();
}

void MenuTree::appendMenu(const MenuNode &menu)
{
    m_menus.append(menu);
    emit menusChanged();
}

bool MenuTree::insertMenuBefore(const QString &anchorId, const MenuNode &menu)
{
    for (qsizetype i = 0; i < m_menus.size(); ++i) {
        if (m_menus.at(i).id == anchorId) {
            m_menus.insert(i, menu);
            emit menusChanged();
            return true;
        }
    }
    m_menus.append(menu);
    emit menusChanged();
    return false;
}

MenuNode *MenuTree::findMutable(QList<MenuNode> &list, const QString &id)
{
    for (MenuNode &node : list) {
        if (node.kind == MenuNode::Kind::Submenu && node.id == id) {
            return &node;
        }
        if (node.kind == MenuNode::Kind::Submenu) {
            if (MenuNode *found = findMutable(node.children, id)) {
                return found;
            }
        }
    }
    return nullptr;
}

const MenuNode *MenuTree::findConst(const QList<MenuNode> &list, const QString &id)
{
    for (const MenuNode &node : list) {
        if (node.kind == MenuNode::Kind::Submenu && node.id == id) {
            return &node;
        }
        if (node.kind == MenuNode::Kind::Submenu) {
            if (const MenuNode *found = findConst(node.children, id)) {
                return found;
            }
        }
    }
    return nullptr;
}

const MenuNode *MenuTree::find(const QString &id) const
{
    return findConst(m_menus, id);
}

bool MenuTree::replaceSubmenu(const QString &id, const MenuNode &menu)
{
    MenuNode *target = findMutable(m_menus, id);
    if (target == nullptr) {
        return false;
    }
    *target = menu;
    emit menusChanged();
    return true;
}

bool MenuTree::insertInto(const QString &menuId, const QString &anchorId, const MenuNode &node)
{
    MenuNode *target = findMutable(m_menus, menuId);
    if (target == nullptr) {
        return false;
    }
    qsizetype at = target->children.size();
    if (!anchorId.isEmpty()) {
        for (qsizetype i = 0; i < target->children.size(); ++i) {
            if (target->children.at(i).id == anchorId) {
                at = i;
                break;
            }
        }
    }
    target->children.insert(at, node);
    emit menusChanged();
    return true;
}

} // namespace QindaOffice::Quick
