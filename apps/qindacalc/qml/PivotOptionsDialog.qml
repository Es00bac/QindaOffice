// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// PivotTable Options (M74, D-083), the parts of Excel's dialog QindaCalc
// models: the PivotTable's name, the report layout, "For empty cells show",
// the grand totals for rows (the column of row totals) and for columns
// (the row of column totals), and the field captions and filter buttons.
Tk.Dialog {
    id: dialog

    required property CalcHost host

    objectName: "pivotOptionsDialog"
    title: qsTr("PivotTable Options")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: name.text.trim().length > 0

    function load() {
        const state = dialog.host.pivotOptionsState()
        name.text = state.name || ""
        form.currentIndex = state.form || 0
        showMissing.checked = state.showMissing === true
        missing.text = state.missingCaption || ""
        rowGrand.checked = state.rowGrand === true
        colGrand.checked = state.colGrand === true
        headers.checked = state.showHeaders !== false
    }
    onOpened: name.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: dialog.host.applyPivotOptions({
        name: name.text,
        form: form.currentIndex,
        showMissing: showMissing.checked,
        missingCaption: missing.text,
        rowGrand: rowGrand.checked,
        colGrand: colGrand.checked,
        showHeaders: headers.checked
    })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Name")
            Tk.TextField { id: name; objectName: "pivotOptionsName" }
        }
        Tk.SectionHeader { title: qsTr("Layout & Format") }
        Tk.PropertyRow {
            label: qsTr("Report layout")
            Tk.ComboBox {
                id: form
                objectName: "pivotOptionsForm"
                model: [qsTr("Compact Form"), qsTr("Outline Form"), qsTr("Tabular Form")]
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.CheckBox { id: showMissing; objectName: "pivotOptionsShowMissing"; text: qsTr("For empty cells show") }
            Tk.TextField { id: missing; objectName: "pivotOptionsMissing"; enabled: showMissing.checked; Tk.Flex.grow: 1 }
        }
        Tk.SectionHeader { title: qsTr("Totals & Filters") }
        Tk.CheckBox { id: rowGrand; objectName: "pivotOptionsRowGrand"; text: qsTr("Show grand totals for rows") }
        Tk.CheckBox { id: colGrand; objectName: "pivotOptionsColumnGrand"; text: qsTr("Show grand totals for columns") }
        Tk.SectionHeader { title: qsTr("Display") }
        Tk.CheckBox { id: headers; objectName: "pivotOptionsHeaders"; text: qsTr("Display field captions and filter drop downs") }
    }
}
