// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Finish & Merge ▸ Send E-mail Messages (M77, D-086): Word's
// Merge to E-mail — the field with the addresses, the subject, the format
// (the message as plain text, or the merged document attached) and which
// records. Each recipient's message opens as a QindaMail composer draft;
// nothing is sent until it is sent from there.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    objectName: "emailMergeDialog"
    title: qsTr("Merge to E-mail")
    dialogWidth: 460
    primaryText: qsTr("OK")

    Connections {
        target: dialog.mailMerge
        function onFinishRequested(target) {
            if (target !== "email") return
            const info = dialog.mailMerge.emailInfo()
            toCombo.currentIndex = Math.max(0, info.fields.indexOf(info.to))
            subject.text = info.subject
            format.currentIndex = info.format === "attachment" ? 1 : 0
            all.checked = true
            some.checked = false
            fromField.value = 1
            toField.value = Math.max(1, info.records)
            dialog.open()
        }
    }
    onAccepted: dialog.mailMerge.sendEmails({ to: dialog.mailMerge.fields[toCombo.currentIndex] || "", subject: subject.text,
                                              format: format.currentIndex === 1 ? "attachment" : "text",
                                              records: all.checked ? "all" : "range", from: fromField.value, to: toField.value })

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("To"); Tk.ComboBox { id: toCombo; objectName: "emailTo"; model: dialog.mailMerge.fields } }
        Tk.PropertyRow { label: qsTr("Subject line"); Tk.TextField { id: subject; objectName: "emailSubject" } }
        Tk.PropertyRow {
            label: qsTr("Mail format")
            Tk.ComboBox { id: format; objectName: "emailFormat"; model: [qsTr("Plain text"), qsTr("Attachment (Word document)")] }
        }
        Tk.Flex {
            gap: Tk.Theme.space.xs
            Tk.RadioButton { id: all; objectName: "emailAll"; text: qsTr("All"); onClicked: some.checked = false }
            Tk.RadioButton { id: some; objectName: "emailRange"; text: qsTr("From"); onClicked: all.checked = false }
            Tk.NumberField { id: fromField; objectName: "emailFrom"; enabled: some.checked; implicitWidth: 70; from: 1; to: Math.max(1, dialog.mailMerge.recipientCount); decimals: 0; onValueModified: function(v) { value = v } }
            Tk.Label { text: qsTr("To") }
            Tk.NumberField { id: toField; objectName: "emailLast"; enabled: some.checked; implicitWidth: 70; from: 1; to: Math.max(1, dialog.mailMerge.recipientCount); decimals: 0; onValueModified: function(v) { value = v } }
        }
        Tk.Caption { text: qsTr("Each message opens in QindaMail as a draft to check and send.") }
    }
}
