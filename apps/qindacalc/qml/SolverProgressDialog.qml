// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Solver at work (M90, D-098): the trial solution and the objective's value
// while the worker solves (calc_solver.h); Stop ends the search, and Solver
// Results then offers what it had found — Excel's Esc and Stop. Modal, so
// the workbook is not edited under the solve. It closes itself when the
// host says the solve finished.
Tk.Dialog {
    id: dialog

    required property CalcHost host

    objectName: "solverProgressDialog"
    title: qsTr("Solver")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: ""
    secondaryText: qsTr("Stop")

    onRejected: dialog.host.solver.stop()

    Connections {
        target: dialog.host.solver
        function onRunningChanged() { if (!dialog.host.solver.running && dialog.visible) dialog.close() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Label {
            objectName: "solverProgressText"
            text: dialog.host.solver.progressText.length > 0 ? dialog.host.solver.progressText : qsTr("Setting up problem…")
        }
        Tk.ProgressBar { objectName: "solverProgressBar"; indeterminate: true }
    }
}
