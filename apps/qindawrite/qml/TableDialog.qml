// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Table (rows × columns) and Insert ▸ Columns (a borderless
// 1×N table with a gutter), one dialog in two modes.
Tk.Dialog {
    id: dialog
    property bool columnsMode: false
    signal tableApplied(int rows, int columns)
    signal columnsApplied(int columns, int gutter)
    objectName: "tableDialog"
    title: columnsMode ? qsTr("Insert Columns") : qsTr("Insert Table")
    dialogWidth: 340
    primaryText: qsTr("Insert")
    onAccepted: {
        if (columnsMode) dialog.columnsApplied(Math.round(columns.value), Math.round(gutter.value))
        else dialog.tableApplied(Math.round(rows.value), Math.round(cols.value))
    }
    onOpened: (columnsMode ? columns : rows).forceActiveFocus(Qt.TabFocusReason)
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { visible: !dialog.columnsMode; label: qsTr("Rows"); Tk.NumberField { id: rows; objectName: "tableRows"; from: 1; to: 200; value: 3; decimals: 0 } }
        Tk.PropertyRow { visible: !dialog.columnsMode; label: qsTr("Columns"); Tk.NumberField { id: cols; objectName: "tableColumns"; from: 1; to: 20; value: 3; decimals: 0 } }
        Tk.PropertyRow { visible: dialog.columnsMode; label: qsTr("Columns"); Tk.NumberField { id: columns; objectName: "textColumns"; from: 2; to: 4; value: 2; decimals: 0 } }
        Tk.PropertyRow { visible: dialog.columnsMode; label: qsTr("Gutter"); Tk.NumberField { id: gutter; objectName: "textGutter"; from: 0; to: 96; value: 24; decimals: 0; suffix: " px" } }
    }
}
