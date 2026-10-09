// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ What-If Analysis ▸ Scenario Manager (M78, D-087): the active
// sheet's scenarios with their changing cells and comment; Add, Edit and
// Delete (ScenarioEditDialog.qml), Show (the values go into the cells, one
// undo step) and Summary (a Scenario Summary sheet for the result cells
// asked for). Each change is applied at once and is its own undo step.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var scenarios: []
    property int selected: -1
    property string note: ""
    readonly property var current: dialog.selected >= 0 && dialog.selected < dialog.scenarios.length
                                    ? dialog.scenarios[dialog.selected] : null

    signal editRequested(int index)
    signal summaryRequested()

    objectName: "scenarioManagerDialog"
    title: qsTr("Scenario Manager")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.2
    primaryText: qsTr("Close")
    secondaryText: ""

    function load() {
        dialog.scenarios = dialog.host.scenarioList()
        dialog.selected = dialog.scenarios.length > 0 ? Math.min(Math.max(0, dialog.selected), dialog.scenarios.length - 1) : -1
        dialog.note = ""
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.SectionHeader { title: qsTr("Scenarios"); count: String(dialog.scenarios.length) }
        Tk.Scroll {
            objectName: "scenarioList"
            implicitHeight: Tk.Theme.size.row * 5
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.scenarios
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "scenario_" + index
                        text: modelData.name
                        secondaryText: modelData.cells
                        selected: dialog.selected === index
                        onClicked: dialog.selected = index
                        onDoubleClicked: dialog.host.showScenarioAt(modelData.index)
                    }
                }
                Tk.Caption {
                    visible: dialog.scenarios.length === 0
                    muted: true
                    text: qsTr("No Scenarios defined. Choose Add to add scenarios.")
                }
            }
        }
        Tk.KeyValue { objectName: "scenarioChangingCells"; key: qsTr("Changing cells"); value: dialog.current ? dialog.current.cells : "" }
        Tk.KeyValue { objectName: "scenarioComment"; key: qsTr("Comment"); mono: false; value: dialog.current ? dialog.current.comment : "" }
        Tk.Notice { objectName: "scenarioNote"; visible: dialog.note.length > 0; variant: "danger"; text: dialog.note }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            wrap: Tk.Flex.Wrap
            Tk.Button { objectName: "scenarioAdd"; small: true; iconName: "plus"; text: qsTr("Add…"); onClicked: dialog.editRequested(-1) }
            Tk.Button {
                objectName: "scenarioEdit"; small: true; iconName: "pencil"; text: qsTr("Edit…"); available: dialog.current !== null
                onClicked: dialog.editRequested(dialog.current.index)
            }
            Tk.Button {
                objectName: "scenarioDelete"; small: true; iconName: "trash-2"; text: qsTr("Delete"); available: dialog.current !== null
                onClicked: { dialog.host.deleteScenario(dialog.current.index); dialog.load() }
            }
            Tk.Button {
                objectName: "scenarioShow"; small: true; iconName: "eye"; text: qsTr("Show"); available: dialog.current !== null
                onClicked: dialog.note = dialog.host.showScenarioAt(dialog.current.index)
            }
            Tk.Button {
                objectName: "scenarioSummary"; small: true; iconName: "table"; text: qsTr("Summary…"); available: dialog.scenarios.length > 0
                onClicked: dialog.summaryRequested()
            }
        }
    }
}
