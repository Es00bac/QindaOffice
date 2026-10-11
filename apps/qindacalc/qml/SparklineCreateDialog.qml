// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Insert ▸ Sparklines' Create Sparklines and the Sparkline tab's Edit
// Sparklines (M90, D-098): the Data Range and the Location Range, Excel's
// two fields (Edit Single Sparkline's Data asks for the data only). OK
// answers through the host's sparklines controller; Excel's message
// reopens the dialog.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    // "create" with `type` (0 line, 1 column, 2 win/loss), "edit", "single".
    property string mode: "create"
    property int type: 0
    property string error: ""

    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "sparklineCreateDialog"
    title: dialog.mode === "create" ? qsTr("Create Sparklines") : qsTr("Edit Sparklines")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: dataField.text.trim().length > 0 && (dialog.mode === "single" || locationField.text.trim().length > 0)

    function load(mode, type) {
        dialog.mode = mode
        dialog.type = type
        const state = mode === "create" ? dialog.host.sparklines.createState() : dialog.host.sparklines.editState(mode === "single")
        dataField.text = state.data || ""
        locationField.text = state.location || ""
        dialog.error = ""
    }
    onOpened: (dialog.mode === "create" ? locationField : dataField).forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const problem = dialog.mode === "create"
            ? dialog.host.sparklines.create(dialog.type, dataField.text, locationField.text)
            : dialog.host.sparklines.applyEdit(dialog.mode === "single", dataField.text, locationField.text)
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "sparklineError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.Caption { text: qsTr("Choose the data that you want"); visible: dialog.mode === "create" }
        Tk.PropertyRow {
            label: qsTr("Data Range")
            Tk.TextField { id: dataField; objectName: "sparklineDataRange"; mono: true }
        }
        Tk.Caption { text: qsTr("Choose where you want the sparklines to be placed"); visible: dialog.mode !== "single" }
        Tk.PropertyRow {
            visible: dialog.mode !== "single"
            label: qsTr("Location Range")
            Tk.TextField { id: locationField; objectName: "sparklineLocationRange"; mono: true }
        }
    }
}
