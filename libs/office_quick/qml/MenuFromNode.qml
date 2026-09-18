// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// One Tk.Menu built from a MenuTree node: actions become Tk.MenuItems
// bound to their ActionEntry (text, shortcut, enabled, checked, radio),
// separators Tk.MenuSeparators, submenus nested MenuFromNodes — in the
// node's order. objectNames: menu_<id>, menuItem_<actionId>.
Tk.Menu {
    id: menu

    property var node: null
    property ActionModel actions: null

    title: node !== null && node.title !== undefined ? node.title : ""
    objectName: node !== null && node.id !== undefined ? "menu_" + node.id : ""

    Component {
        id: itemComponent
        Tk.MenuItem {
            id: item
            // AGENT-NOTE: bindings evaluate once before createObject's
            // initial properties land, hence the null guards.
            property ActionEntry entry: null
            objectName: entry !== null ? "menuItem_" + entry.id : ""
            text: entry !== null ? entry.text : ""
            shortcut: entry !== null ? entry.shortcutText : ""
            iconName: entry !== null ? entry.iconName : ""
            enabled: entry !== null && entry.enabled
            visible: entry !== null && entry.visible
            height: visible ? implicitHeight : 0
            checkable: entry !== null && entry.checkable
            checked: entry !== null && entry.checked
            radio: entry !== null && entry.radioGroup.length > 0
            onTriggered: {
                entry.trigger()
                // AGENT-GUARD: T.MenuItem flips `checked` itself on a click,
                // which breaks the binding above; the entry is the truth.
                item.checked = Qt.binding(function() { return item.entry !== null && item.entry.checked })
            }
        }
    }
    Component { id: separatorComponent; Tk.MenuSeparator { } }
    // AGENT-NOTE: a submenu is another MenuFromNode, created through
    // Qt.createComponent at run time: an inline Component of this file's
    // own type is a recursive instantiation the engine refuses to load.
    property var submenuComponent: null

    function rebuild() {
        while (menu.count > 0) {
            const last = menu.itemAt(menu.count - 1)
            const sub = menu.menuAt(menu.count - 1)
            if (sub !== null) {
                menu.removeMenu(sub)
                sub.destroy()
            } else {
                menu.removeItem(last)
                last.destroy()
            }
        }
        if (menu.node === null || menu.node.children === undefined) {
            return
        }
        const children = menu.node.children
        for (let i = 0; i < children.length; ++i) {
            const child = children[i]
            if (child.kind === "separator") {
                menu.addItem(separatorComponent.createObject(menu.contentItem))
            } else if (child.kind === "menu") {
                if (menu.submenuComponent === null) {
                    menu.submenuComponent = Qt.createComponent(Qt.resolvedUrl("MenuFromNode.qml"))
                }
                if (menu.submenuComponent.status !== Component.Ready) {
                    console.warn("QindaOffice: " + menu.submenuComponent.errorString())
                    continue
                }
                menu.addMenu(menu.submenuComponent.createObject(menu, { node: child, actions: menu.actions }))
            } else if (child.kind === "action" && menu.actions !== null) {
                const entry = menu.actions.entry(child.id)
                if (entry === null) {
                    console.warn("QindaOffice: menu names an unknown command " + child.id)
                    continue
                }
                menu.addItem(itemComponent.createObject(menu.contentItem, { entry: entry }))
            }
        }
    }

    onNodeChanged: rebuild()
    onActionsChanged: rebuild()
    Component.onCompleted: rebuild()
}
