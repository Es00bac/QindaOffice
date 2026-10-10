// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Task Information ▸ Resources (M83, D-092), as Project's Task Form: each
// resource on the task with its units and its work. Typing one applies at
// once (one undo step) by the task's type — units on a Fixed Units task
// make it longer or shorter, on a Fixed Duration task they change the work,
// and work typed on one person can end that person's part early.
Tk.Flex {
    id: page

    property PlanService plan: null
    property string taskId: ""
    property var rows: []
    signal report(var result)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function refresh() { rows = plan !== null && taskId !== "" ? plan.assignmentDetails(taskId) : [] }
    function commit(uid, field, text) {
        report(plan.setAssignmentField(taskId, uid, field, text))
        refresh()
    }

    Tk.Flex {
        visible: page.rows.length > 0
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        Tk.Caption { text: qsTr("Resource"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
        Tk.Caption { text: qsTr("Units"); Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6; Tk.Flex.shrink: 0 }
        Tk.Caption { text: qsTr("Work"); Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6; Tk.Flex.shrink: 0 }
        Tk.Caption { text: qsTr("Cost"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
    }
    Tk.Caption {
        visible: page.rows.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("No resources yet. Type their names on the General page, or use Assign Resources.")
    }
    Repeater {
        model: page.rows
        delegate: Tk.Flex {
            id: row
            required property var modelData
            objectName: "taskInfoAssignment_" + modelData.resourceUid
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Icon {
                name: row.modelData.kind === "cost" ? "circle-dollar-sign" : row.modelData.kind === "material" ? "package" : "user"
                size: Tk.Theme.size.icon
                color: Tk.Theme.color.textMuted
            }
            Tk.Label { text: String(row.modelData.name); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.TextField {
                objectName: "taskInfoAssignmentUnits_" + row.modelData.resourceUid
                small: true
                text: String(row.modelData.units)
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6
                Tk.Flex.shrink: 0
                tooltip: row.modelData.kind === "cost" ? qsTr("Cost") : row.modelData.kind === "material" ? qsTr("Quantity") : qsTr("Units")
                onEditingFinished: if (text !== String(row.modelData.units)) page.commit(row.modelData.resourceUid, "units", text)
            }
            Tk.TextField {
                objectName: "taskInfoAssignmentWork_" + row.modelData.resourceUid
                visible: row.modelData.kind === "work"
                small: true
                text: String(row.modelData.work)
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 0.6
                Tk.Flex.shrink: 0
                tooltip: qsTr("Work")
                onEditingFinished: if (text !== String(row.modelData.work)) page.commit(row.modelData.resourceUid, "work", text)
            }
            Tk.Caption { text: String(row.modelData.cost); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        }
    }
}
