// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Resource Information: the resource sheet's fields in a form, plus a way
// into the resource's own working calendar. Each changed field is saved in
// one undo step; a refusal names the field.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property PlanHost host: null
    property int uid: 0
    readonly property var fields: ["name", "type", "materialLabel", "initials", "group", "maxUnits", "standardRate",
                                   "costPerUse", "email"]
    property var before: ({})

    objectName: "resourceInfoDialog"
    title: qsTr("Resource Information")
    subtitle: String(before.name || "")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")
    tertiaryText: qsTr("Working time…")

    function openFor(resourceUid) {
        uid = resourceUid
        const list = plan.resourceList()
        const index = list.map(function(r) { return r.uid }).indexOf(resourceUid)
        if (index < 0) return
        const values = {}
        for (const f of fields) values[f] = plan.resourceSheetModel.editValue("r" + resourceUid, f)
        before = values
        nameField.text = values.name; typeBox.currentIndex = values.type === "Material" ? 1 : values.type === "Cost" ? 2 : 0
        labelField.text = values.materialLabel; initialsField.text = values.initials; groupField.text = values.group
        unitsField.text = values.maxUnits; rateField.text = values.standardRate; perUseField.text = values.costPerUse
        emailField.text = values.email
        error.visible = false
        open()
    }
    onAccepted: {
        const after = { name: nameField.text, type: ["Work", "Material", "Cost"][typeBox.currentIndex],
                        materialLabel: labelField.text, initials: initialsField.text, group: groupField.text,
                        maxUnits: unitsField.text, standardRate: rateField.text, costPerUse: perUseField.text,
                        email: emailField.text }
        for (const f of fields) {
            if (String(after[f]) === String(before[f])) continue
            const r = plan.setResourceField(uid, f, after[f])
            if (!r.ok) { error.text = String(r.error || ""); error.visible = true; open(); return }
        }
    }
    onTertiary: {
        plan.currentResourceUid = uid
        host.actions.trigger("planResourceCalendarAction")
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.PropertyRow { label: qsTr("Name"); Tk.TextField { id: nameField; objectName: "resourceInfoName" } }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Type"); Tk.ComboBox { id: typeBox; small: true; model: [qsTr("Work"), qsTr("Material"), qsTr("Cost")] } }
            Tk.PropertyRow { label: qsTr("Material label"); enabled: typeBox.currentIndex === 1; Tk.TextField { id: labelField } }
            Tk.PropertyRow { label: qsTr("Initials"); Tk.TextField { id: initialsField } }
            Tk.PropertyRow { label: qsTr("Group"); Tk.TextField { id: groupField } }
            Tk.PropertyRow { label: qsTr("Max. units"); enabled: typeBox.currentIndex === 0; Tk.TextField { id: unitsField; objectName: "resourceInfoMaxUnits" } }
            Tk.PropertyRow { label: qsTr("Std. rate"); enabled: typeBox.currentIndex !== 2; Tk.TextField { id: rateField; objectName: "resourceInfoRate" } }
            Tk.PropertyRow { label: qsTr("Cost/use"); enabled: typeBox.currentIndex !== 2; Tk.TextField { id: perUseField } }
            Tk.PropertyRow { label: qsTr("Email"); Tk.TextField { id: emailField; iconName: "mail" } }
        }
    }
}
