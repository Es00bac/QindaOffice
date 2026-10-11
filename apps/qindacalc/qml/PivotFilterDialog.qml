// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// A PivotTable field's filter (M74, D-083), what Excel's filter dropdown
// offers: the item list with a search and Select All, a label filter
// (equals, begins with, contains, greater than, between …), a value filter
// over a data field, and Top 10 (top or bottom N items, percent or sum).
// Clear Filter is the third button.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property int field: -1
    property var state: ({})
    property var items: []

    readonly property var labelOps: [qsTr("equals"), qsTr("does not equal"), qsTr("is greater than"),
        qsTr("is greater than or equal to"), qsTr("is less than"), qsTr("is less than or equal to"),
        qsTr("begins with"), qsTr("does not begin with"), qsTr("ends with"), qsTr("does not end with"),
        qsTr("contains"), qsTr("does not contain"), qsTr("is between"), qsTr("is not between")]
    // The value filter offers the comparisons (PivotFilter::Op's first six and the two between).
    readonly property var valueOps: [0, 1, 2, 3, 4, 5, 12, 13]

    readonly property var dateTypes: ["dateBetween", "dateOlderThan", "dateNewerThan", "thisMonth", "lastMonth", "nextMonth",
        "thisQuarter", "lastQuarter", "nextQuarter", "thisYear", "lastYear", "nextYear", "yearToDate"]
    readonly property var dateLabels: [qsTr("Between"), qsTr("Before"), qsTr("After"), qsTr("This month"), qsTr("Last month"),
        qsTr("Next month"), qsTr("This quarter"), qsTr("Last quarter"), qsTr("Next quarter"), qsTr("This year"),
        qsTr("Last year"), qsTr("Next year"), qsTr("Year to date")]
    objectName: "pivotFilterDialog"
    title: qsTr("Filter “%1”").arg(dialog.state.name || "")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    tertiaryText: qsTr("Clear Filter")

    function load(index) {
        dialog.field = index
        dialog.state = dialog.host.pivotFilterState(index)
        dialog.items = (dialog.state.items || []).map(function(entry) { return Object.assign({}, entry) })
        kind.currentIndex = dialog.state.kind || 0
        dateOp.currentIndex = Math.max(0, dialog.dateTypes.indexOf(dialog.state.dateType))
        search.text = ""
        const op = dialog.state.op || 0
        labelOp.currentIndex = op
        valueOp.currentIndex = Math.max(0, dialog.valueOps.indexOf(op))
        text1.text = dialog.state.text1 || ""
        text2.text = dialog.state.text2 || ""
        dataField.currentIndex = dialog.state.dataField || 0
        topOrBottom.currentIndex = dialog.state.top === false ? 1 : 0
        topBy.currentIndex = dialog.state.topBy || 0
        if (kind.currentIndex === 3) count.text = dialog.state.text1 || "10"
        else count.text = "10"
    }
    function setAll(checked) {
        dialog.items = dialog.items.map(function(entry) { return Object.assign({}, entry, { checked: checked }) })
    }
    onAccepted: {
        const choice = { kind: kind.currentIndex, items: dialog.items }
        if (kind.currentIndex === 1) Object.assign(choice, { op: labelOp.currentIndex, text1: text1.text, text2: text2.text })
        if (kind.currentIndex === 2) Object.assign(choice, { op: dialog.valueOps[valueOp.currentIndex], text1: text1.text,
                                                             text2: text2.text, dataField: dataField.currentIndex })
        if (kind.currentIndex === 3) Object.assign(choice, { top: topOrBottom.currentIndex === 0, text1: count.text,
                                                             topBy: topBy.currentIndex, dataField: dataField.currentIndex })
        if (kind.currentIndex === 4) Object.assign(choice, { dateType: dialog.dateTypes[dateOp.currentIndex], text1: text1.text, text2: text2.text })
        dialog.host.applyPivotFilter(dialog.field, choice)
    }
    onTertiary: {
        dialog.setAll(true)
        dialog.host.applyPivotFilter(dialog.field, { kind: 0, items: dialog.items })
        dialog.close()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Segmented {
            id: kind
            objectName: "pivotFilterKind"
            model: dialog.state.page ? [qsTr("Items")] : [qsTr("Items"), qsTr("Label"), qsTr("Value"), qsTr("Top 10"), qsTr("Date")]
            tooltip: qsTr("What the filter keeps")
        }
        // ---- the item list ----
        Tk.SearchField {
            id: search
            objectName: "pivotFilterSearch"
            visible: kind.currentIndex === 0
            width: Tk.Theme.size.fieldWidth * 2
            placeholderText: qsTr("Search")
        }
        Tk.CheckBox {
            objectName: "pivotFilterAll"
            visible: kind.currentIndex === 0
            text: qsTr("(Select All)")
            checked: dialog.items.every(function(entry) { return entry.checked })
            onToggled: dialog.setAll(checked)
        }
        Tk.Scroll {
            visible: kind.currentIndex === 0
            implicitHeight: Math.min(Tk.Theme.size.panelMinWidth, itemColumn.implicitHeight)
            Tk.Flex {
                id: itemColumn
                direction: Tk.Flex.Column
                Repeater {
                    model: dialog.items
                    Tk.CheckBox {
                        required property var modelData
                        required property int index
                        objectName: "pivotFilterItem_" + index
                        visible: search.text.length === 0
                                 || String(modelData.caption).toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                        text: modelData.caption
                        checked: modelData.checked
                        onToggled: {
                            const next = dialog.items.slice()
                            next[index] = Object.assign({}, next[index], { checked: checked })
                            dialog.items = next
                        }
                    }
                }
            }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 4
            label: qsTr("Show dates")
            Tk.ComboBox { id: dateOp; objectName: "pivotFilterDateOp"; model: dialog.dateLabels }
        }
        // ---- label, value, date and Top 10 filters ----
        Tk.PropertyRow {
            visible: kind.currentIndex === 2 || kind.currentIndex === 3
            label: qsTr("Data field")
            Tk.ComboBox { id: dataField; objectName: "pivotFilterDataField"; width: Tk.Theme.size.fieldWidth * 2; model: dialog.state.dataFields || [] }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 1
            label: qsTr("Show items whose label")
            Tk.ComboBox { id: labelOp; objectName: "pivotFilterLabelOp"; width: Tk.Theme.size.fieldWidth * 2; model: dialog.labelOps }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 2
            label: qsTr("Show items whose value")
            Tk.ComboBox {
                id: valueOp
                objectName: "pivotFilterValueOp"
                width: Tk.Theme.size.fieldWidth * 2
                model: dialog.valueOps.map(function(op) { return dialog.labelOps[op] })
            }
        }
        Tk.PropertyRow {
            visible: kind.currentIndex === 1 || kind.currentIndex === 2 || (kind.currentIndex === 4 && dateOp.currentIndex < 3)
            label: kind.currentIndex === 4 ? qsTr("Date (YYYY-MM-DD)") : qsTr("Value")
            Tk.TextField { id: text1; objectName: "pivotFilterText1"; width: Tk.Theme.size.fieldWidth * 2; placeholderText: qsTr("Use ? and * as wildcards") }
        }
        Tk.PropertyRow {
            visible: (kind.currentIndex === 1 && labelOp.currentIndex >= 12) || (kind.currentIndex === 2 && valueOp.currentIndex >= 6) || (kind.currentIndex === 4 && dateOp.currentIndex === 0)
            label: qsTr("and")
            Tk.TextField { id: text2; objectName: "pivotFilterText2"; width: Tk.Theme.size.fieldWidth * 2 }
        }
        Tk.Flex {
            visible: kind.currentIndex === 3
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.ComboBox { id: topOrBottom; objectName: "pivotFilterTop"; width: Tk.Theme.size.fieldWidth; model: [qsTr("Top"), qsTr("Bottom")] }
            Tk.TextField { id: count; objectName: "pivotFilterCount"; width: Tk.Theme.size.fieldWidth / 2 }
            Tk.ComboBox { id: topBy; objectName: "pivotFilterTopBy"; width: Tk.Theme.size.fieldWidth; model: [qsTr("Items"), qsTr("Percent"), qsTr("Sum")] }
        }
    }
}
