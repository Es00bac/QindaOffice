// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Insert ▸ PivotTable (M74, D-083), and the same dialog cut down for
// PivotTable Analyze ▸ Change Data Source (the source only) and Move
// PivotTable (the place only): Excel's "Select a table or range" — a range
// such as Data!$A$1:$E$7 or a table's name, prefilled from the cursor —
// and "Choose where you want the PivotTable report to be placed".
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property string mode: "create" // "create", "source", "move"

    objectName: "pivotCreateDialog"
    title: mode === "source" ? qsTr("Change PivotTable Data Source")
         : mode === "move" ? qsTr("Move PivotTable") : qsTr("Create PivotTable")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: (mode === "move" || source.text.trim().length > 0)
                    && (mode === "source" || newSheet.checked || location.text.trim().length > 0)

    function load(kind) {
        dialog.mode = kind
        const state = dialog.host.pivotCreateState()
        source.text = state.source
        location.text = state.location
        newSheet.checked = kind === "create" && state.newSheet
        existing.checked = !newSheet.checked
    }
    onOpened: (dialog.mode === "move" ? location : source).forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        if (dialog.mode === "source") dialog.host.changePivotSourceTo(source.text)
        else if (dialog.mode === "move") dialog.host.movePivotTo(location.text)
        else dialog.host.insertPivotTableFrom({ source: source.text, newSheet: newSheet.checked, location: location.text })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption {
            visible: dialog.mode !== "move"
            text: qsTr("Choose the data that you want to analyze")
        }
        Tk.PropertyRow {
            visible: dialog.mode !== "move"
            label: qsTr("Table/Range")
            Tk.TextField {
                id: source
                objectName: "pivotCreateSource"
                width: Tk.Theme.size.fieldWidth * 2
                placeholderText: qsTr("Data!$A$1:$E$20, or a table's name")
                tooltip: qsTr("The cells to summarize, their first row the field names")
            }
        }
        Tk.Caption {
            visible: dialog.mode !== "source"
            text: qsTr("Choose where you want the PivotTable report to be placed")
        }
        Tk.RadioButton {
            id: newSheet
            objectName: "pivotCreateNewSheet"
            visible: dialog.mode === "create"
            text: qsTr("New Worksheet")
            onToggled: if (checked) existing.checked = false
        }
        Tk.RadioButton {
            id: existing
            objectName: "pivotCreateExisting"
            visible: dialog.mode === "create"
            text: qsTr("Existing Worksheet")
            onToggled: if (checked) newSheet.checked = false
        }
        Tk.PropertyRow {
            visible: dialog.mode !== "source" && !newSheet.checked
            label: qsTr("Location")
            Tk.TextField {
                id: location
                objectName: "pivotCreateLocation"
                width: Tk.Theme.size.fieldWidth * 2
                placeholderText: qsTr("Sheet1!$H$3")
                tooltip: qsTr("The top-left cell of the PivotTable")
            }
        }
    }
}
