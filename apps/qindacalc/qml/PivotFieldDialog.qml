// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Field Settings (M74, D-083), Excel's dialog for a row, column or filter
// field: the custom name; Subtotals — Automatic, None or Custom with any of
// the eleven functions; Layout & Print — subtotals at the top, item labels
// repeated, a blank line after each item, items with no data; and the
// sort — by hand, A to Z or Z to A, by the labels or by a data field.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property int field: -1
    property var state: ({})

    objectName: "pivotFieldDialog"
    title: qsTr("Field Settings")
    subtitle: dialog.state.source ? qsTr("Source Name: %1").arg(dialog.state.source) : ""
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")

    function load(index) {
        dialog.field = index
        dialog.state = dialog.host.pivotFieldState(index)
        caption.text = dialog.state.caption || ""
        automatic.checked = dialog.state.subtotals === 0
        none.checked = dialog.state.subtotals === 1
        custom.checked = dialog.state.subtotals === 2
        for (let i = 0; i < functions.count; ++i) functions.itemAt(i).checked = (dialog.state.custom || [])[i] === true
        top.checked = dialog.state.subtotalTop === true
        repeat.checked = dialog.state.repeatLabels === true
        blank.checked = dialog.state.blankLine === true
        showAll.checked = dialog.state.showAll === true
        sort.currentIndex = dialog.state.sort || 0
        sortBy.currentIndex = dialog.state.sortBy || 0
    }
    onOpened: caption.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const chosen = []
        for (let i = 0; i < functions.count; ++i) chosen.push(functions.itemAt(i).checked)
        dialog.host.applyFieldSettings(dialog.field, {
            caption: caption.text,
            subtotals: automatic.checked ? 0 : (none.checked ? 1 : 2),
            custom: chosen,
            subtotalTop: top.checked,
            repeatLabels: repeat.checked,
            blankLine: blank.checked,
            showAll: showAll.checked,
            sort: sort.currentIndex,
            sortBy: sortBy.currentIndex
        })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Custom Name")
            Tk.TextField { id: caption; objectName: "pivotFieldCaption"; width: Tk.Theme.size.fieldWidth * 2 }
        }
        Tk.SectionHeader { title: qsTr("Subtotals") }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.RadioButton {
                id: automatic; objectName: "pivotFieldSubtotalAutomatic"; text: qsTr("Automatic")
                onToggled: if (checked) { none.checked = false; custom.checked = false }
            }
            Tk.RadioButton {
                id: none; objectName: "pivotFieldSubtotalNone"; text: qsTr("None")
                onToggled: if (checked) { automatic.checked = false; custom.checked = false }
            }
            Tk.RadioButton {
                id: custom; objectName: "pivotFieldSubtotalCustom"; text: qsTr("Custom")
                onToggled: if (checked) { automatic.checked = false; none.checked = false }
            }
        }
        Tk.Grid {
            visible: custom.checked
            columns: "1fr 1fr 1fr"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.xs
            Repeater {
                id: functions
                model: dialog.state.functions || []
                Tk.CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "pivotFieldFunction_" + index
                    text: modelData
                    small: true
                }
            }
        }
        Tk.SectionHeader { title: qsTr("Layout & Print") }
        Tk.CheckBox { id: top; objectName: "pivotFieldSubtotalTop"; text: qsTr("Display subtotals at the top of each group") }
        Tk.CheckBox { id: repeat; objectName: "pivotFieldRepeat"; text: qsTr("Repeat item labels") }
        Tk.CheckBox { id: blank; objectName: "pivotFieldBlankLine"; text: qsTr("Insert blank line after each item label") }
        Tk.CheckBox { id: showAll; objectName: "pivotFieldShowAll"; text: qsTr("Show items with no data") }
        Tk.SectionHeader { title: qsTr("Sort") }
        Tk.PropertyRow {
            label: qsTr("Order")
            Tk.ComboBox {
                id: sort
                objectName: "pivotFieldSort"
                width: Tk.Theme.size.fieldWidth * 2
                model: [qsTr("Manual (drag items to rearrange them)"), qsTr("Ascending (A to Z)"), qsTr("Descending (Z to A)")]
            }
        }
        Tk.PropertyRow {
            visible: sort.currentIndex > 0
            label: qsTr("By")
            Tk.ComboBox {
                id: sortBy
                objectName: "pivotFieldSortBy"
                width: Tk.Theme.size.fieldWidth * 2
                model: [qsTr("The item labels")].concat(dialog.state.dataFields || [])
            }
        }
    }
}
