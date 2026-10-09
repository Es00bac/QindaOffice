// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Grouping (M74, D-083), Excel's dialog for a field of numbers or dates:
// Auto — Starting at / Ending at, each automatic (the data's own first and
// last value) unless unchecked — and By: a step for numbers ("0-9",
// "10-19" …), the date parts for dates (Seconds … Years, any number of
// them; Days alone takes a number of days). Text items group through
// Group Selection instead (CalcHost::groupAtCursor).
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property int field: -1
    property var state: ({})
    // Seconds, Minutes, Hours, Days, Months, Quarters, Years: CalcHost::applyPivotGroup's order.
    readonly property var partNames: [qsTr("Seconds"), qsTr("Minutes"), qsTr("Hours"), qsTr("Days"), qsTr("Months"),
                                      qsTr("Quarters"), qsTr("Years")]
    property var parts: [false, false, false, false, true, false, false]
    readonly property bool dates: dialog.state.dates === true
    readonly property bool daysOnly: dialog.parts.filter(function(on) { return on }).length === 1 && dialog.parts[3]

    objectName: "pivotGroupDialog"
    title: qsTr("Grouping")
    subtitle: dialog.state.name || ""
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: dialog.dates ? dialog.parts.indexOf(true) >= 0 : Number(by.text) > 0

    function load(index) {
        dialog.field = index
        dialog.state = dialog.host.pivotGroupState(index)
        autoStart.checked = true
        autoEnd.checked = true
        start.text = dialog.state.start || ""
        end.text = dialog.state.end || ""
        by.text = String(dialog.state.interval || 10)
        days.text = "1"
        dialog.parts = [false, false, false, false, true, false, false]
    }
    onAccepted: dialog.host.applyPivotGroup(dialog.field, {
        dates: dialog.dates,
        parts: dialog.parts,
        autoStart: autoStart.checked,
        autoEnd: autoEnd.checked,
        start: start.text,
        end: end.text,
        interval: dialog.dates ? (dialog.daysOnly ? Number(days.text) : 1) : Number(by.text)
    })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.SectionHeader { title: qsTr("Auto") }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.CheckBox { id: autoStart; objectName: "pivotGroupAutoStart"; text: qsTr("Starting at"); Tk.Flex.basis: Tk.Theme.size.fieldWidth }
            Tk.TextField {
                id: start
                objectName: "pivotGroupStart"
                enabled: !autoStart.checked
                Tk.Flex.grow: 1
                tooltip: dialog.dates ? qsTr("A date, like 1/1/2024") : qsTr("A number")
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.CheckBox { id: autoEnd; objectName: "pivotGroupAutoEnd"; text: qsTr("Ending at"); Tk.Flex.basis: Tk.Theme.size.fieldWidth }
            Tk.TextField {
                id: end
                objectName: "pivotGroupEnd"
                enabled: !autoEnd.checked
                Tk.Flex.grow: 1
                tooltip: dialog.dates ? qsTr("A date, like 12/31/2024") : qsTr("A number")
            }
        }
        Tk.SectionHeader { title: qsTr("By") }
        Tk.PropertyRow {
            visible: !dialog.dates
            label: qsTr("By")
            Tk.TextField { id: by; objectName: "pivotGroupBy"; tooltip: qsTr("The size of each group, like 10 for 0-9, 10-19 …") }
        }
        Tk.Grid {
            visible: dialog.dates
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.xs
            Repeater {
                model: dialog.partNames
                Tk.CheckBox {
                    required property string modelData
                    required property int index
                    objectName: "pivotGroupPart_" + index
                    text: modelData
                    checked: dialog.parts[index]
                    onToggled: {
                        const next = dialog.parts.slice()
                        next[index] = checked
                        dialog.parts = next
                    }
                }
            }
        }
        Tk.PropertyRow {
            visible: dialog.dates
            label: qsTr("Number of days")
            Tk.TextField { id: days; objectName: "pivotGroupDays"; enabled: dialog.daysOnly }
        }
    }
}
