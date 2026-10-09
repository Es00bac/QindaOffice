// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ What-If Analysis ▸ Data Table (M78, D-087): Row input cell and
// Column input cell for the selected table ({=TABLE(r,c)}). One input cell
// makes a one-variable table, both a two-variable one; the results fill
// the table and follow its formulas from then on. Excel's refusals reopen
// the dialog with the reason.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property string selection: ""
    property string error: ""

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "dataTableDialog"
    title: qsTr("Data Table")
    subtitle: dialog.selection
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: rowInput.text.trim().length > 0 || colInput.text.trim().length > 0

    function load(selection) {
        dialog.selection = selection
        rowInput.text = ""
        colInput.text = ""
        dialog.error = ""
    }
    onOpened: rowInput.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const problem = dialog.host.createDataTable(rowInput.text, colInput.text)
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "dataTableError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Row input cell")
            Tk.TextField { id: rowInput; objectName: "dataTableRowInput"; mono: true }
        }
        Tk.PropertyRow {
            label: qsTr("Column input cell")
            Tk.TextField { id: colInput; objectName: "dataTableColumnInput"; mono: true }
        }
        Tk.Caption {
            muted: true
            wrapMode: Text.WordWrap
            text: qsTr("Select the whole table first: the formula row or column, the input values and the cells for the results.")
        }
    }
}
