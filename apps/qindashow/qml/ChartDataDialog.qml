// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Edit Data for a chart (M67, D-076): the chart's data sheet — series
// names across the first row, category labels down the first column, the
// values between, lettered as the embedded workbook PowerPoint opens lays
// them out — and the chart's kind. OK writes both as one undo step; the
// saved .pptx carries them in the chart part and its workbook alike.
Tk.Dialog {
    id: dialog
    objectName: "chartDataDialog"
    property ChartDataEditor editor: null
    signal dataApplied()
    title: qsTr("Edit Chart Data")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")
    onAccepted: {
        if (grid.cellEditing) grid.commitCellEdit(grid.editText)
        dialog.dataApplied()
    }
    function letters(index) {
        let name = ""
        for (let n = index + 1; n > 0; n = Math.floor((n - 1) / 26)) name = String.fromCharCode(65 + (n - 1) % 26) + name
        return name
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Chart type")
            Tk.ComboBox {
                objectName: "chartKindCombo"
                model: dialog.editor !== null ? dialog.editor.kindNames : []
                currentIndex: dialog.editor !== null ? dialog.editor.kindIndex : 0
                onActivated: function(index) { dialog.editor.kindIndex = index }
            }
        }
        Tk.EditableGrid {
            id: grid
            objectName: "chartDataGrid"
            Tk.Flex.shrink: 0
            implicitWidth: Tk.Theme.size.dialogWidth * 1.3
            implicitHeight: Tk.Theme.size.row * 9
            model: dialog.editor !== null ? dialog.editor.grid : null
            tooltip: qsTr("Chart data")
            fillHandleEnabled: false
            columnLabel: function(id, index) { return dialog.letters(index) }
            rowLabel: function(id, index) { return String(index + 1) }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "chartAddCategory"
                text: qsTr("Add category")
                iconName: "between-horizontal-end"
                onClicked: dialog.editor.addCategory()
            }
            Tk.Button {
                objectName: "chartAddSeries"
                text: qsTr("Add series")
                iconName: "between-vertical-end"
                onClicked: dialog.editor.addSeries()
            }
            Tk.Button {
                objectName: "chartDeleteCategory"
                text: qsTr("Delete category")
                enabled: dialog.editor !== null && grid.activeRow >= 1 && dialog.editor.categoryCount > 1
                onClicked: dialog.editor.removeCategory(grid.activeRow)
            }
            Tk.Button {
                objectName: "chartDeleteSeries"
                text: qsTr("Delete series")
                enabled: dialog.editor !== null && grid.activeColumn >= 1 && dialog.editor.seriesCount > 1
                onClicked: dialog.editor.removeSeries(grid.activeColumn)
            }
        }
    }
}
