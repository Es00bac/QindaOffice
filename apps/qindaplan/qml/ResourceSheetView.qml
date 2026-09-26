// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The Resource Sheet: every person, room or material the plan uses, edited
// in place (Tk.EditableGrid over PlanService's resource grid; the blank
// last row adds a resource). Overallocated resources are marked in red.
Item {
    id: view

    property PlanService plan: null
    readonly property var titles: {
        const out = {}
        for (const c of (plan !== null ? plan.resourceColumns : [])) out[c.id] = c.title
        return out
    }
    readonly property int overallocated: {
        let n = 0
        if (plan !== null && plan.revision >= 0) for (const r of plan.resourceList()) n += r.overallocated ? 1 : 0
        return n
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Flex {
            Tk.Flex.shrink: 0
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.md
            Tk.Heading { level: 2; text: qsTr("Resources"); Tk.Flex.shrink: 0 }
            Tk.Caption {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                elide: Text.ElideRight
                text: view.plan === null ? "" : view.overallocated > 0
                      ? qsTr("%1 resources · %2 overallocated").arg(view.plan.resourceCount).arg(view.overallocated)
                      : qsTr("%1 resources").arg(view.plan.resourceCount)
            }
            Tk.Badge {
                visible: view.overallocated > 0
                variant: "danger"
                text: qsTr("Overallocated")
            }
        }
        Tk.Divider { }
        Tk.EditableGrid {
            id: grid
            objectName: "planResourceSheet"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            model: view.plan !== null ? view.plan.resourceSheetModel : null
            tooltip: qsTr("Resource sheet")
            rowHeaderWidth: Tk.Theme.size.control * 1.6
            headerHeight: Tk.Theme.size.row * 1.5
            fillHandleEnabled: false
            columnLabel: function(id, index) { return view.titles[id] !== undefined ? view.titles[id] : id }
            rowLabel: function(id, index) { return id === "new" ? "" : String(index + 1) }
            onCurrentCellChanged: function(rowId, columnId) {
                if (view.plan !== null && rowId.startsWith("r")) view.plan.currentResourceUid = Number(rowId.substring(1))
            }
            function applyWidths() {
                if (view.plan === null || model === null) return
                for (const c of view.plan.resourceColumns) model.requestColumnResize(c.id, c.factor * Tk.Theme.size.tableColumn)
            }
            Component.onCompleted: applyWidths()
        }
    }
}
