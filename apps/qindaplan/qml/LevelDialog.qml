// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Resource Leveling (Project's dialog; M51, options M79/D-088): which
// resource, the leveling range, the order that decides which task keeps
// its dates (ID Only; Standard; Priority, Standard), "level only within
// available slack" and "clear leveling values before leveling". The
// options are the project's (one undo step when changed, saved with it);
// Level runs the documented heuristic (docs/plan.md "Leveling") and says
// what it did; Clear Leveling takes every delay back.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property var resources: []
    property string outcome: ""
    readonly property var orders: ["id", "standard", "priority"]

    objectName: "levelDialog"
    title: qsTr("Resource Leveling")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.25
    primaryText: qsTr("Level")
    secondaryText: qsTr("Close")
    tertiaryText: qsTr("Clear leveling")

    function openDialog() {
        const all = plan.resourceList().filter(function(r) { return !r.material })
        const over = all.filter(function(r) { return r.overallocated })
        resources = [{ uid: 0, name: qsTr("All resources") }].concat(over.length ? over : all)
        picker.currentIndex = over.length === 1 ? 1 : 0
        const o = plan.levelingOptions()
        orderBox.currentIndex = Math.max(0, orders.indexOf(String(o.order)))
        withinSlack.checked = o.withinSlack === true
        clearFirst.checked = o.clearFirst !== false
        fromField.text = ""
        toField.text = ""
        outcome = ""
        open()
    }
    function saveOptions() {
        const r = plan.setLevelingOptions({ order: orders[orderBox.currentIndex], withinSlack: withinSlack.checked,
                                            clearFirst: clearFirst.checked })
        if (!r.ok) outcome = String(r.error || "")
        return r.ok
    }
    onAccepted: {
        if (saveOptions()) {
            const r = plan.level(resources[picker.currentIndex].uid, fromField.text, toField.text)
            outcome = r.ok ? String(r.message || "") : String(r.error || "")
        }
        open()
    }
    onRejected: saveOptions()
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
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Level from")
                hint: qsTr("empty = the start")
                Tk.TextField { id: fromField; objectName: "levelFrom"; placeholderText: qsTr("e.g. 6 Apr 2026") }
            }
            Tk.PropertyRow {
                label: qsTr("To")
                hint: qsTr("empty = the finish")
                Tk.TextField { id: toField; objectName: "levelTo" }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Leveling order")
            Tk.ComboBox {
                id: orderBox
                objectName: "levelOrder"
                small: true
                model: [qsTr("ID Only"), qsTr("Standard"), qsTr("Priority, Standard")]
            }
        }
        Tk.Switch {
            id: withinSlack
            objectName: "levelWithinSlack"
            text: qsTr("Level only within available slack (the finish never moves)")
        }
        Tk.Switch {
            id: clearFirst
            objectName: "levelClearFirst"
            text: qsTr("Clear leveling values before leveling")
        }
        Tk.Caption {
            wrapMode: Text.WordWrap
            text: qsTr("On each day a resource is overbooked, the task first in leveling order keeps its dates and another waits until after it — within its slack when that is enough, otherwise past it. Started tasks, Must Start/Finish On tasks and priority 1000 never move. Undo takes it all back.")
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
