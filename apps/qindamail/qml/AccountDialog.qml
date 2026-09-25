// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Add or edit an account (D-042, D-043): pick a kind, give the address
// and a password (or sign in with Google / Microsoft), let autoconfig fill
// the servers (editable under "Server settings"), test, save. Passwords
// go straight to the keyring through the service; nothing here keeps them.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string step: "choose"            // choose | details | testing
    property string kind: ""
    property var account: ({})
    property string editingId: ""
    property string lookupId: ""
    property string testId: ""
    property var testResult: null
    property string autoNote: ""
    readonly property bool oauthKind: kind === "gmail" || kind === "microsoft365"
    readonly property bool mailKind: kind === "gmail" || kind === "imap" || kind === "pop3" || kind === "microsoft365" || kind === "exchange"
    readonly property bool calendarUrlKind: kind === "caldav" || kind === "ics" || kind === "exchange"
    readonly property var kinds: [
        { id: "gmail", title: qsTr("Google"), text: qsTr("Gmail and Google Calendar"), iconName: "mail" },
        { id: "microsoft365", title: qsTr("Microsoft"), text: qsTr("Outlook.com and Microsoft 365"), iconName: "building" },
        { id: "imap", title: qsTr("Email (IMAP)"), text: qsTr("iCloud, Fastmail, Yahoo, your own server…"), iconName: "at-sign" },
        { id: "pop3", title: qsTr("Email (POP3)"), text: qsTr("Download mail to this computer"), iconName: "inbox" },
        { id: "exchange", title: qsTr("Exchange"), text: qsTr("An Exchange Server calendar (EWS)"), iconName: "building" },
        { id: "caldav", title: qsTr("CalDAV"), text: qsTr("Nextcloud, iCloud, Fastmail calendars"), iconName: "calendar" },
        { id: "ics", title: qsTr("Calendar feed"), text: qsTr("A published .ics / webcal link"), iconName: "calendar-range" }]

    objectName: "accountDialog"
    title: editingId.length > 0 ? qsTr("Account settings") : qsTr("Add an account")
    subtitle: step === "choose" ? qsTr("What kind of account?") : String((kinds.find(function(k) { return k.id === dialog.kind }) || {}).title || "")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: step === "choose" ? "" : step === "testing" ? qsTr("Save") : (oauthKind && !appPassword.checked ? qsTr("Save and sign in") : qsTr("Test and save"))
    primaryEnabled: step === "testing" ? (testResult !== null && testResult.state === "accepted") : step === "details" && emailField.text.trim().length > 0
    secondaryText: qsTr("Cancel")
    tertiaryText: step === "details" && editingId.length === 0 ? qsTr("Back") : editingId.length > 0 ? qsTr("Remove account") : ""
    destructive: false

    function openFor(accountId) {
        editingId = accountId || ""
        testResult = null; autoNote = ""
        if (editingId.length > 0) {
            account = service.accountStore.accountById(editingId)
            kind = String(account.kind || "imap")
            loadFields()
            step = "details"
        } else {
            account = ({}); kind = ""; step = "choose"
            nameField.text = ""; emailField.text = ""; passwordField.text = ""; urlField.text = ""
        }
        open()
    }
    function choose(k) {
        kind = k
        account = service.accountStore.draftFor(k, "", "")
        loadFields()
        step = "details"
        emailField.forceActiveFocus()
    }
    function loadFields() {
        nameField.text = String(account.fullName || "")
        emailField.text = String(account.emailAddress || "")
        const incoming = account.incoming || {}, outgoing = account.outgoing || {}, calendar = account.calendar || {}
        inHost.text = String(incoming.host || ""); inPort.text = incoming.port ? String(incoming.port) : ""
        outHost.text = String(outgoing.host || ""); outPort.text = outgoing.port ? String(outgoing.port) : ""
        userField.text = String(incoming.userName || "")
        urlField.text = String(calendar.url || "")
        passwordField.text = ""
    }
    function collect() {
        const out = Object.assign({}, account)
        out.kind = kind
        out.fullName = nameField.text.trim()
        out.emailAddress = emailField.text.trim()
        out.displayName = String(account.displayName || "") || out.emailAddress
        out.incoming = Object.assign({}, account.incoming || {}, { host: inHost.text.trim(), port: Number(inPort.text) || 0,
                                                                   userName: userField.text.trim() || out.emailAddress })
        out.outgoing = Object.assign({}, account.outgoing || {}, { host: outHost.text.trim(), port: Number(outPort.text) || 0 })
        if (calendarUrlKind) out.calendar = Object.assign({}, account.calendar || {}, { url: urlField.text.trim() })
        if (oauthKind && appPassword.checked) {
            out.incoming.authMechanism = "plain"; out.outgoing.authMechanism = "plain"
        }
        return out
    }
    function lookup() {
        if (!mailKind || editingId.length > 0 || emailField.text.indexOf("@") < 1) return
        lookupId = "lookup-" + Date.now()
        service.autoConfig.lookup(lookupId, emailField.text.trim())
    }
    onAccepted: {
        if (step === "details" && oauthKind && !appPassword.checked) {
            const saved = editingId.length > 0 ? service.updateAccount(collect()) : service.addAccount(collect(), "")
            if (saved.ok) service.signIn(String(saved.id || editingId))
            return
        }
        if (step === "details") {
            step = "testing"; testResult = null
            testId = "test-" + Date.now()
            service.testAccount(testId, collect(), passwordField.text)
            open()      // the dialog stays up while the test runs
            return
        }
        if (step === "testing") {
            if (editingId.length > 0) service.updateAccount(collect(), passwordField.text)
            else service.addAccount(collect(), passwordField.text)
        }
    }
    onTertiary: {
        if (editingId.length > 0) { service.removeAccount(editingId); return }
        step = "choose"; open()
    }
    Connections {
        target: dialog.service !== null ? dialog.service.autoConfig : null
        function onFinished(requestId, result) {
            if (requestId !== dialog.lookupId || !result.ok) return
            dialog.account = Object.assign({}, dialog.account, result.account || {})
            dialog.autoNote = String(result.note || result.providerName || "")
            inHost.text = String((result.account.incoming || {}).host || inHost.text)
            inPort.text = String((result.account.incoming || {}).port || inPort.text)
            outHost.text = String((result.account.outgoing || {}).host || outHost.text)
            outPort.text = String((result.account.outgoing || {}).port || outPort.text)
        }
    }
    Connections {
        target: dialog.service
        function onAccountTestFinished(requestId, result) { if (requestId === dialog.testId) dialog.testResult = result }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        // ---- step 1: the kind ----
        Tk.Grid {
            objectName: "accountKinds"
            visible: dialog.step === "choose"
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.sm
            Repeater {
                model: dialog.kinds
                delegate: Tk.Card {
                    id: kindCard
                    required property var modelData
                    objectName: "accountKind_" + modelData.id
                    interactive: true
                    tooltip: modelData.title + " — " + modelData.text
                    Accessible.role: Accessible.Button
                    onClicked: dialog.choose(modelData.id)
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.md
                        Tk.Icon { name: kindCard.modelData.iconName; size: Tk.Theme.size.iconXl; color: Tk.Theme.color.accent }
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: 0
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Tk.Label { text: kindCard.modelData.title; font.weight: Font.DemiBold }
                            Tk.Caption { text: kindCard.modelData.text; wrapMode: Text.Wrap; width: parent.width }
                        }
                    }
                }
            }
        }

        // ---- step 2: details ----
        Tk.Flex {
            visible: dialog.step === "details"
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Your name"); Tk.TextField { id: nameField; objectName: "accountName"; placeholderText: qsTr("Shown to people you write to") } }
            Tk.PropertyRow {
                label: dialog.kind === "ics" ? qsTr("Name") : qsTr("Email address")
                Tk.TextField { id: emailField; objectName: "accountEmail"; placeholderText: dialog.kind === "ics" ? qsTr("Holidays") : "you@example.com"; onEditingFinished: dialog.lookup() }
            }
            Tk.Switch {
                id: appPassword
                objectName: "accountAppPassword"
                visible: dialog.kind === "gmail"
                text: qsTr("Use an app password instead of signing in (mail only)")
            }
            Tk.PropertyRow {
                visible: dialog.kind !== "ics" && (!dialog.oauthKind || appPassword.checked)
                label: dialog.kind === "gmail" ? qsTr("App password") : qsTr("Password")
                hint: dialog.editingId.length > 0 ? qsTr("Leave empty to keep the saved password") : qsTr("Kept in your desktop keyring, never on disk")
                Tk.TextField { id: passwordField; objectName: "accountPassword"; echoMode: TextInput.Password }
            }
            Tk.Notice {
                visible: dialog.oauthKind && !appPassword.checked
                variant: "info"
                title: qsTr("Sign in with your browser")
                text: qsTr("QindaMail opens your browser to sign in; the token stays in your keyring. Signing in needs an OAuth client ID configured for this computer (see Mail ▸ Accounts help in docs/mail.md).")
            }
            Tk.PropertyRow {
                visible: dialog.calendarUrlKind
                label: dialog.kind === "exchange" ? qsTr("Server") : qsTr("Calendar URL")
                Tk.TextField { id: urlField; objectName: "accountUrl"; placeholderText: dialog.kind === "exchange" ? "https://mail.example.com/EWS/Exchange.asmx" : "https://…" }
            }
            Tk.Caption { visible: dialog.autoNote.length > 0; text: dialog.autoNote; wrapMode: Text.Wrap }
            Tk.FormGroup {
                objectName: "accountServers"
                visible: dialog.mailKind
                title: qsTr("Server settings")
                collapsible: true
                collapsed: true
                Tk.PropertyRow { label: qsTr("Incoming"); Tk.Flex { direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
                    Tk.TextField { id: inHost; objectName: "accountInHost"; Tk.Flex.grow: 1; placeholderText: "imap.example.com" }
                    Tk.TextField { id: inPort; objectName: "accountInPort"; implicitWidth: Tk.Theme.size.controlLg * 2; placeholderText: "993" } } }
                Tk.PropertyRow { label: qsTr("Outgoing"); Tk.Flex { direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
                    Tk.TextField { id: outHost; objectName: "accountOutHost"; Tk.Flex.grow: 1; placeholderText: "smtp.example.com" }
                    Tk.TextField { id: outPort; objectName: "accountOutPort"; implicitWidth: Tk.Theme.size.controlLg * 2; placeholderText: "587" } } }
                Tk.PropertyRow { label: qsTr("User name"); Tk.TextField { id: userField; objectName: "accountUser"; placeholderText: qsTr("Usually your email address") } }
            }
        }

        // ---- step 3: test ----
        Tk.Flex {
            visible: dialog.step === "testing"
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex {
                visible: dialog.testResult === null
                direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                Tk.Spinner { }
                Tk.Label { text: qsTr("Connecting to %1…").arg(inHost.text || urlField.text || emailField.text) }
            }
            Tk.Notice {
                objectName: "accountTestResult"
                visible: dialog.testResult !== null
                variant: dialog.testResult !== null && dialog.testResult.state === "accepted" ? "success" : "danger"
                title: dialog.testResult !== null && dialog.testResult.state === "accepted" ? qsTr("Everything works") : qsTr("That did not work")
                text: dialog.testResult !== null ? String(dialog.testResult.error || qsTr("The servers accepted the sign-in.")) : ""
            }
        }
    }
}
