// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The "Contacts (CardDAV)" part of the account dialog (M76, D-085): turn
// address-book sync on for an account (or set up a contacts-only CardDAV
// account), give the server — QindaMail suggests it from the address, and
// discovery follows /.well-known/carddav to the principal and its home —
// then Find address books and pick the ones to sync. The password goes to
// the keyring with the account's (`password` here only overrides it for
// the address books: empty = the account's own password, which the dialog
// lends through `accountPassword` so Find address books can sign in).
//
// AGENT-CONTRACT (MailAccount contacts, AccountDialog.qml): load(contacts,
// address) fills the section; settings(base) returns the account's
// `contacts` object; `password` is read by the dialog when it saves.
Tk.FormGroup {
    id: section

    property MailService service: null
    property bool forced: false             // a carddav account: always on
    property bool bearer: false             // the account signs in with OAuth (Google)
    property string emailAddress: ""
    property string accountPassword: ""     // the dialog's password field (never stored here)
    property var found: []                  // [{url, name, enabled}]
    property string discoverId: ""
    property string status: ""
    property bool failed: false
    readonly property bool enabled_: section.forced || syncSwitch.checked
    readonly property string password: passwordField.text

    // The user's choice: the header's toggle, and load() for an account.
    property bool folded: true

    objectName: "addressBookSection"
    title: qsTr("Contacts (CardDAV)")
    collapsible: !section.forced
    // M89 (QindaTK D-317): `collapsed` stays bound — the header offers its
    // toggle through `toggled` first and writes nothing while the binding
    // answers it — so a carddav account's fields always show (M76 set it
    // imperatively because the header wrote it back over the binding).
    collapsed: !section.forced && section.folded
    onToggled: function(collapsed) { section.folded = collapsed }

    function load(contacts, address) {
        const c = contacts || {}
        section.emailAddress = String(address || "")
        syncSwitch.checked = section.forced || c.protocol === "carddav"
        urlField.text = String(c.url || "")
        userField.text = String(c.userName || "")
        passwordField.text = ""
        section.found = (c.addressBooks || []).map(function(b) { return { url: String(b.url), name: String(b.name || b.url), enabled: b.enabled !== false } })
        section.status = section.found.length > 0 ? qsTr("%n address book(s)", "", section.found.length) : ""
        section.failed = false
        section.discoverId = ""
        section.folded = !section.enabled_
    }
    function suggestUrl() {
        if (urlField.text.trim().length === 0 && section.service !== null && section.emailAddress.indexOf("@") > 0)
            urlField.text = section.service.accountStore.contactsUrlFor(section.emailAddress)
    }
    function settings(base) {
        if (!section.enabled_) return Object.assign({}, base || {}, { protocol: "none" })
        return Object.assign({}, base || {}, {
            protocol: "carddav", url: urlField.text.trim(), userName: userField.text.trim() || section.emailAddress,
            authMechanism: section.bearer ? "bearer" : "basic",
            addressBooks: section.found.map(function(b, i) { return { id: "book" + (i + 1), url: b.url, name: b.name, enabled: b.enabled } })
        })
    }
    function discover() {
        if (section.service === null) return
        section.suggestUrl()
        section.discoverId = "books-" + Date.now()
        section.status = qsTr("Looking for address books…")
        section.failed = false
        section.service.discoverAddressBooks(section.discoverId, urlField.text.trim(), userField.text.trim() || section.emailAddress,
                                             passwordField.text.length > 0 ? passwordField.text : section.accountPassword)
    }

    Connections {
        target: section.service
        function onAddressBooksDiscovered(requestId, result) {
            if (requestId !== section.discoverId) return
            section.discoverId = ""
            section.failed = !result.ok
            if (!result.ok) {
                section.status = String(result.error || qsTr("No address book was found"))
                return
            }
            const previous = section.found
            section.found = (result.addressBooks || []).map(function(b) {
                const kept = previous.find(function(p) { return p.url === b.url })
                return { url: String(b.url), name: String(b.name), enabled: kept ? kept.enabled : true }
            })
            section.status = qsTr("%n address book(s) found", "", section.found.length)
        }
    }

    Tk.Switch {
        id: syncSwitch
        objectName: "addressBookSync"
        visible: !section.forced
        text: qsTr("Sync contacts with this account's address books")
        onToggled: if (checked) section.suggestUrl()
    }
    Tk.PropertyRow {
        visible: section.enabled_
        label: qsTr("Server")
        hint: qsTr("Your provider's address (QindaMail finds the address books from there)")
        Tk.TextField { id: urlField; objectName: "addressBookUrl"; placeholderText: "https://contacts.example.com/" }
    }
    Tk.PropertyRow {
        visible: section.enabled_ && !section.bearer
        label: qsTr("User name")
        Tk.TextField { id: userField; objectName: "addressBookUser"; placeholderText: qsTr("Usually your email address") }
    }
    Tk.PropertyRow {
        visible: section.enabled_ && !section.bearer && !section.forced
        label: qsTr("Password")
        hint: qsTr("Leave empty to use the account's password")
        Tk.TextField { id: passwordField; objectName: "addressBookPassword"; echoMode: TextInput.Password }
    }
    Tk.Flex {
        visible: section.enabled_
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Button {
            objectName: "addressBookFind"
            iconName: "search"
            text: qsTr("Find address books")
            available: section.discoverId.length === 0
            onClicked: section.discover()
        }
        Tk.Spinner { visible: section.discoverId.length > 0 }
        Tk.Caption {
            objectName: "addressBookStatus"
            Tk.Flex.grow: 1
            text: section.status
            color: section.failed ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            wrapMode: Text.Wrap
        }
    }
    Repeater {
        model: section.enabled_ ? section.found : []
        delegate: Tk.CheckBox {
            required property var modelData
            required property int index
            objectName: "addressBook_" + index
            text: modelData.name
            checked: modelData.enabled
            onToggled: {
                const next = section.found.slice()
                next[index] = Object.assign({}, next[index], { enabled: checked })
                section.found = next
            }
        }
    }
}
