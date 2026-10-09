// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Edit Recipient List ▸ Filter and Sort (M77, D-086): Word's Filter Records
// (up to four rows: And / Or, field, comparison, compare to) and Sort Records
// (up to three keys, ascending or descending). Saved in the merge's query
// as Word writes it (WHERE … ORDER BY …, docx_mailmerge.h).
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var fields: []
    readonly property var ops: ["equal", "notEqual", "less", "greater", "lessEqual", "greaterEqual", "blank",
                                "notBlank", "contains", "notContains"]
    objectName: "filterSortDialog"
    title: qsTr("Filter and Sort")
    dialogWidth: 640
    primaryText: qsTr("OK")

    function load() {
        const info = dialog.mailMerge.filterSort()
        fields = [""].concat(info.fields)
        for (let i = 0; i < filterRows.count; ++i) {
            const row = filterRows.itemAt(i)
            const f = i < info.filters.length ? info.filters[i] : { join: "and", field: "", op: "equal", value: "" }
            row.join = f.join === "or" ? 1 : 0
            row.field = Math.max(0, fields.indexOf(f.field))
            row.op = Math.max(0, ops.indexOf(f.op))
            row.value = f.value
        }
        for (let k = 0; k < sortRows.count; ++k) {
            const key = sortRows.itemAt(k)
            const s = k < info.sorts.length ? info.sorts[k] : { field: "", descending: false }
            key.field = Math.max(0, fields.indexOf(s.field))
            key.descending = s.descending
        }
        open()
    }
    onAccepted: {
        const filters = []
        for (let i = 0; i < filterRows.count; ++i) {
            const row = filterRows.itemAt(i)
            if (row.field > 0)
                filters.push({ join: row.join === 1 ? "or" : "and", field: fields[row.field], op: ops[row.op], value: row.value })
        }
        const sorts = []
        for (let k = 0; k < sortRows.count; ++k) {
            const key = sortRows.itemAt(k)
            if (key.field > 0) sorts.push({ field: fields[key.field], descending: key.descending })
        }
        dialog.mailMerge.setFilterSort({ filters: filters, sorts: sorts })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { text: qsTr("Filter records") }
        Repeater {
            id: filterRows
            model: 4
            delegate: Tk.Flex {
                id: filterRow
                required property int index
                property int join: 0
                property int field: 0
                property int op: 0
                property string value: ""
                gap: Tk.Theme.space.xs
                Tk.ComboBox {
                    objectName: "filterJoin" + filterRow.index
                    small: true
                    implicitWidth: 70
                    enabled: filterRow.index > 0
                    model: [qsTr("And"), qsTr("Or")]
                    currentIndex: filterRow.join
                    onActivated: function(i) { filterRow.join = i }
                }
                Tk.ComboBox {
                    objectName: "filterField" + filterRow.index
                    small: true
                    implicitWidth: 170
                    model: dialog.fields.map(function(f) { return f.length > 0 ? f : qsTr("(none)") })
                    currentIndex: filterRow.field
                    onActivated: function(i) { filterRow.field = i }
                }
                Tk.ComboBox {
                    objectName: "filterOp" + filterRow.index
                    small: true
                    implicitWidth: 170
                    model: [qsTr("Equal to"), qsTr("Not equal to"), qsTr("Less than"), qsTr("Greater than"),
                            qsTr("Less than or equal"), qsTr("Greater than or equal"), qsTr("Is blank"),
                            qsTr("Is not blank"), qsTr("Contains"), qsTr("Does not contain")]
                    currentIndex: filterRow.op
                    onActivated: function(i) { filterRow.op = i }
                }
                Tk.TextField {
                    objectName: "filterValue" + filterRow.index
                    small: true
                    implicitWidth: 160
                    enabled: filterRow.op !== 6 && filterRow.op !== 7
                    text: filterRow.value
                    onTextChanged: filterRow.value = text
                }
            }
        }
        Tk.Overline { text: qsTr("Sort records") }
        Repeater {
            id: sortRows
            model: 3
            delegate: Tk.Flex {
                id: sortRow
                required property int index
                property int field: 0
                property bool descending: false
                gap: Tk.Theme.space.xs
                Tk.Label { text: sortRow.index === 0 ? qsTr("Sort by") : qsTr("Then by"); width: 70 }
                Tk.ComboBox {
                    objectName: "sortField" + sortRow.index
                    small: true
                    implicitWidth: 200
                    model: dialog.fields.map(function(f) { return f.length > 0 ? f : qsTr("(none)") })
                    currentIndex: sortRow.field
                    onActivated: function(i) { sortRow.field = i }
                }
                Tk.Segmented {
                    objectName: "sortDirection" + sortRow.index
                    small: true
                    model: [qsTr("Ascending"), qsTr("Descending")]
                    currentIndex: sortRow.descending ? 1 : 0
                    onActivated: function(i) { sortRow.descending = i === 1 }
                }
            }
        }
    }
}
