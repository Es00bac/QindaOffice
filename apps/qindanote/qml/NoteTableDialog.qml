// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Table (M84, D-093): a table of rows and columns in the open
// note (or a new one). Tab moves between its cells; the Table tab adds and
// deletes rows and columns.
Tk.Dialog {
    id: dialog
    signal tableApplied(int rows, int columns)
    objectName: "noteTableDialog"
    title: qsTr("Insert Table")
    dialogWidth: 320
    primaryText: qsTr("Insert")
    onAccepted: dialog.tableApplied(Math.round(rowsField.value), Math.round(columnsField.value))
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Columns")
            Tk.NumberField { id: columnsField; objectName: "noteTableColumns"; value: 3; from: 1; to: 32 }
        }
        Tk.PropertyRow {
            label: qsTr("Rows")
            Tk.NumberField { id: rowsField; objectName: "noteTableRows"; value: 2; from: 1; to: 100 }
        }
    }
}
