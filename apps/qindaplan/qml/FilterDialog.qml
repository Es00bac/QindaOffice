// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The two of Project's filters that ask a question (M79/D-088): Using
// Resource… (whose tasks) and Date Range… (tasks working on any day of a
// range). Apply shows only those tasks (with their summary rows unless View
// ▸ Filter ▸ Show Related Summary Rows is off); All Tasks lifts it.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property string filterId: ""
    property var resources: []

    objectName: "filterDialog"
    title: filterId === "resource" ? qsTr("Using Resource") : qsTr("Date Range")
    primaryText: qsTr("Apply")
    secondaryText: qsTr("Cancel")

    function openFor(id) {
        filterId = id
        resources = plan.resourceList()
        resourceBox.currentIndex = 0
        fromField.text = plan.projectInfo().start
        toField.text = plan.projectInfo().finish
        error.visible = false
        open()
    }
    onAccepted: {
        const params = filterId === "resource"
            ? { resourceUid: resources.length > 0 ? resources[resourceBox.currentIndex].uid : 0 }
            : { from: fromField.text, to: toField.text }
        const r = plan.applyFilter(filterId, params)
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
    }
}
