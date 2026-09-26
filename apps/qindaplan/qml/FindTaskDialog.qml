// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Find and Replace over task names (Edit ▸ Find, Replace). Find selects
// the next matching task and remembers the text for Find Next/Previous;
// Replace All rewrites every matching name in one undo step.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property PlanHost host: null
    property bool replacing: false
    property string outcome: ""

    objectName: "findTaskDialog"
    title: replacing ? qsTr("Replace in task names") : qsTr("Find task")
    primaryText: replacing ? qsTr("Replace all") : qsTr("Find next")
    secondaryText: qsTr("Close")
    primaryEnabled: findField.text.length > 0

    function openFor(replace) {
        replacing = replace
        findField.text = host.findText
        outcome = ""
        open()
        findField.forceActiveFocus()
    }
    onAccepted: {
        host.findText = findField.text
        const r = replacing ? plan.replaceInNames(findField.text, replaceField.text) : plan.findTask(findField.text, true)
        outcome = r.ok ? (replacing ? qsTr("Renamed %n task(s).", "", r.count) : "") : String(r.error || "")
        if (replacing || !r.ok) open()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Find")
            Tk.TextField { id: findField; objectName: "findTaskText"; iconName: "search"; placeholderText: qsTr("Part of a task name") }
        }
        Tk.PropertyRow {
            visible: dialog.replacing
            label: qsTr("Replace with")
            Tk.TextField { id: replaceField; objectName: "findTaskReplacement" }
        }
        Tk.Caption { visible: dialog.outcome.length > 0; text: dialog.outcome }
    }
}
