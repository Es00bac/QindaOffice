// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Calc

// The grid: the SpreadsheetItem with its two scroll bars (four under
// View ▸ Split, M82), the one live
// cell editor placed at editorRect (Return commits and moves down, Tab
// commits and moves right, Escape cancels, Ctrl+Shift+Enter enters an array
// formula), the comment tooltip and the context menu built from the host's
// entries; over all of it, a chart sheet's chart when one is active (M69).
// M82: View ▸ New Window's grid is one too, `viewOnly` on `viewSheet`
// (CalcViewWindows.qml) — no editor, no context menu, no chart sheet.
Item {
    id: root

    required property CalcHost host
    property var contextMenuNode: null
    property bool viewOnly: false
    property string viewSheet: ""
    readonly property SpreadsheetItem grid: sheet

    // "=f" → "{=f}": the form an array formula takes (CalcHost::commitInput).
    function asArrayFormula(text) {
        const t = text.trim()
        return t.startsWith("=") ? "{" + t + "}" : t
    }

    SpreadsheetItem {
        id: sheet
        objectName: root.viewOnly ? "calcViewGrid" : "calcGrid"
        readOnly: root.viewOnly
        viewSheet: root.viewSheet
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
        Component.onCompleted: root.viewOnly ? root.host.attachViewGrid(sheet) : root.host.attachGrid(sheet)

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
                const arrayEntry = (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && arrayEntry) {
                    // Ctrl+Shift+Enter: a legacy array formula (M69).
                    sheet.commitEditor(root.asArrayFormula(editor.text), true)
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
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
        // M82: under View ▸ Split this bar scrolls the bottom panes and the
        // top panes get their own (splitVBar).
        anchors.topMargin: sheet.splitBarY > 0 ? sheet.splitBarY + sheet.splitBarWidth : 0
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
        anchors.leftMargin: sheet.splitBarX > 0 ? sheet.splitBarX + sheet.splitBarWidth : 0
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

    // M82: the left and top panes' own scroll bars while the view is split.
    Tk.ScrollBar {
        id: splitHBar
        objectName: "gridSplitHScroll"
        visible: sheet.splitBarX > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: sheet.splitBarX
        orientation: Qt.Horizontal
        active: true
        policy: T.ScrollBar.AlwaysOn
        size: sheet.scrollWidth > 0 ? Math.min(1, sheet.splitX / sheet.scrollWidth) : 1
        position: sheet.scrollWidth > 0 ? sheet.splitScrollX / sheet.scrollWidth : 0
        onPositionChanged: if (pressed || hovered) sheet.splitScrollX = position * sheet.scrollWidth
        stepSize: sheet.scrollWidth > 0 ? 96 / sheet.scrollWidth : 0.1
    }
    Tk.ScrollBar {
        id: splitVBar
        objectName: "gridSplitVScroll"
        visible: sheet.splitBarY > 0
        anchors.top: parent.top
        anchors.right: parent.right
        height: sheet.splitBarY
        orientation: Qt.Vertical
        active: true
        policy: T.ScrollBar.AlwaysOn
        size: sheet.scrollHeight > 0 ? Math.min(1, sheet.splitY / sheet.scrollHeight) : 1
        position: sheet.scrollHeight > 0 ? sheet.splitScrollY / sheet.scrollHeight : 0
        onPositionChanged: if (pressed || hovered) sheet.splitScrollY = position * sheet.scrollHeight
        stepSize: sheet.scrollHeight > 0 ? 20 / sheet.scrollHeight : 0.1
    }

    // A chart sheet (M69, D-078): its chart in the grid's place, the size of
    // the window.
    Tk.Box {
        objectName: "chartSheetView"
        anchors.fill: parent
        visible: !root.viewOnly && root.host.chartSheetActive
        color: Tk.Theme.color.panel
        padding: Tk.Theme.space.lg
        ChartItem {
            objectName: "chartSheetChart"
            chartData: root.host.chartSheetData
            inkColor: Tk.Theme.color.text
        }
    }

    MenuFromNode {
        id: contextMenu
        objectName: "calcGridContextMenu"
        node: root.contextMenuNode
        actions: root.host.actions
    }
    Connections {
        target: root.host
        enabled: !root.viewOnly
        function onContextMenuRequested(x, y) {
            contextMenu.popup(sheet.mapToItem(root, x, y))
        }
    }
}
