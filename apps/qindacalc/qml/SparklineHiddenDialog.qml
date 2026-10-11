// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// The Sparkline tab's Hidden and Empty Cell Settings (M90, D-098): show
// empty cells as Gaps, Zero or Connect data points with line, and Show data
// in hidden rows and columns — for the selected groups, one undo step.
Tk.Dialog {
    id: dialog

    required property CalcHost host

    objectName: "sparklineHiddenDialog"
    title: qsTr("Hidden and Empty Cell Settings")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")

    function load() {
        const state = dialog.host.sparklines.hiddenEmptyState()
        gaps.checked = state.empty === 0
        zero.checked = state.empty === 1
        connect.checked = state.empty === 2
        hidden.checked = state.hidden === true
    }
    onAccepted: dialog.host.sparklines.applyHiddenEmpty(gaps.checked ? 0 : (zero.checked ? 1 : 2), hidden.checked)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.SectionHeader { title: qsTr("Show empty cells as") }
        Tk.RadioButton {
            id: gaps; objectName: "sparklineEmptyGaps"; text: qsTr("Gaps")
            onToggled: if (checked) { zero.checked = false; connect.checked = false }
        }
        Tk.RadioButton {
            id: zero; objectName: "sparklineEmptyZero"; text: qsTr("Zero")
            onToggled: if (checked) { gaps.checked = false; connect.checked = false }
        }
        Tk.RadioButton {
            id: connect; objectName: "sparklineEmptyConnect"; text: qsTr("Connect data points with line")
            onToggled: if (checked) { gaps.checked = false; zero.checked = false }
        }
        Tk.CheckBox { id: hidden; objectName: "sparklineShowHidden"; text: qsTr("Show data in hidden rows and columns") }
    }
}
