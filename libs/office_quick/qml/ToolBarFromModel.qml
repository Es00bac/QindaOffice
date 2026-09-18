// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// A wrapping tool strip built from action ids in `commands`:
// "fileNewAction", "|" for a separator, or { component: Component } for a
// widget slot (a combo, a zoom island). A command with an icon is an
// icon-only Tk.IconButton with the text as its tooltip; one without shows
// its text (D-013: a null icon never becomes a blank square). Buttons bind
// live to their ActionEntry. objectNames: toolButton_<actionId>.
//
// AGENT-GUARD: the items are created with createObject straight into the
// bar's row, not through a Repeater of Loaders. A Loader whose `visible`
// followed its item's read the item's EFFECTIVE visibility (false under an
// invisible Loader), a cycle that kept every button hidden; and `commands`
// is not named `items` because that is Tk.ToolBar's default child list.
Tk.ToolBar {
    id: bar

    property ActionModel actions: null
    property var commands: []
    wrap: true

    Component {
        id: iconButtonComponent
        Tk.IconButton {
            id: button
            property ActionEntry entry: null
            Tk.Flex.shrink: 0
            objectName: entry !== null ? "toolButton_" + entry.id : ""
            iconName: entry !== null ? entry.iconName : ""
            tooltip: entry !== null ? entry.plainText + (entry.shortcutText.length > 0 ? "  " + entry.shortcutText : "") : ""
            enabled: entry !== null && entry.enabled
            visible: entry !== null && entry.visible
            checkable: entry !== null && entry.checkable
            checked: entry !== null && entry.checked
            onClicked: {
                entry.trigger()
                button.checked = Qt.binding(function() { return button.entry !== null && button.entry.checked })
            }
        }
    }
    Component {
        id: textButtonComponent
        Tk.Button {
            id: button
            property ActionEntry entry: null
            Tk.Flex.shrink: 0
            objectName: entry !== null ? "toolButton_" + entry.id : ""
            text: entry !== null ? entry.plainText : ""
            tooltip: entry !== null ? entry.shortcutText : ""
            available: entry !== null && entry.enabled
            visible: entry !== null && entry.visible
            checkable: entry !== null && entry.checkable
            checked: entry !== null && entry.checked
            variant: "ghost"
            onClicked: {
                entry.trigger()
                button.checked = Qt.binding(function() { return button.entry !== null && button.entry.checked })
            }
        }
    }
    Component { id: separatorComponent; Tk.ToolSeparator { Tk.Flex.shrink: 0 } }

    // AGENT-GUARD: the rebuild is coalesced (Qt.callLater) and clears the
    // row by its CHILDREN, not by a bookkeeping list: `commands`, `actions`
    // and completion all fire during creation, and a list property that
    // was reset between two of them left every button built twice.
    function rebuild() {
        Qt.callLater(bar.rebuildNow)
    }

    function rebuildNow() {
        const row = bar.rowItem
        if (row === null || row === undefined) {
            return
        }
        const stale = []
        for (let i = 0; i < row.children.length; ++i) {
            stale.push(row.children[i])
        }
        for (let i = 0; i < stale.length; ++i) {
            stale[i].parent = null
            stale[i].destroy()
        }
        if (bar.actions === null) {
            return
        }
        for (let i = 0; i < bar.commands.length; ++i) {
            const command = bar.commands[i]
            if (command === "|") {
                separatorComponent.createObject(row)
            } else if (typeof command === "object" && command !== null && command.component !== undefined) {
                command.component.createObject(row)
            } else {
                const entry = bar.actions.entry(command)
                if (entry === null) {
                    console.warn("QindaOffice: tool strip names an unknown command " + command)
                    continue
                }
                (entry.iconName.length > 0 ? iconButtonComponent : textButtonComponent)
                    .createObject(row, { entry: entry })
            }
        }
    }

    onCommandsChanged: rebuild()
    onActionsChanged: rebuild()
    Component.onCompleted: rebuild()
}
