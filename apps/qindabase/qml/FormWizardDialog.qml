// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The form wizard: pick a table, the fields in the order they appear
// (every field but the automatic number, by default), a title and a name;
// the form opens ready for data entry. Its layout (labels, order, shown
// fields) is edited afterwards in the form's Design view.
Tk.Dialog {
    id: dialog

    property var window: null
    readonly property var base: window !== null ? window.base : null
    property string table: ""
    property var fields: []
    property var chosen: []

    title: qsTr("Form wizard")
    subtitle: qsTr("A single-record form for entering and editing records.")
    primaryText: qsTr("Create form")
    secondaryText: qsTr("Cancel")
    primaryEnabled: table.length > 0 && chosen.length > 0 && nameField.text.trim().length > 0

    function openFor(preferred) {
        const tables = base.tableNames
        choose(preferred && tables.indexOf(preferred) >= 0 ? preferred : (tables[0] || ""))
        open()
    }
    function choose(t) {
        table = t
        fields = t ? base.tableFields(t) : []
        chosen = fields.filter(function(f) { return f.type !== "autoNumber" }).map(function(f) { return f.name })
        titleField.text = t
        nameField.text = t ? qsTr("%1 Form").arg(t.charAt(0).toUpperCase() + t.slice(1)) : ""
    }
    function toggle(name, on) {
        // Keep the table's order.
        chosen = fields.map(function(f) { return f.name }).filter(function(n) {
            return n === name ? on : chosen.indexOf(n) >= 0
        })
    }
    onAccepted: {
        const r = base.createForm(nameField.text.trim(), table, titleField.text.trim(), chosen)
        if (!r.ok) { window.notify(qsTr("No form created"), r.error, "danger"); return }
        window.baseHost.selectedKind = "form"
        window.baseHost.selectedName = r.name
        window.commands.openObject("form", r.name)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Table")
            Tk.ComboBox {
                objectName: "formWizardTable"
                small: true
                model: dialog.base !== null ? dialog.base.tableNames : []
                currentIndex: model.indexOf(dialog.table)
                onActivated: function(i) { dialog.choose(model[i]) }
            }
        }
        Tk.PropertyRow { label: qsTr("Title"); Tk.TextField { id: titleField; small: true } }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "formWizardName"; small: true } }
        Tk.Overline { title: qsTr("FIELDS ON THE FORM") }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.md
            Repeater {
                model: dialog.fields
                delegate: Tk.CheckBox {
                    required property var modelData
                    text: modelData.caption || modelData.name
                    enabled: modelData.type !== "autoNumber"
                    checked: dialog.chosen.indexOf(modelData.name) >= 0
                    onToggled: dialog.toggle(modelData.name, checked)
                }
            }
        }
    }
}
