// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's Custom follow-up (Ctrl+Shift+G; M81, D-090): a start and a due
// date for the selected messages' flag. A missing start takes the due
// date; a start after the due date is moved to it (follow_up.h).
Tk.Dialog {
    id: dialog

    property MailService service: null
    property var ids: []

    objectName: "followUpDialog"
    title: qsTr("Custom")
    subtitle: qsTr("Flag the message to remind you to follow up on it. After you follow up, mark it complete.")
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")
    tertiaryText: qsTr("Clear Flag")
    primaryEnabled: dueField.text.length > 0 && dueField.valid && startField.valid

    function openFor(ids) {
        dialog.ids = ids
        const current = ids.length > 0 ? service.organizer.followUpOf(ids[0]) : ({})
        const today = new Date()
        const iso = today.getFullYear() + "-" + String(today.getMonth() + 1).padStart(2, "0") + "-" + String(today.getDate()).padStart(2, "0")
        startField.text = String(current.start || iso)
        dueField.text = String(current.due || iso)
        open()
    }
    onAccepted: service.organizer.followUpCustom(ids, startField.text, dueField.text)
    onTertiary: service.organizer.followUp(ids, "clear")

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("Start date"); MailDateField { id: startField; objectName: "followUpStart" } }
        Tk.PropertyRow { label: qsTr("Due date"); MailDateField { id: dueField; objectName: "followUpDue" } }
    }
}
