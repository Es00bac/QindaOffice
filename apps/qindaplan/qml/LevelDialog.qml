// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Level Resource: pick an overallocated resource (or all of them) and let
// the heuristic delay tasks — first only within their slack, then, when
// nothing else fixes a day, past it (the finish moves). What it does is
// said in plain words before and after (D-051, docs/plan.md "Leveling").
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property var resources: []
    property string outcome: ""

    objectName: "levelDialog"
    title: qsTr("Level Resource")
    primaryText: qsTr("Level")
    secondaryText: qsTr("Close")
    tertiaryText: qsTr("Clear leveling")

    function openDialog() {
        const all = plan.resourceList().filter(function(r) { return !r.material })
        const over = all.filter(function(r) { return r.overallocated })
        resources = [{ uid: 0, name: qsTr("All resources") }].concat(over.length ? over : all)
        picker.currentIndex = over.length === 1 ? 1 : 0
        outcome = ""
        open()
    }
    onAccepted: {
        const r = plan.level(resources[picker.currentIndex].uid)
        outcome = r.ok ? String(r.message || "") : String(r.error || "")
        open()
    }
    onTertiary: {
        const r = plan.clearLeveling()
        outcome = r.ok ? qsTr("Leveling delays were removed.") : String(r.error || "")
        open()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Resource")
            Tk.ComboBox {
                id: picker
                objectName: "levelPicker"
                small: true
                model: dialog.resources.map(function(r) { return r.name + (r.overallocated ? qsTr(" — overallocated") : "") })
            }
        }
        Tk.Caption {
            wrapMode: Text.WordWrap
            text: qsTr("On each day the resource is overbooked, the task with the least slack keeps its dates and another is delayed until after it — within its slack when that is enough, otherwise past it. Started tasks and Must Start/Finish On tasks never move. Undo takes it all back.")
        }
        Tk.Notice {
            objectName: "levelResult"
            visible: dialog.outcome.length > 0
            variant: "info"
            title: qsTr("Leveling")
            text: dialog.outcome
        }
    }
}
