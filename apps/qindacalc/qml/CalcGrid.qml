// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Calc

// The grid: the SpreadsheetItem with its two scroll bars, the one live
// cell editor placed at editorRect (Return commits and moves down, Tab
// commits and moves right, Escape cancels), the comment tooltip and the
// context menu built from the host's entries.
Item {
    id: root

    required property CalcHost host
    property var contextMenuNode: null
    readonly property SpreadsheetItem grid: sheet

    SpreadsheetItem {
        id: sheet
        objectName: "calcGrid"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: vbar.left
        anchors.bottom: hbar.top
        focus: true
        headerColor: Tk.Theme.color.panel
        headerTextColor: Tk.Theme.color.textMuted
        headerLineColor: Tk.Theme.color.border
        selectionColor: Tk.Theme.color.accent
        cursorColor: Tk.Theme.color.accent
        Component.onCompleted: root.host.attachGrid(sheet)

        // Typing lands in the editor; the grid takes the focus back on
        // commit or cancel so the next key navigates again.
        Tk.TextField {
            id: editor
            objectName: "cellEditor"
            visible: sheet.editing
            x: sheet.editorRect.x
            y: sheet.editorRect.y
            width: Math.max(sheet.editorRect.width, 60)
            height: Math.max(sheet.editorRect.height, Tk.Theme.size.controlSm)
            font: sheet.cellFont
            Connections {
                target: sheet
                function onEditingChanged() {
                    if (!sheet.editing) {
                        sheet.forceActiveFocus(Qt.OtherFocusReason)
                        return
                    }
                    editor.text = sheet.editorText
                    editor.forceActiveFocus(Qt.OtherFocusReason)
                    if (sheet.editorSelectAll) {
                        editor.selectAll()
                    } else {
                        editor.cursorPosition = editor.text.length
                    }
                }
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    sheet.commitEditor(editor.text, !(event.modifiers & Qt.ShiftModifier))
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                    sheet.commitEditor(editor.text, false)
                    sheet.stepCursor(0, event.key === Qt.Key_Tab ? 1 : -1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    sheet.cancelEditor()
                    event.accepted = true
                }
            }
            onActiveFocusChanged: if (!activeFocus && sheet.editing) sheet.commitEditor(editor.text, false)
        }

        Tk.ToolTip {
            objectName: "commentTip"
            visible: sheet.hoverComment.length > 0
            text: sheet.hoverComment
            x: sheet.hoverPoint.x + 12
            y: sheet.hoverPoint.y + 16
        }
    }
    Tk.ScrollBar {
        id: vbar
        objectName: "gridVScroll"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: hbar.top
        orientation: Qt.Vertical
        active: true
        policy: T.ScrollBar.AlwaysOn
        size: sheet.scrollHeight > 0 ? Math.min(1, sheet.viewportHeight / sheet.scrollHeight) : 1
        position: sheet.scrollHeight > 0 ? sheet.scrollY / sheet.scrollHeight : 0
        onPositionChanged: if (pressed || hovered) sheet.scrollY = position * sheet.scrollHeight
        stepSize: sheet.scrollHeight > 0 ? 20 / sheet.scrollHeight : 0.1
    }
    Tk.ScrollBar {
        id: hbar
        objectName: "gridHScroll"
        anchors.left: parent.left
        anchors.right: vbar.left
        anchors.bottom: parent.bottom
        orientation: Qt.Horizontal
        active: true
        policy: T.ScrollBar.AlwaysOn
        size: sheet.scrollWidth > 0 ? Math.min(1, sheet.viewportWidth / sheet.scrollWidth) : 1
        position: sheet.scrollWidth > 0 ? sheet.scrollX / sheet.scrollWidth : 0
        onPositionChanged: if (pressed || hovered) sheet.scrollX = position * sheet.scrollWidth
        stepSize: sheet.scrollWidth > 0 ? 96 / sheet.scrollWidth : 0.1
    }

    MenuFromNode {
        id: contextMenu
        objectName: "calcGridContextMenu"
        node: root.contextMenuNode
        actions: root.host.actions
    }
    Connections {
        target: root.host
        function onContextMenuRequested(x, y) {
            contextMenu.popup(sheet.mapToItem(root, x, y))
        }
    }
}
