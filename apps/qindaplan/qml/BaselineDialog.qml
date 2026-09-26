// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Set Baseline / Clear Baseline: which of the three baselines, and (when
// setting) for the whole project or the selected tasks only. A baseline
// that is already set says how many tasks it holds, so overwriting is a
// visible choice.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property bool clearing: false
    property var baselines: []
    readonly property var chosen: baselines.length > 0 ? baselines[Math.max(0, picker.currentIndex)] : ({})

    objectName: "baselineDialog"
    title: clearing ? qsTr("Clear Baseline") : qsTr("Set Baseline")
    primaryText: clearing ? qsTr("Clear") : qsTr("Set")
    destructive: clearing || chosen.set === true
    secondaryText: qsTr("Cancel")
    primaryEnabled: !clearing || chosen.set === true

    function openFor(clear) {
        clearing = clear
        baselines = plan.baselines()
        picker.currentIndex = 0
        selectedOnly.checked = false
        open()
    }
    onAccepted: {
        const r = clearing ? plan.clearBaseline(picker.currentIndex) : plan.setBaseline(picker.currentIndex, selectedOnly.checked)
        if (!r.ok) plan.notify(title, String(r.error || ""), "warning")
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Baseline")
            Tk.ComboBox {
                id: picker
                objectName: "baselinePicker"
                small: true
                model: dialog.baselines.map(function(b) {
                    return b.set ? qsTr("%1 (%2 tasks)").arg(b.label).arg(b.tasks) : b.label
                })
            }
        }
        Tk.Switch {
            id: selectedOnly
            objectName: "baselineSelectedOnly"
            visible: !dialog.clearing
            text: qsTr("Selected tasks only")
        }
        Tk.Notice {
            visible: !dialog.clearing && dialog.chosen.set === true
            variant: "warning"
            text: qsTr("%1 is already set. Setting it again replaces the saved dates, work and cost.").arg(String(dialog.chosen.label || ""))
        }
        Tk.Caption {
            text: dialog.clearing ? qsTr("Clearing removes the saved dates; variances against it disappear.")
                                  : qsTr("A baseline saves today's schedule so later slips show as variances and grey bars.")
            wrapMode: Text.WordWrap
        }
    }
}
