// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Table (M67, D-076): PowerPoint's Insert Table dialog. The table
// lands in the deck's default table style (ShowHost::insertTable).
Tk.Dialog {
    id: dialog
    objectName: "insertTableDialog"
    property int columns: 5
    property int rows: 2
    signal tableChosen(int rows, int columns)
    title: qsTr("Insert Table")
    primaryText: qsTr("Insert")
    onAccepted: dialog.tableChosen(dialog.rows, dialog.columns)
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Number of columns")
            Tk.NumberField {
                objectName: "insertTableColumns"
                from: 1
                to: 63
                stepSize: 1
                decimals: 0
                value: dialog.columns
                onValueModified: function(value) { dialog.columns = value }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number of rows")
            Tk.NumberField {
                objectName: "insertTableRows"
                from: 1
                to: 100
                stepSize: 1
                decimals: 0
                value: dialog.rows
                onValueModified: function(value) { dialog.rows = value }
            }
        }
    }
}
