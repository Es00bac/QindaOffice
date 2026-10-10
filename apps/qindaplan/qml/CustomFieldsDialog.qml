// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Project ▸ Custom Fields (M83, D-092): pick a kind (Text, Number, Date,
// Flag, Cost, Duration, Start, Finish) and a field of it (Text1 … Text30
// …), rename it, give it a lookup table — and allow only its values — or a
// formula (Project's syntax: [Duration]/480, IIf([% Complete]=100,"Done",
// "Open")). Apply saves the field as one undo step and stays open; the
// Custom Fields table (View ▸ Tables) shows every field in use.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    readonly property var kinds: ["Text", "Number", "Date", "Flag", "Cost", "Duration", "Start", "Finish"]
    property string kind: "Text"
    property var fields: []
    property string field: ""
    property var lookup: []

    objectName: "customFieldsDialog"
    title: qsTr("Custom Fields")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("Done")
    secondaryText: qsTr("Cancel")

    function openDialog() {
        pickKind(kind)
        error.visible = false
        open()
    }
    function pickKind(k) {
        kind = k
        fields = plan.customFields(k)
        pickField(fields.length > 0 ? fields[0].field : "")
    }
    function pickField(f) {
        field = f
        const def = plan.customFieldDefinition(f)
        aliasField.text = String(def.alias || "")
        formulaField.text = String(def.formula || "")
        restrictSwitch.checked = def.restrict === true
        lookup = (def.lookup || []).map(function(e) { return { value: e.value, description: e.description } })
    }
    function save() {
        const r = plan.saveCustomField(field, { alias: aliasField.text, formula: formulaField.text,
                                                 restrict: restrictSwitch.checked, lookup: lookup })
        error.visible = !r.ok
        if (!r.ok) error.text = String(r.error || "")
        const keep = field
        fields = plan.customFields(kind)
        if (r.ok) pickField(keep)
        return r.ok
    }
    function setEntry(i, key, value) {
        const next = lookup.slice()
        next[i] = Object.assign({}, next[i])
        next[i][key] = value
        lookup = next
    }
    onAccepted: if (!save()) open()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "customFieldsError"; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            Tk.PropertyRow {
                label: qsTr("Type")
                Tk.ComboBox {
                    objectName: "customFieldsKind"
                    small: true
                    model: [qsTr("Text"), qsTr("Number"), qsTr("Date"), qsTr("Flag"), qsTr("Cost"), qsTr("Duration"),
                            qsTr("Start"), qsTr("Finish")]
                    currentIndex: dialog.kinds.indexOf(dialog.kind)
                    onActivated: function(i) { dialog.pickKind(dialog.kinds[i]) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Field")
                Tk.ComboBox {
                    objectName: "customFieldsField"
                    small: true
                    model: dialog.fields.map(function(f) { return f.title + (f.used ? " •" : "") })
                    currentIndex: dialog.fields.map(function(f) { return f.field }).indexOf(dialog.field)
                    onActivated: function(i) { dialog.pickField(dialog.fields[i].field) }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Rename")
            hint: qsTr("The name the field shows as a column and in Task Information")
            Tk.TextField { id: aliasField; objectName: "customFieldsAlias"; placeholderText: dialog.field }
        }
        Tk.Overline { title: qsTr("LOOKUP TABLE") }
        Repeater {
            model: dialog.lookup
            delegate: Tk.Flex {
                id: entry
                required property var modelData
                required property int index
                objectName: "customFieldsLookup_" + index
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.TextField {
                    objectName: "customFieldsLookupValue_" + entry.index
                    small: true
                    text: String(entry.modelData.value)
                    placeholderText: qsTr("Value")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onEditingFinished: dialog.setEntry(entry.index, "value", text)
                }
                Tk.TextField {
                    small: true
                    text: String(entry.modelData.description || "")
                    placeholderText: qsTr("Description")
                    Tk.Flex.grow: 2
                    Tk.Flex.basis: 0
                    onEditingFinished: dialog.setEntry(entry.index, "description", text)
                }
                Tk.IconButton {
                    iconName: "x"
                    tooltip: qsTr("Remove this value")
                    onClicked: { const next = dialog.lookup.slice(); next.splice(entry.index, 1); dialog.lookup = next }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "customFieldsAddValue"
                small: true
                iconName: "plus"
                text: qsTr("Add value")
                enabled: dialog.kind !== "Flag"
                onClicked: dialog.lookup = dialog.lookup.concat([{ value: "", description: "" }])
            }
            Tk.Switch {
                id: restrictSwitch
                objectName: "customFieldsRestrict"
                text: qsTr("Allow only these values")
                enabled: dialog.lookup.length > 0
            }
        }
        Tk.Overline { title: qsTr("FORMULA") }
        Tk.PropertyRow {
            label: qsTr("Formula")
            hint: qsTr("Empty for typed values. Fields in brackets: [Duration]/480, [Cost]-[Baseline Cost]")
            Tk.TextField { id: formulaField; objectName: "customFieldsFormula"; mono: true }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Item { Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "customFieldsApply"
                small: true
                variant: "accent"
                text: qsTr("Apply")
                enabled: dialog.field !== ""
                onClicked: dialog.save()
            }
        }
    }
}
