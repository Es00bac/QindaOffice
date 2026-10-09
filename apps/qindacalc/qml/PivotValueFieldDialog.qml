// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Value Field Settings (M74, D-083), Excel's dialog: the source field, the
// custom name, Summarize Values By (Sum … Varp), Show Values As with its
// base field and base item where the calculation needs them, and the
// number format the values take.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property int dataIndex: 0
    property var state: ({})
    property var baseItems: []
    // Which Show Values As calculations read a base field / a base item.
    readonly property var needsField: [4, 7, 8, 9, 10, 11, 12, 13]
    readonly property var needsItem: [4, 8, 9]
    readonly property var formats: ["General", "0", "0.00", "#,##0", "#,##0.00", "0%", "0.00%", "$#,##0.00",
                                    "m/d/yyyy", "0.00E+00"]

    objectName: "pivotValueFieldDialog"
    title: qsTr("Value Field Settings")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")

    function load(index) {
        dialog.dataIndex = index
        dialog.state = dialog.host.pivotValueFieldState(index)
        name.text = dialog.state.name || ""
        summarize.currentIndex = dialog.state.function || 0
        showAs.currentIndex = dialog.state.showAs || 0
        baseField.currentIndex = dialog.state.baseField || 0
        dialog.loadItems()
        baseItem.currentIndex = dialog.state.baseItem || 0
        const code = dialog.state.numberFormat || "General"
        format.currentIndex = Math.max(0, dialog.formats.indexOf(code))
        format.editText = code
    }
    function loadItems() {
        const bases = dialog.state.baseFields || []
        const field = bases.length > 0 ? bases[Math.max(0, baseField.currentIndex)].field : -1
        dialog.baseItems = dialog.host.pivotBaseItems(field)
    }
    onOpened: name.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: dialog.host.applyValueFieldSettings(dialog.dataIndex, {
        name: name.text,
        function: summarize.currentIndex,
        showAs: showAs.currentIndex,
        baseField: baseField.currentIndex,
        baseItem: baseItem.currentIndex,
        numberFormat: format.editText.length > 0 ? format.editText : dialog.formats[format.currentIndex]
    })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Source Name")
            Tk.Label { objectName: "pivotValueSource"; text: dialog.state.source || "" }
        }
        Tk.PropertyRow {
            label: qsTr("Custom Name")
            Tk.TextField { id: name; objectName: "pivotValueName"; width: Tk.Theme.size.fieldWidth * 2 }
        }
        Tk.SectionHeader { title: qsTr("Summarize Values By") }
        Tk.PropertyRow {
            label: qsTr("Calculation")
            Tk.ComboBox {
                id: summarize
                objectName: "pivotValueFunction"
                width: Tk.Theme.size.fieldWidth * 2
                model: dialog.state.functions || []
                tooltip: qsTr("The type of calculation that summarizes the field")
            }
        }
        Tk.SectionHeader { title: qsTr("Show Values As") }
        Tk.PropertyRow {
            label: qsTr("Show values as")
            Tk.ComboBox {
                id: showAs
                objectName: "pivotValueShowAs"
                width: Tk.Theme.size.fieldWidth * 2
                model: dialog.state.showAsNames || []
            }
        }
        Tk.PropertyRow {
            visible: dialog.needsField.indexOf(showAs.currentIndex) >= 0
            label: qsTr("Base field")
            Tk.ComboBox {
                id: baseField
                objectName: "pivotValueBaseField"
                width: Tk.Theme.size.fieldWidth * 2
                model: (dialog.state.baseFields || []).map(function(entry) { return entry.name })
                onActivated: dialog.loadItems()
            }
        }
        Tk.PropertyRow {
            visible: dialog.needsItem.indexOf(showAs.currentIndex) >= 0
            label: qsTr("Base item")
            Tk.ComboBox {
                id: baseItem
                objectName: "pivotValueBaseItem"
                width: Tk.Theme.size.fieldWidth * 2
                model: dialog.baseItems
            }
        }
        Tk.SectionHeader { title: qsTr("Number Format") }
        Tk.PropertyRow {
            label: qsTr("Format code")
            Tk.ComboBox {
                id: format
                objectName: "pivotValueFormat"
                width: Tk.Theme.size.fieldWidth * 2
                editable: true
                model: dialog.formats
                tooltip: qsTr("An Excel number format, like #,##0.00 or 0%")
            }
        }
    }
}
