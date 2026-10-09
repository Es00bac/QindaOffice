// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Task Usage (M79, D-088): hours of work per task — a summary adding up its
// subtasks — and under each task its resources' assignments, per day or
// week (Tk.ResourceTable over PlanService.taskUsageModel; the rows are the
// view's, filtered and sorted like the sheet). Read-only: work comes from
// the schedule; overallocation is the Resource Usage view's.
Item {
    id: view

    property PlanService plan: null
    property PlanHost host: null

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Flex {
            Tk.Flex.shrink: 0
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.md
            Tk.Heading { level: 2; text: qsTr("Task usage"); Tk.Flex.shrink: 0 }
            Tk.Caption {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                elide: Text.ElideRight
                text: qsTr("Hours of work per %1 · %2")
                    .arg(view.plan !== null && view.plan.usageWeeks ? qsTr("week") : qsTr("day"))
                    .arg(view.plan !== null ? view.plan.filterTitle : "")
            }
            Tk.Segmented {
                objectName: "planTaskUsageScale"
                small: true
                model: [qsTr("Days"), qsTr("Weeks")]
                currentIndex: view.plan !== null && view.plan.usageWeeks ? 1 : 0
                tooltip: qsTr("Usage per day or per week")
                onActivated: function(index) { if (view.host !== null) view.host.actions.entry("planUsageWeeksAction").checked = index === 1 }
            }
        }
        Tk.ResourceTable {
            objectName: "planTaskUsageTable"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.plan !== null ? view.plan.taskUsageModel : null
            tooltip: qsTr("Task usage")
            resourceColumnWidth: Tk.Theme.size.tableColumn * 3.2
            bucketColumnWidth: Tk.Theme.size.tableColumn * 0.7
            headerHeight: Tk.Theme.size.row * 1.5
            showZeros: false
            summaryMode: "usage"
        }
    }
}
