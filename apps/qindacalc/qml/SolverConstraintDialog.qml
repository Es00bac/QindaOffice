// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Solver's Add Constraint / Change Constraint (M90, D-098): Cell Reference,
// the relation (<=, =, >=, int, bin, dif) and the Constraint — a number, a
// reference or a formula; int, bin and dif fill it in themselves, as Excel
// does. OK checks it with the host (Excel's messages) and hands it to the
// Solver Parameters dialog; Add does the same and stays open for the next.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var parameters: ({})
    property int index: -1
    property string error: ""

    signal saved(int index, var constraint)

    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    readonly property var relations: ["<=", "=", ">=", "int", "bin", "dif"]
    readonly property bool fixedRight: relation.currentIndex >= 3

    objectName: "solverConstraintDialog"
    title: dialog.index >= 0 ? qsTr("Change Constraint") : qsTr("Add Constraint")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    tertiaryText: qsTr("Add")
    primaryEnabled: left.text.trim().length > 0 && (dialog.fixedRight || right.text.trim().length > 0)

    function load(index, constraint, parameters) {
        dialog.index = index
        dialog.parameters = parameters
        left.text = constraint.left || ""
        relation.currentIndex = Math.max(0, dialog.relations.indexOf(constraint.relation || "<="))
        right.text = dialog.fixedRight ? "" : (constraint.right || "")
        dialog.error = ""
    }
    function current() {
        const rel = dialog.relations[relation.currentIndex]
        const words = { "int": "integer", "bin": "binary", "dif": "AllDifferent" }
        const rhs = dialog.fixedRight ? words[rel] : right.text.trim()
        return { left: left.text.trim(), relation: rel, right: rhs, text: left.text.trim() + " " + rel + " " + rhs }
    }
    // "" when the host takes it; else the message is shown and the dialog reopens.
    function commit() {
        const constraint = dialog.current()
        const problem = dialog.host.solver.checkConstraint(dialog.parameters, constraint)
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
            return false
        }
        dialog.saved(dialog.index, constraint)
        return true
    }

    onAccepted: dialog.commit()
    onTertiary: {
        if (dialog.commit()) {
            // Add: the next constraint, in a fresh dialog.
            dialog.index = -1
            left.text = ""
            right.text = ""
            dialog.error = ""
            dialog.reopen = true
        }
        dialog.close()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "solverConstraintError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Cell Reference")
            Tk.TextField { id: left; objectName: "solverConstraintLeft"; mono: true }
        }
        Tk.PropertyRow {
            label: qsTr("Relation")
            Tk.ComboBox { id: relation; objectName: "solverConstraintRelation"; model: dialog.relations }
        }
        Tk.PropertyRow {
            label: qsTr("Constraint")
            Tk.TextField {
                id: right
                objectName: "solverConstraintRight"
                mono: true
                enabled: !dialog.fixedRight
                placeholderText: dialog.fixedRight ? ["integer", "binary", "AllDifferent"][relation.currentIndex - 3] : ""
            }
        }
    }
}
