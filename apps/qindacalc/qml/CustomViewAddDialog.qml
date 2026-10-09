// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Custom Views ▸ Add (M82, D-091), Excel's Add View: the name, and what
// the view holds besides the window — print settings, and hidden rows,
// columns and filter settings (both on, as in Excel). A view of the same
// name is replaced. A refusal reopens the dialog with the reason.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property string error: ""

    signal saved()

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "customViewAddDialog"
    title: qsTr("Add View")
    primaryText: qsTr("OK")
    primaryEnabled: name.text.trim().length > 0

    function load() {
        name.text = ""
        printSettings.checked = true
        hiddenRowsCols.checked = true
        dialog.error = ""
    }
    onOpened: name.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const problem = dialog.host.addCustomView(name.text, printSettings.checked, hiddenRowsCols.checked)
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
            return
        }
        dialog.saved()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "customViewError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Name")
            Tk.TextField { id: name; objectName: "customViewName" }
        }
        Tk.SectionHeader { title: qsTr("Include in view") }
        Tk.CheckBox { id: printSettings; objectName: "customViewPrintSettings"; text: qsTr("Print settings") }
        Tk.CheckBox {
            id: hiddenRowsCols
            objectName: "customViewHiddenRowsCols"
            text: qsTr("Hidden rows, columns and filter settings")
        }
    }
}
