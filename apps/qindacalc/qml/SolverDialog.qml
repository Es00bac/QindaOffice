// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ Analyze ▸ Solver (M90, D-098): Excel's Solver Parameters — Set
// Objective, To Max / Min / Value Of, By Changing Variable Cells, Subject to
// the Constraints (Add, Change, Delete, Reset All), Make Unconstrained
// Variables Non-Negative, the solving method and its Options. Solve starts
// the worker (calc_solver.h) and hands over to the progress dialog; Close
// keeps the model in the workbook's names (one undo step when it changed),
// as Excel's Close does. A refusal reopens the dialog with Excel's message.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var model: ({})
    property var constraints: []
    property var options: ({})
    property int selected: -1
    property string error: ""
    property bool solving: false

    signal constraintRequested(int index, var constraint)
    signal optionsRequested(int method, var options)
    signal started()

    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    // Excel's labels are long ("By Changing Variable Cells").
    readonly property real labelWidth: Tk.Theme.size.labelWidth * 1.6

    objectName: "solverDialog"
    title: qsTr("Solver Parameters")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.35
    primaryText: qsTr("Solve")
    secondaryText: qsTr("Close")
    primaryEnabled: variables.text.trim().length > 0

    readonly property var methods: [qsTr("GRG Nonlinear"), qsTr("Simplex LP"), qsTr("Evolutionary")]
    readonly property var notes: [
        qsTr("Select the GRG Nonlinear engine for Solver Problems that are smooth nonlinear."),
        qsTr("Select the LP Simplex engine for linear Solver Problems."),
        qsTr("Select the Evolutionary engine for Solver problems that are non-smooth.")]

    function load(state) {
        dialog.model = state
        objective.text = state.objective || ""
        goal.currentIndex = state.goal === "min" ? 1 : (state.goal === "value" ? 2 : 0)
        valueOf.text = state.valueOf || "0"
        variables.text = state.variables || ""
        dialog.constraints = state.constraints || []
        nonNegative.checked = state.nonNegative !== false
        method.currentIndex = Math.max(0, (state.method || 1) - 1)
        dialog.options = state.options || ({})
        dialog.selected = dialog.constraints.length > 0 ? 0 : -1
        dialog.error = ""
    }
    // The dialog's model as the host reads it.
    function parameters() {
        return {
            objective: objective.text, goal: ["max", "min", "value"][goal.currentIndex], valueOf: valueOf.text,
            variables: variables.text, constraints: dialog.constraints, nonNegative: nonNegative.checked,
            method: method.currentIndex + 1, options: dialog.options
        }
    }
    function setConstraint(index, constraint) {
        const list = dialog.constraints.slice()
        if (index >= 0 && index < list.length) list[index] = constraint
        else list.push(constraint)
        dialog.constraints = list
        dialog.selected = index >= 0 ? index : list.length - 1
    }

    onAccepted: {
        const problem = dialog.host.solver.solve(dialog.parameters())
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
            return
        }
        dialog.started()
    }
    onRejected: dialog.host.solver.saveParameters(dialog.parameters())

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "solverError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            labelWidth: dialog.labelWidth
            label: qsTr("Set Objective")
            Tk.TextField { id: objective; objectName: "solverObjective"; mono: true }
        }
        Tk.PropertyRow {
            labelWidth: dialog.labelWidth
            label: qsTr("To")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                wrap: Tk.Flex.Wrap
                Tk.Segmented {
                    id: goal
                    objectName: "solverGoal"
                    small: true
                    model: [qsTr("Max"), qsTr("Min"), qsTr("Value Of")]
                }
                Tk.TextField {
                    id: valueOf
                    objectName: "solverValueOf"
                    mono: true
                    implicitWidth: 96
                    enabled: goal.currentIndex === 2
                }
            }
        }
        Tk.PropertyRow {
            labelWidth: dialog.labelWidth
            label: qsTr("By Changing Variable Cells")
            Tk.TextField { id: variables; objectName: "solverVariables"; mono: true }
        }
        Tk.SectionHeader { title: qsTr("Subject to the Constraints"); count: String(dialog.constraints.length) }
        Tk.Scroll {
            objectName: "solverConstraintList"
            implicitHeight: Tk.Theme.size.row * 5
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.constraints
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "solverConstraint_" + index
                        text: modelData.text || (modelData.left + " " + modelData.relation + " " + modelData.right)
                        selected: dialog.selected === index
                        onClicked: dialog.selected = index
                        onDoubleClicked: dialog.constraintRequested(index, modelData)
                    }
                }
                Tk.Caption {
                    visible: dialog.constraints.length === 0
                    muted: true
                    text: qsTr("No constraints. Choose Add to add one.")
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            wrap: Tk.Flex.Wrap
            Tk.Button {
                objectName: "solverAdd"; small: true; iconName: "plus"; text: qsTr("Add…")
                onClicked: dialog.constraintRequested(-1, ({ left: "", relation: "<=", right: "" }))
            }
            Tk.Button {
                objectName: "solverChange"; small: true; iconName: "pencil"; text: qsTr("Change…")
                available: dialog.selected >= 0 && dialog.selected < dialog.constraints.length
                onClicked: dialog.constraintRequested(dialog.selected, dialog.constraints[dialog.selected])
            }
            Tk.Button {
                objectName: "solverDelete"; small: true; iconName: "trash-2"; text: qsTr("Delete")
                available: dialog.selected >= 0 && dialog.selected < dialog.constraints.length
                onClicked: {
                    dialog.constraints = dialog.constraints.filter(function(c, i) { return i !== dialog.selected })
                    dialog.selected = Math.min(dialog.selected, dialog.constraints.length - 1)
                }
            }
            Tk.Button {
                objectName: "solverResetAll"; small: true; iconName: "rotate-ccw"; text: qsTr("Reset All")
                onClicked: dialog.load(dialog.host.solver.defaultParameters())
            }
        }
        Tk.CheckBox {
            id: nonNegative
            objectName: "solverNonNegative"
            text: qsTr("Make Unconstrained Variables Non-Negative")
        }
        Tk.PropertyRow {
            labelWidth: dialog.labelWidth
            label: qsTr("Select a Solving Method")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.ComboBox { id: method; objectName: "solverMethod"; model: dialog.methods; Tk.Flex.grow: 1 }
                Tk.Button {
                    objectName: "solverOptions"; small: true; text: qsTr("Options…")
                    onClicked: dialog.optionsRequested(method.currentIndex + 1, dialog.options)
                }
            }
        }
        Tk.Caption {
            objectName: "solverMethodNote"
            muted: true
            wrapMode: Text.WordWrap
            text: dialog.notes[method.currentIndex] || ""
        }
    }
}
