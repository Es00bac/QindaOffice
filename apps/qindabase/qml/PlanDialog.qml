// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The last look before a schema change runs: what it does, what it risks,
// whether a backup is made, and the exact SQL (BaseService plans are
// previewed against the real data in a rolled-back transaction first).
Tk.Dialog {
    id: dialog

    property var plan: ({})
    property var callback: null

    title: plan.label || qsTr("Apply change")
    subtitle: qsTr("Checked against your data; nothing has changed yet.")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.5
    primaryText: qsTr("Apply")
    secondaryText: qsTr("Cancel")
    destructive: plan.destructive === true

    function show(p, done) {
        plan = p
        callback = done
        open()
    }
    onAccepted: { const f = callback; callback = null; if (f) f() }
    onRejected: callback = null

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Repeater {
            model: dialog.plan.notes || []
            delegate: Tk.Label { required property string modelData; text: "• " + modelData; wrapMode: Text.Wrap }
        }
        Tk.Notice {
            visible: (dialog.plan.warnings || []).length > 0
            variant: "warning"
            text: (dialog.plan.warnings || []).join("\n")
        }
        Tk.Notice {
            visible: dialog.plan.destructive === true
            variant: "info"
            iconName: "archive"
            text: qsTr("This removes or narrows data, so a backup copy of the whole database is saved first. Undo also reverses it.")
        }
        Tk.Overline { title: qsTr("SQL") }
        Tk.TextArea {
            objectName: "planSql"
            readOnly: true
            mono: true
            rows: 8
            wrapMode: TextEdit.Wrap
            text: dialog.plan.sql || ""
        }
    }
}
