// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The window's menu bar as data (plan §3.2/§3.4): an ordered tree of
// menus whose leaves are action ids. Two views read it — the local
// Tk.MenuBar (MenuBarFromModel.qml, through `menus`) and the panel export
// (ActionModelMenuSource) — so they cannot disagree.
//
// AGENT-CONTRACT: menu ids (`fileMenu`, `viewMenu`, `aiMenu`, …) are as
// stable as action ids: the export names submenus by them and apps insert
// their menus relative to them (insertMenuBefore("aiMenu", …)).

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Quick {

struct MenuNode {
    enum class Kind { Action, Separator, Submenu };

    Kind kind = Kind::Action;
    QString id;    // action id, or the submenu's stable id
    QString title; // submenu title with '&' mnemonic; unused for actions
    QList<MenuNode> children;

    static MenuNode action(const QString &actionId) { return {Kind::Action, actionId, {}, {}}; }
    static MenuNode separator() { return {Kind::Separator, {}, {}, {}}; }
    static MenuNode submenu(const QString &id, const QString &title, QList<MenuNode> children = {})
    {
        return {Kind::Submenu, id, title, std::move(children)};
    }
    [[nodiscard]] QVariantMap toVariant() const;
};

class MenuTree : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The DocumentHost owns the window's MenuTree.")
    // [{kind:"menu", id, title, children:[{kind:"action", id} | {kind:"separator"} | {kind:"menu",…}]}]
    Q_PROPERTY(QVariantList menus READ menus NOTIFY menusChanged)
public:
    explicit MenuTree(QObject *parent = nullptr);

    [[nodiscard]] const QList<MenuNode> &topLevel() const { return m_menus; }
    [[nodiscard]] QVariantList menus() const;

    void setTopLevel(QList<MenuNode> menus);
    void appendMenu(const MenuNode &menu);
    // Inserts a top-level menu before the one with `anchorId` (appends
    // when the anchor is unknown, and says so in the return value).
    bool insertMenuBefore(const QString &anchorId, const MenuNode &menu);
    // Replaces the submenu (at any depth) whose id matches; false if none.
    bool replaceSubmenu(const QString &id, const MenuNode &menu);
    // Inserts `node` into the submenu `menuId` before the action/submenu
    // `anchorId` (appends when the anchor is empty or unknown).
    bool insertInto(const QString &menuId, const QString &anchorId, const MenuNode &node);
    void appendTo(const QString &menuId, const MenuNode &node) { insertInto(menuId, {}, node); }
    [[nodiscard]] const MenuNode *find(const QString &id) const;

signals:
    void menusChanged();

private:
    static MenuNode *findMutable(QList<MenuNode> &list, const QString &id);
    static const MenuNode *findConst(const QList<MenuNode> &list, const QString &id);

    QList<MenuNode> m_menus;
};

} // namespace QindaOffice::Quick
