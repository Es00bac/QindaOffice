// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The filters and sorts that ask a question first: Using Resource… (whose
// tasks) and Date Range… (tasks working on any day of a range), M79/D-088;
// M83 (D-092) Custom Field… (a custom field tested against a value) and
// Sort ▸ by Custom Field… (ascending or descending, tasks without a value
// last). Apply shows only those tasks (with their summary rows unless View ▸
// Filter ▸ Show Related Summary Rows is off); All Tasks lifts it. M89
// (D-097): Group By ▸ Custom Field… (bands by a custom field's value).
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property string filterId: ""
    property var resources: []
    property var fields: []
    readonly property var tests: ["equals", "notEquals", "contains", "greater", "less", "isSet", "notSet"]
    readonly property bool custom: filterId === "custom" || filterId === "customSort" || filterId === "customGroup"
    readonly property string test: tests[testBox.currentIndex] || "equals"

    objectName: "filterDialog"
    title: filterId === "resource" ? qsTr("Using Resource") : filterId === "dateRange" ? qsTr("Date Range")
         : filterId === "custom" ? qsTr("Custom Field Filter") : filterId === "customGroup" ? qsTr("Group by Custom Field")
         : qsTr("Sort by Custom Field")
    primaryText: filterId === "customSort" ? qsTr("Sort") : filterId === "customGroup" ? qsTr("Group") : qsTr("Apply")
    secondaryText: qsTr("Cancel")

    function openFor(id) {
        filterId = id
        resources = plan.resourceList()
        resourceBox.currentIndex = 0
        fromField.text = plan.projectInfo().start
        toField.text = plan.projectInfo().finish
        // The fields in use, else every Text field to start from.
        fields = plan.usedCustomFields()
        if (fields.length === 0)
            fields = plan.customFields("Text").map(function(f) { return { field: f.field, title: f.title } })
        fieldBox.currentIndex = 0
        testBox.currentIndex = 0
        valueField.text = ""
        error.visible = false
        open()
    }
    onAccepted: {
        const field = fields.length > 0 ? fields[fieldBox.currentIndex].field : ""
        const r = filterId === "resource" ? plan.applyFilter(filterId, { resourceUid: resources.length > 0 ? resources[resourceBox.currentIndex].uid : 0 })
                : filterId === "dateRange" ? plan.applyFilter(filterId, { from: fromField.text, to: toField.text })
                : filterId === "custom" ? plan.applyCustomFilter(field, test, valueField.text)
                : filterId === "customGroup" ? plan.setCustomGroup(field)
                : plan.setCustomSort(field, descendingSwitch.checked)
        if (!r.ok) {
            error.text = String(r.error || "")
            error.visible = true
            open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "filterError"; visible: false; variant: "danger"; title: qsTr("Not applied") }
        Tk.PropertyRow {
            visible: dialog.filterId === "resource"
            label: qsTr("Show tasks using")
            Tk.ComboBox {
                id: resourceBox
                objectName: "filterResource"
                small: true
                model: dialog.resources.map(function(r) { return r.name })
            }
        }
        Tk.PropertyRow {
            visible: dialog.filterId === "dateRange"
            label: qsTr("Working on or after")
            Tk.TextField { id: fromField; objectName: "filterFrom" }
        }
        Tk.PropertyRow {
            visible: dialog.filterId === "dateRange"
            label: qsTr("And on or before")
            Tk.TextField { id: toField; objectName: "filterTo" }
        }
        Tk.PropertyRow {
            visible: dialog.custom
            label: qsTr("Field")
            Tk.ComboBox {
                id: fieldBox
                objectName: "filterCustomField"
                small: true
                model: dialog.fields.map(function(f) { return f.title })
            }
        }
        Tk.PropertyRow {
            visible: dialog.filterId === "custom"
            label: qsTr("Test")
            Tk.ComboBox {
                id: testBox
                objectName: "filterCustomTest"
                small: true
                model: [qsTr("equals"), qsTr("does not equal"), qsTr("contains"), qsTr("is greater than"),
                        qsTr("is less than"), qsTr("has a value"), qsTr("has no value")]
            }
        }
        Tk.PropertyRow {
            visible: dialog.filterId === "custom" && dialog.test !== "isSet" && dialog.test !== "notSet"
            label: qsTr("Value")
            Tk.TextField { id: valueField; objectName: "filterCustomValue" }
        }
        Tk.Switch {
            id: descendingSwitch
            objectName: "filterCustomDescending"
            visible: dialog.filterId === "customSort"
            text: qsTr("Descending")
        }
    }
}
