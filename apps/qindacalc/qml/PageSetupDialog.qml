// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Page Setup (M78, D-087): Excel's four tabs over the active sheet — Page
// (orientation, scaling, paper size, first page number), Margins (and
// centring), Header/Footer (odd, even and first headers and footers in
// Excel's &[Page] spelling) and Sheet (print area, rows and columns to
// repeat, gridlines, headings, black and white, draft, comments, errors,
// page order). The tabs edit one draft (CalcHost::pageSetupState) that OK
// hands back as ONE undo step; a reference the host cannot read reopens
// the dialog with the reason.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var draft: ({})
    property int tab: 0
    property string error: ""

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "pageSetupDialog"
    title: qsTr("Page Setup")
    subtitle: dialog.draft.sheet !== undefined ? dialog.draft.sheet : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("OK")

    function load(tabName) {
        dialog.draft = dialog.host.pageSetupState()
        dialog.error = ""
        dialog.tab = Math.max(0, ["page", "margins", "header", "sheet"].indexOf(tabName))
    }
    function set(key, value) {
        const next = Object.assign({}, dialog.draft)
        next[key] = value
        dialog.draft = next
    }
    function setIn(key, sub, value) {
        const inner = Object.assign({}, dialog.draft[key] || {})
        inner[sub] = value
        dialog.set(key, inner)
    }
    onAccepted: {
        const problem = dialog.host.applyPageSetup(dialog.draft)
        if (problem.length > 0) {
            dialog.error = problem
            dialog.reopen = true
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TabStrip {
            objectName: "pageSetupTabs"
            model: [qsTr("Page"), qsTr("Margins"), qsTr("Header/Footer"), qsTr("Sheet")]
            currentIndex: dialog.tab
            onTabActivated: function(index) { dialog.tab = index }
        }
        Tk.Notice {
            objectName: "pageSetupError"
            visible: dialog.error.length > 0
            variant: "danger"
            text: dialog.error
        }
        PageSetupPageTab { dialog: dialog; visible: dialog.tab === 0 }
        PageSetupMarginsTab { dialog: dialog; visible: dialog.tab === 1 }
        PageSetupHeaderTab { dialog: dialog; visible: dialog.tab === 2 }
        PageSetupSheetTab { dialog: dialog; visible: dialog.tab === 3 }
    }
}
