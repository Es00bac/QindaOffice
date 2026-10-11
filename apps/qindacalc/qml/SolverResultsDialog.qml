// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Solver Results (M90, D-098): Excel's sentence for how the solve ended,
// with the variable cells already showing what Solver found behind it;
// Keep Solver Solution or Restore Original Values, the Answer Report, and
// Return to Solver Parameters Dialog. OK makes ONE undo step "Solver" of
// the model, the kept values and the report; Cancel restores.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var outcome: ({})

    signal returnRequested()

    objectName: "solverResultsDialog"
    title: qsTr("Solver Results")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.15
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")

    function load(outcome) {
        dialog.outcome = outcome
        keep.checked = outcome.changed === true
        restore.checked = outcome.changed !== true
        answer.checked = false
        back.checked = false
    }
    onAccepted: {
        dialog.host.solver.finish(keep.checked, answer.checked)
        if (back.checked) dialog.returnRequested()
    }
    onRejected: {
        dialog.host.solver.finish(false, false)
        if (back.checked) dialog.returnRequested()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice {
            objectName: "solverResultMessage"
            variant: dialog.outcome.feasible === true ? "success" : "warning"
            text: dialog.outcome.message || ""
        }
        Tk.RadioButton {
            id: keep
            objectName: "solverKeep"
            text: qsTr("Keep Solver Solution")
            enabled: dialog.outcome.changed === true
            onToggled: if (checked) restore.checked = false
        }
        Tk.RadioButton {
            id: restore
            objectName: "solverRestore"
            text: qsTr("Restore Original Values")
            onToggled: if (checked) keep.checked = false
        }
        Tk.CheckBox { id: back; objectName: "solverReturn"; text: qsTr("Return to Solver Parameters Dialog") }
        Tk.SectionHeader { title: qsTr("Reports") }
        Tk.CheckBox { id: answer; objectName: "solverAnswerReport"; text: qsTr("Answer") }
    }
}
