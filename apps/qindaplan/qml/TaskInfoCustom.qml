// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Task Information ▸ Custom Fields (M83, D-092): the task's values of the
// custom fields the plan uses, under their new names; a field with a lookup
// table offers its values; a formula's result is shown, not typed. Another
// field can be filled in below. Saved with the dialog's other fields.
Tk.Flex {
    id: page

    property PlanService plan: null
    property string taskId: ""
    property var values: []
    property var edited: ({})
    property var allFields: []

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function load(id) {
        taskId = id
        values = plan !== null ? plan.taskCustomValues(id) : []
        edited = ({})
        allFields = []
        const kinds = ["Text", "Number", "Date", "Flag", "Cost", "Duration", "Start", "Finish"]
        for (let k = 0; k < kinds.length; ++k) allFields = allFields.concat(plan.customFields(kinds[k]))
        otherValue.text = ""
    }
    function collect(out) {
        for (const key in edited) out[key] = edited[key]
        if (otherValue.text.trim().length > 0 && allFields.length > 0) out[allFields[otherField.currentIndex].field] = otherValue.text
        return out
    }

    Tk.Caption { visible: page.values.length === 0; text: qsTr("The plan uses no custom fields yet (Project ▸ Custom Fields).") }
    Repeater {
        model: page.values
        delegate: Tk.PropertyRow {
            id: row
            required property var modelData
            label: String(modelData.title)
            enabled: modelData.editable === true
            hint: modelData.editable !== true ? qsTr("Calculated by its formula")
                  : (modelData.choices || []).length > 0 ? qsTr("One of: %1").arg(modelData.choices.join(", ")) : ""
            Tk.TextField {
                objectName: "taskInfoCustom_" + row.modelData.field
                small: true
                text: String(row.modelData.value)
                onEditingFinished: if (text !== String(row.modelData.value)) {
                    const next = Object.assign({}, page.edited)
                    next[row.modelData.field] = text
                    page.edited = next
                }
            }
        }
    }
    Tk.Overline { title: qsTr("ANOTHER FIELD") }
    Tk.Grid {
        columns: "1fr 1fr"
        columnGap: Tk.Theme.space.sm
        Tk.ComboBox {
            id: otherField
            objectName: "taskInfoCustomOtherField"
            small: true
            model: page.allFields.map(function(f) { return f.title })
        }
        Tk.TextField { id: otherValue; objectName: "taskInfoCustomOtherValue"; small: true; placeholderText: qsTr("Value") }
    }
}
