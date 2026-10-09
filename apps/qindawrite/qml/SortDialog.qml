// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Table ▸ Sort (M70, D-079): Word's Sort for a table — the column to sort
// by (its heading when the list has a header row), the type (text, number,
// date), ascending or descending, and whether the first row is a header
// that stays put. Sorts the selected rows, or the whole table from one
// cell; whole rows move with their formatting (table_data.h).
Tk.Dialog {
    id: dialog
    signal sortApplied(var answer)
    property var columns: []
    property var headings: []
    objectName: "sortDialog"
    title: qsTr("Sort")
    dialogWidth: 420
    primaryText: qsTr("Sort")

    function load(info) {
        columns = info.columns
        headings = info.headings
        header.checked = info.header
        column.currentIndex = Math.max(0, info.column)
        type.currentIndex = 0
        order.currentIndex = 0
    }
    onAccepted: dialog.sortApplied({ column: column.currentIndex, type: ["text", "number", "date"][type.currentIndex],
                                     descending: order.currentIndex === 1, header: header.checked })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Sort by")
            Tk.ComboBox {
                id: column
                objectName: "sortColumn"
                model: header.checked ? dialog.headings : dialog.columns
            }
        }
        Tk.PropertyRow {
            label: qsTr("Type")
            Tk.Segmented {
                id: type
                objectName: "sortType"
                model: [qsTr("Text"), qsTr("Number"), qsTr("Date")]
            }
        }
        Tk.PropertyRow {
            label: qsTr("Order")
            Tk.Segmented {
                id: order
                objectName: "sortOrder"
                model: [qsTr("Ascending"), qsTr("Descending")]
            }
        }
        Tk.CheckBox {
            id: header
            objectName: "sortHeader"
            text: qsTr("My list has a header row")
        }
    }
}
