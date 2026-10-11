// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ Solver's dialogs (M90, D-098) and the host requests that open
// them: Solver Parameters with Add / Change Constraint and Options, the
// progress dialog while the worker solves, Solver Results.
// AGENT-GUARD: a QtObject, not an Item — the window seats ONE content child
// (Tk.AppWindow); the dialogs reach the window's overlay through hostItem.
QtObject {
    id: root

    required property CalcHost host
    property Item hostItem: null

    function openParameters(state) {
        root.parameters.load(state)
        root.parameters.open()
    }

    property SolverDialog parameters: SolverDialog {
        host: root.host
        hostItem: root.hostItem
        onConstraintRequested: function(index, constraint) {
            root.constraint.load(index, constraint, root.parameters.parameters())
            root.constraint.open()
        }
        onOptionsRequested: function(method, options) {
            root.options.load(method, options)
            root.options.open()
        }
        onStarted: root.progress.open()
    }
    property SolverConstraintDialog constraint: SolverConstraintDialog {
        host: root.host
        hostItem: root.hostItem
        onSaved: function(index, constraint) { root.parameters.setConstraint(index, constraint) }
        onClosed: if (!root.constraint.reopen && !root.parameters.visible) root.parameters.open()
    }
    property SolverOptionsDialog options: SolverOptionsDialog {
        hostItem: root.hostItem
        onSaved: function(options) { root.parameters.options = options }
        onClosed: if (!root.parameters.visible) root.parameters.open()
    }
    property SolverProgressDialog progress: SolverProgressDialog { host: root.host; hostItem: root.hostItem }
    property SolverResultsDialog results: SolverResultsDialog {
        host: root.host
        hostItem: root.hostItem
        onReturnRequested: root.openParameters(root.host.solver.parameters())
    }

    property Connections requests: Connections {
        target: root.host.solver
        function onParametersRequested() { root.openParameters(root.host.solver.parameters()) }
        function onFinished(outcome) {
            root.results.load(outcome)
            root.results.open()
        }
    }
}
