// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Resource Usage: hours of work per resource and per assignment in each
// day or week (Tk.ResourceTable over PlanService's plan model). A cell over
// the resource's capacity is marked as a conflict — the overallocation the
// Level commands resolve. Read-only: work comes from the schedule.
Item {
    id: view

    property PlanService plan: null
    property PlanHost host: null
    readonly property var overallocated: {
        const out = []
        if (plan !== null && plan.revision >= 0) {
            for (const r of plan.resourceList()) if (r.overallocated) out.push(r.name)
        }
        return out
    }

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
            Tk.Heading { level: 2; text: qsTr("Resource usage"); Tk.Flex.shrink: 0 }
            Tk.Caption {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                elide: Text.ElideRight
                text: qsTr("Hours of work per %1").arg(view.plan !== null && view.plan.usageWeeks ? qsTr("week") : qsTr("day"))
            }
            Tk.Segmented {
                objectName: "planUsageScale"
                small: true
                model: [qsTr("Days"), qsTr("Weeks")]
                currentIndex: view.plan !== null && view.plan.usageWeeks ? 1 : 0
                tooltip: qsTr("Usage per day or per week")
                onActivated: function(index) { if (view.host !== null) view.host.actions.entry("planUsageWeeksAction").checked = index === 1 }
            }
            Tk.Button {
                objectName: "planUsageLevel"
                small: true
                variant: "outline"
                iconName: "scale"
                text: qsTr("Level…")
                enabled: view.overallocated.length > 0
                onClicked: view.host.actions.trigger("planLevelResourceAction")
            }
        }
        Tk.Notice {
            objectName: "planUsageNotice"
            Tk.Flex.shrink: 0
            visible: view.overallocated.length > 0
            variant: "danger"
            title: qsTr("Overallocated")
            text: qsTr("%1 %2 more work than their hours allow on some days. Level them, or move or shorten their tasks.")
                .arg(view.overallocated.join(", ")).arg(view.overallocated.length === 1 ? qsTr("has") : qsTr("have"))
        }
        Tk.ResourceTable {
            id: table
            objectName: "planUsageTable"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.plan !== null ? view.plan.usageModel : null
            tooltip: qsTr("Resource usage")
            resourceColumnWidth: Tk.Theme.size.tableColumn * 2.8
            bucketColumnWidth: Tk.Theme.size.tableColumn * 0.7
            headerHeight: Tk.Theme.size.row * 1.5
            showZeros: false
            summaryMode: "usage"
        }
    }
}
