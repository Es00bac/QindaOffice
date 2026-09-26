// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Assign Resources (Alt+F10): the resource pool against the selected
// task(s). Each resource shows whether it is assigned, at what units and
// what that costs; Assign / Replace units / Remove act at once (one undo
// step each) and the list follows. Overallocated resources are flagged.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property var rows: []

    objectName: "assignResourcesDialog"
    title: qsTr("Assign Resources")
    subtitle: plan !== null && plan.currentTask.name !== undefined
              ? (plan.selectedTaskIds.length > 1 ? qsTr("%1 tasks selected").arg(plan.selectedTaskIds.length)
                                                 : String(plan.currentTask.name))
              : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.5
    primaryText: qsTr("Close")
    secondaryText: ""

    function refresh() { rows = plan.assignmentsOf(plan.currentTaskId) }
    function openDialog() {
        refresh()
        error.visible = false
        open()
    }
    function report(r) {
        error.visible = !r.ok
        if (!r.ok) error.text = String(r.error || "")
        refresh()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not assigned") }
        Tk.Caption {
            visible: dialog.rows.length === 0
            text: qsTr("The plan has no resources yet. Add people and materials on the Resource Sheet.")
        }
        Repeater {
            model: dialog.rows
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "assignRow_" + modelData.resourceUid
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Icon {
                    name: row.modelData.assigned ? "circle-check" : row.modelData.material ? "package" : "user"
                    size: Tk.Theme.size.icon
                    color: row.modelData.assigned ? Tk.Theme.color.success : Tk.Theme.color.textMuted
                }
                Tk.Label {
                    text: String(row.modelData.name)
                    font.bold: row.modelData.assigned
                    elide: Text.ElideRight
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Badge { visible: row.modelData.overallocated; variant: "danger"; text: qsTr("Overallocated") }
                Tk.TextField {
                    id: units
                    objectName: "assignUnits_" + row.modelData.resourceUid
                    small: true
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.8
                    Tk.Flex.shrink: 0
                    text: String(row.modelData.units)
                    placeholderText: row.modelData.material ? qsTr("Quantity") : qsTr("100%")
                }
                Tk.Caption { text: String(row.modelData.cost); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.Button {
                    objectName: "assignButton_" + row.modelData.resourceUid
                    small: true
                    variant: row.modelData.assigned ? "outline" : "accent"
                    text: row.modelData.assigned ? qsTr("Update") : qsTr("Assign")
                    onClicked: dialog.report(dialog.plan.assign(row.modelData.resourceUid, units.text))
                }
                Tk.IconButton {
                    iconName: "x"
                    tooltip: qsTr("Remove %1 from the task").arg(String(row.modelData.name))
                    enabled: row.modelData.assigned
                    onClicked: dialog.report(dialog.plan.unassign(row.modelData.resourceUid))
                }
            }
        }
    }
}
