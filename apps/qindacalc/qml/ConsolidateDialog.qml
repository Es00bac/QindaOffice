// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ Consolidate (M78, D-087): the function (Sum … Varp), the
// references (Add / Delete; Sheet2!$A$1:$C$9), Use labels in Top row /
// Left column, and where the result goes (the cursor by default). One undo
// step; a reference the host cannot read reopens the dialog with it.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var state: ({})
    property var references: []
    property int selected: -1
    property string error: ""

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "consolidateDialog"
    title: qsTr("Consolidate")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.1
    primaryText: qsTr("OK")
    primaryEnabled: dialog.references.length > 0

    function load() {
        dialog.state = dialog.host.consolidateState()
        reference.text = dialog.state.reference || ""
        destination.text = dialog.state.destination || ""
        dialog.error = ""
    }
    function add() {
        const text = reference.text.trim()
        if (text.length > 0 && dialog.references.indexOf(text) < 0) dialog.references = dialog.references.concat([text])
    }
    onAccepted: {
        const problem = dialog.host.applyConsolidate({
            function: fn.currentIndex, references: dialog.references, topRow: topRow.checked,
            leftColumn: leftColumn.checked, destination: destination.text
        })
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "consolidateError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Function")
            Tk.ComboBox { id: fn; objectName: "consolidateFunction"; model: dialog.state.functions || [] }
        }
        Tk.PropertyRow {
            label: qsTr("Reference")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.TextField { id: reference; objectName: "consolidateReference"; mono: true; Tk.Flex.grow: 1 }
                Tk.Button { objectName: "consolidateAdd"; small: true; text: qsTr("Add"); onClicked: dialog.add() }
            }
        }
        Tk.SectionHeader { title: qsTr("All references") }
        Tk.Scroll {
            implicitHeight: Tk.Theme.size.row * 4
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.references
                    Tk.ListRow {
                        required property string modelData
                        required property int index
                        objectName: "consolidateRef_" + index
                        text: modelData
                        selected: dialog.selected === index
                        onClicked: dialog.selected = index
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Spacer { }
            Tk.Button {
                objectName: "consolidateDelete"; small: true; text: qsTr("Delete"); available: dialog.selected >= 0
                onClicked: { dialog.references = dialog.references.filter(function(r, i) { return i !== dialog.selected }); dialog.selected = -1 }
            }
        }
        Tk.SectionHeader { title: qsTr("Use labels in") }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.CheckBox { id: topRow; objectName: "consolidateTopRow"; text: qsTr("Top row") }
            Tk.CheckBox { id: leftColumn; objectName: "consolidateLeftColumn"; text: qsTr("Left column") }
        }
        Tk.PropertyRow {
            label: qsTr("Put the result at")
            Tk.TextField { id: destination; objectName: "consolidateDestination"; mono: true }
        }
    }
}
