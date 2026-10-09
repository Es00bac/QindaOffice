// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Table ▸ Split Cells (M67, D-076): into how many columns and rows. A
// merged block divides into parts of its own size; a single cell gains grid
// lines (show_model's TableEdit::split).
Tk.Dialog {
    id: dialog
    objectName: "splitCellsDialog"
    property int columns: 2
    property int rows: 1
    property int maxColumns: 20
    property int maxRows: 20
    signal splitChosen(int columns, int rows)
    title: qsTr("Split Cells")
    primaryText: qsTr("Split")
    primaryEnabled: dialog.columns * dialog.rows > 1
    onAccepted: dialog.splitChosen(dialog.columns, dialog.rows)
    function load(columns, rows, maxColumns, maxRows) {
        dialog.maxColumns = maxColumns
        dialog.maxRows = maxRows
        dialog.columns = columns
        dialog.rows = rows
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Number of columns")
            Tk.NumberField {
                objectName: "splitColumns"
                from: 1
                to: dialog.maxColumns
                stepSize: 1
                decimals: 0
                value: dialog.columns
                onValueModified: function(value) { dialog.columns = value }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Number of rows")
            Tk.NumberField {
                objectName: "splitRows"
                from: 1
                to: dialog.maxRows
                stepSize: 1
                decimals: 0
                value: dialog.rows
                onValueModified: function(value) { dialog.rows = value }
            }
        }
    }
}
