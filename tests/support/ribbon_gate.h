// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The ribbon gate (D-041), shared by the four apps' UI tests:
//  - coverage: every command of the menu tree is reachable from the ribbon
//    (a button or a dropdown entry) unless the app lists it as menu-only;
//  - icons: every icon name the layout asks for exists in QindaTK;
//  - squeeze (AGENTS.md "Squeeze"): at the window's current width, every
//    item of every tab — contextual tabs forced on — is visible, non-empty
//    and inside the ribbon's bounds.

#include "action_model.h"
#include "menu_tree.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTest>
#include <QVariant>

namespace QindaOffice::Test {

inline QQuickItem *findNamed(QQuickItem *parent, const QString &name)
{
    for (QQuickItem *child : parent->childItems()) {
        if (child->objectName() == name) {
            return child;
        }
        if (QQuickItem *found = findNamed(child, name)) {
            return found;
        }
    }
    return nullptr;
}

inline QStringList callStrings(QObject *object, const char *method, const QVariant &arg = {})
{
    QVariant out;
    if (arg.isValid()) {
        QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, arg));
    } else {
        QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, out));
    }
    return out.toStringList();
}

inline void collectMenuActions(const QList<Quick::MenuNode> &nodes, QStringList &into)
{
    for (const Quick::MenuNode &node : nodes) {
        if (node.kind == Quick::MenuNode::Kind::Submenu) {
            collectMenuActions(node.children, into);
        } else if (node.kind == Quick::MenuNode::Kind::Action) {
            into.append(node.id);
        }
    }
}

// Menu commands that deliberately live only in the menus (and the command
// search): process/session/about entries every app shares.
inline QStringList sharedMenuOnly()
{
    return {QStringLiteral("fileQuitAction"), QStringLiteral("editFindNextAction"),
            QStringLiteral("editFindPreviousAction"), QStringLiteral("settingsRestorePreviousAction"),
            QStringLiteral("helpAboutAction"), QStringLiteral("helpAboutQtAction")};
}

// Returns human-readable problems; empty means the gate passes.
inline QStringList ribbonProblems(QQuickWindow *window, const Quick::MenuTree *tree,
                                  const QStringList &appMenuOnly)
{
    QStringList problems;
    QQuickItem *ribbon = findNamed(window->contentItem(), QStringLiteral("officeRibbon"));
    if (ribbon == nullptr) {
        return {QStringLiteral("no officeRibbon in the window")};
    }
    const QStringList reachable = callStrings(ribbon, "commandIds");
    QStringList menuIds;
    collectMenuActions(tree->topLevel(), menuIds);
    const QStringList menuOnly = sharedMenuOnly() + appMenuOnly;
    for (const QString &id : menuIds) {
        if (!reachable.contains(id) && !menuOnly.contains(id)) {
            problems << QStringLiteral("menu command not on the ribbon: %1").arg(id);
        }
    }
    for (const QString &icon : callStrings(ribbon, "unknownIcons")) {
        problems << QStringLiteral("unknown icon name: %1").arg(icon);
    }
    // Force every contextual tab on, then walk the tabs.
    const QStringList contexts = callStrings(ribbon, "contextualIds");
    ribbon->setProperty("extraContexts", QVariant(contexts));
    for (const QString &tab : callStrings(ribbon, "layoutTabIds")) {
        QVariant ok;
        QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, tab));
        if (!ok.toBool()) {
            problems << QStringLiteral("tab %1 cannot be activated").arg(tab);
            continue;
        }
        QTest::qWait(60);
        for (const QString &item : callStrings(ribbon, "tabItemIds", tab)) {
            QQuickItem *slot = findNamed(ribbon, QStringLiteral("ribbonItem_") + item);
            if (slot == nullptr) {
                problems << QStringLiteral("%1: %2 not drawn").arg(tab, item);
                continue;
            }
            const QRectF r = slot->mapRectToItem(ribbon, QRectF(0, 0, slot->width(), slot->height()));
            if (!slot->isVisible() || r.width() < 1 || r.height() < 1) {
                problems << QStringLiteral("%1: %2 hidden or empty").arg(tab, item);
            } else if (r.left() < -0.5 || r.right() > ribbon->width() + 0.5 || r.bottom() > ribbon->height() + 0.5) {
                problems << QStringLiteral("%1: %2 outside the ribbon at width %3")
                                .arg(tab, item).arg(ribbon->width());
            }
        }
    }
    ribbon->setProperty("extraContexts", QVariant(QStringList()));
    return problems;
}

} // namespace QindaOffice::Test
