// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data validation for the selection: the kind (or "Any value", which
// removes the rule), bounds, a list, the message shown on refusal. Fields
// that mean nothing for the chosen kind are disabled, not hidden, so the
// dialog does not jump about.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property string range: ""

    objectName: "validationDialog"
    title: qsTr("Data validation")
    subtitle: qsTr("Applies to %1").arg(range)
    dialogWidth: 420
    primaryText: qsTr("Apply")

    function load() {
        const rule = dialog.host.validationAtCursor()
        dialog.range = rule.range
        kind.currentIndex = rule.kind + 1
        min.value = rule.min
        max.value = rule.max
        list.text = rule.list
        message.text = rule.message
    }
    onAccepted: dialog.host.setValidationRule({
        kind: kind.currentIndex - 1, min: min.value, max: max.value, list: list.text, message: message.text
    })

    readonly property int kindValue: kind.currentIndex - 1
    readonly property bool numeric: kindValue === 0 || kindValue === 1 || kindValue === 3

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Allow")
            Tk.ComboBox {
                id: kind; objectName: "validationKind"
                model: [qsTr("Any value (remove validation)"), qsTr("Whole number"), qsTr("Decimal"), qsTr("List"), qsTr("Text length"), qsTr("Not empty")]
            }
        }
        Tk.PropertyRow { label: qsTr("Minimum"); Tk.NumberField { id: min; objectName: "validationMin"; enabled: dialog.numeric; from: -1e12; to: 1e12; decimals: 4 } }
        Tk.PropertyRow { label: qsTr("Maximum"); Tk.NumberField { id: max; objectName: "validationMax"; enabled: dialog.numeric; from: -1e12; to: 1e12; decimals: 4; value: 100 } }
        Tk.PropertyRow { label: qsTr("Entries"); Tk.TextField { id: list; objectName: "validationList"; enabled: dialog.kindValue === 2; placeholderText: qsTr("Comma-separated values") } }
        Tk.PropertyRow { label: qsTr("Message"); Tk.TextField { id: message; objectName: "validationMessage"; enabled: dialog.kindValue >= 0; placeholderText: qsTr("Shown when input is refused") } }
    }
}
