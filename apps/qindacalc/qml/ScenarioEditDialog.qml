// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Add Scenario / Edit Scenario with its Scenario Values (M78, D-087): the
// name, the changing cells ($B$1,$B$2 — at most 32), the comment ("Created
// by … on …"), Prevent changes and Hide (in force while the sheet is
// protected), and one value per changing cell, filled from the cells as
// they are now. Excel's refusals reopen the dialog with the reason.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property int index: -1
    property var values: []
    property string error: ""

    signal saved()

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "scenarioEditDialog"
    title: dialog.index < 0 ? qsTr("Add Scenario") : qsTr("Edit Scenario")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.1
    primaryText: qsTr("OK")
    primaryEnabled: name.text.trim().length > 0 && cells.text.trim().length > 0

    function load(index) {
        dialog.index = index
        const state = dialog.host.scenarioState(index)
        name.text = state.name || ""
        cells.text = state.cells || ""
        comment.text = state.comment || ""
        locked.checked = state.locked !== false
        hidden.checked = state.hidden === true
        dialog.values = state.values || []
        dialog.error = ""
    }
    function refreshValues() {
        const fresh = dialog.host.scenarioCellValues(cells.text)
        if (fresh.length !== dialog.values.length) dialog.values = fresh
    }
    onOpened: name.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const problem = dialog.host.saveScenario(dialog.index, {
            name: name.text, cells: cells.text, comment: comment.text,
            locked: locked.checked, hidden: hidden.checked, values: dialog.values
        })
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
            return
        }
        dialog.saved()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "scenarioError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Scenario name")
            Tk.TextField { id: name; objectName: "scenarioName" }
        }
        Tk.PropertyRow {
            label: qsTr("Changing cells")
            Tk.TextField { id: cells; objectName: "scenarioCells"; mono: true; onEditingFinished: dialog.refreshValues() }
        }
        Tk.PropertyRow {
            label: qsTr("Comment")
            alignTop: true
            Tk.TextArea { id: comment; objectName: "scenarioCommentField"; implicitHeight: Tk.Theme.size.row * 2 }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.CheckBox { id: locked; objectName: "scenarioLocked"; text: qsTr("Prevent changes") }
            Tk.CheckBox { id: hidden; objectName: "scenarioHidden"; text: qsTr("Hide") }
        }
        Tk.SectionHeader { title: qsTr("Scenario values") }
        Tk.Scroll {
            implicitHeight: Math.min(Tk.Theme.size.row * 5, valueRows.implicitHeight)
            Tk.Flex {
                id: valueRows
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: dialog.values.length
                    Tk.PropertyRow {
                        id: valueRow
                        required property int index
                        label: (cells.text.split(/[,;]/)[valueRow.index] || "").trim() || qsTr("Cell %1").arg(valueRow.index + 1)
                        Tk.TextField {
                            objectName: "scenarioValue_" + valueRow.index
                            mono: true
                            text: dialog.values[valueRow.index] || ""
                            onTextEdited: {
                                const next = dialog.values.slice()
                                next[valueRow.index] = text
                                dialog.values = next
                            }
                        }
                    }
                }
            }
        }
    }
}
