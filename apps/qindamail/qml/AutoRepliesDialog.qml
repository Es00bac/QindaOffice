// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Outlook's Automatic Replies (M81, D-090) for one account: off, or on —
// always or only between two times — with a reply for people inside the
// organization and one (or none) for people outside, to contacts only or
// anyone. Exchange and Microsoft 365 keep the settings on the server
// (EWS OOF, Graph mailboxSettings); an IMAP server with ManageSieve gets a
// Sieve vacation in its filters (one reply for everyone, a subject, once
// per sender every few days); anything else says plainly why it cannot.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string accountId: ""
    property string requestId: ""
    property string method: "none"
    property bool available: false
    property bool loading: false
    property string error: ""
    property bool foreignVacation: false
    property int page: 0
    // Read when the dialog opens (an invokable's result has no change signal).
    property var accounts: []
    readonly property bool sieve: method === "sieve"
    readonly property var times: {
        const out = []
        for (let m = 0; m < 24 * 60; m += 30) out.push(String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"))
        return out
    }

    objectName: "autoRepliesDialog"
    title: qsTr("Automatic Replies")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")
    primaryEnabled: available && !loading && (!onButton.checked || replyText().trim().length > 0)

    function openFor(accountId) {
        accounts = service !== null ? service.organizer.autoReplyAccounts() : []
        const ids = accounts.map(function(a) { return String(a.id) })
        const start = Math.max(0, ids.indexOf(String(accountId || "")))
        accountBox.currentIndex = start
        open()
        load(ids.length > 0 ? ids[start] : "")
    }
    function load(id) {
        accountId = id
        requestId = ""                          // a late answer for the account shown before is dropped
        loading = false
        accountBox.currentIndex = Math.max(0, accounts.findIndex(function(a) { return String(a.id) === id }))
        const entry = accounts.find(function(a) { return String(a.id) === id }) || ({})
        method = String(entry.method || "none")
        error = String(entry.reason || "")
        available = false
        foreignVacation = false
        if (id.length === 0 || method === "none") return
        loading = true
        requestId = service.organizer.readAutoReplies(id)
    }
    function pad(n) { return String(n).padStart(2, "0") }
    function fill(settings) {
        offButton.checked = !settings.enabled
        onButton.checked = !!settings.enabled
        rangeBox.checked = !!settings.scheduled
        const start = settings.start ? new Date(settings.start) : new Date()
        const end = settings.end ? new Date(settings.end) : new Date(Date.now() + 7 * 86400000)
        const iso = function(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
        startDate.text = iso(start)
        endDate.text = iso(end)
        startTime.currentIndex = Math.max(0, times.indexOf(pad(start.getHours()) + ":" + (start.getMinutes() < 30 ? "00" : "30")))
        endTime.currentIndex = Math.max(0, times.indexOf(pad(end.getHours()) + ":" + (end.getMinutes() < 30 ? "00" : "30")))
        insideText.text = String(settings.internalMessage || "")
        outsideText.text = String(settings.externalMessage || "")
        outsideBox.checked = String(settings.externalAudience || "all") !== "none"
        contactsOnly.checked = settings.externalAudience === "contacts"
        anyone.checked = !contactsOnly.checked
        subjectField.text = String(settings.subject || "")
    }
    function replyText() { return insideText.text.length > 0 ? insideText.text : outsideText.text }
    function instant(date, time) { return new Date(date + "T" + time + ":00").toISOString() }
    function collect() {
        return { enabled: onButton.checked, scheduled: rangeBox.checked,
                 start: rangeBox.checked ? instant(startDate.text, times[startTime.currentIndex]) : "",
                 end: rangeBox.checked ? instant(endDate.text, times[endTime.currentIndex]) : "",
                 internalMessage: insideText.text, externalMessage: sieve ? "" : outsideText.text,
                 externalAudience: !outsideBox.checked ? "none" : contactsOnly.checked ? "contacts" : "all",
                 subject: subjectField.text, days: 1 }
    }
    onAccepted: {
        loading = true
        requestId = service.organizer.writeAutoReplies(accountId, collect())
        open()                                  // stays up until the server answers
    }

    Connections {
        target: dialog.service !== null ? dialog.service.organizer : null
        function onAutoRepliesFinished(requestId, result) {
            if (requestId !== dialog.requestId) return
            const writing = dialog.loading && dialog.available
            dialog.loading = false
            dialog.available = result.available !== false
            dialog.foreignVacation = result.foreignVacation === true
            dialog.error = result.ok ? "" : String(result.error || "")
            if (!result.ok) return
            if (writing) {
                dialog.close()
                dialog.service.notify(qsTr("Automatic replies saved"), dialog.collect().enabled ? qsTr("They are on.") : qsTr("They are off."), "success")
                return
            }
            dialog.fill(result.settings || {})
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Account")
            Tk.ComboBox {
                id: accountBox
                objectName: "autoRepliesAccount"
                small: true
                textRole: "name"
                model: dialog.accounts
                onActivated: function(index) { dialog.load(String(dialog.accounts[index].id)) }
            }
        }
        Tk.Flex {
            visible: dialog.loading
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Spinner { size: Tk.Theme.size.iconSm }
            Tk.Caption { text: qsTr("Asking the server…") }
        }
        Tk.Notice {
            objectName: "autoRepliesProblem"
            visible: dialog.error.length > 0 && !dialog.loading
            variant: dialog.available ? "warning" : "info"
            title: dialog.available ? qsTr("Not saved") : qsTr("Automatic replies are not available for this account")
            text: dialog.error
        }
        Tk.Notice {
            visible: dialog.foreignVacation
            variant: "info"
            text: qsTr("Another mail program also set an automatic reply in this mailbox's filters; QindaMail leaves it alone.")
        }
        Tk.Flex {
            objectName: "autoRepliesForm"
            visible: dialog.available && !dialog.loading
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.RadioButton { id: offButton; objectName: "autoRepliesOff"; text: qsTr("Do not send automatic replies"); checked: true }
            Tk.RadioButton { id: onButton; objectName: "autoRepliesOn"; text: qsTr("Send automatic replies") }
            Tk.CheckBox { id: rangeBox; objectName: "autoRepliesRange"; enabled: onButton.checked; text: qsTr("Only send during this time range:") }
            Tk.PropertyRow {
                label: qsTr("Start time")
                enabled: onButton.checked && rangeBox.checked
                Tk.Flex {
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                    MailDateField { id: startDate; objectName: "autoRepliesStart" }
                    Tk.ComboBox { id: startTime; small: true; visible: !dialog.sieve; model: dialog.times }
                }
            }
            Tk.PropertyRow {
                label: qsTr("End time")
                enabled: onButton.checked && rangeBox.checked
                Tk.Flex {
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                    MailDateField { id: endDate; objectName: "autoRepliesEnd" }
                    Tk.ComboBox { id: endTime; small: true; visible: !dialog.sieve; model: dialog.times }
                }
            }
            Tk.Segmented {
                objectName: "autoRepliesPage"
                visible: !dialog.sieve
                small: true
                model: [{ text: qsTr("Inside my organization"), value: "inside" }, { text: qsTr("Outside my organization"), value: "outside" }]
                currentIndex: dialog.page
                onActivated: function(index) { dialog.page = index }
            }
            Tk.PropertyRow {
                visible: dialog.sieve
                label: qsTr("Subject")
                Tk.TextField { id: subjectField; objectName: "autoRepliesSubject"; placeholderText: qsTr("Out of office") }
            }
            Tk.TextArea {
                id: insideText
                objectName: "autoRepliesInside"
                visible: dialog.sieve || dialog.page === 0
                enabled: onButton.checked
                rows: 5
                placeholderText: dialog.sieve ? qsTr("The reply everyone who writes gets (once every day)") : qsTr("The reply for people in your organization")
            }
            Tk.Flex {
                visible: !dialog.sieve && dialog.page === 1
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.CheckBox { id: outsideBox; objectName: "autoRepliesOutside"; enabled: onButton.checked; text: qsTr("Auto-reply to people outside my organization"); checked: true }
                Tk.Flex {
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.md
                    enabled: onButton.checked && outsideBox.checked
                    Tk.RadioButton { id: contactsOnly; objectName: "autoRepliesContacts"; text: qsTr("My contacts only") }
                    Tk.RadioButton { id: anyone; objectName: "autoRepliesAnyone"; text: qsTr("Anyone outside my organization"); checked: true }
                }
                Tk.TextArea {
                    id: outsideText
                    objectName: "autoRepliesOutsideText"
                    enabled: onButton.checked && outsideBox.checked
                    rows: 5
                    placeholderText: qsTr("The reply for people outside your organization")
                }
            }
        }
    }
}
