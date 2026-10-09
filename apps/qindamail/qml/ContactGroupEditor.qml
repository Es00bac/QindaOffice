// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// A contact group in the People workspace (M76, D-085), as Outlook's
// Contact Group: its name, its members — contacts of the address book or
// plain e-mail addresses, added through the same suggestions as the
// composer's To field (groups too: a group may hold groups) — and notes.
// Save writes the address book (ContactStore, kind "group"); New Message
// opens a composer addressed to every member.
//
// AGENT-CONTRACT (office_mail contact.h, ContactStore): members are given
// back as {contactId} for a card (the store turns it into "urn:uuid:<uid>")
// or {uri, name} for an address; contactById() returns them resolved
// [{uri, name, address, displayName, kind contact|group|address|missing,
// contactId}].
Tk.Box {
    id: editor

    property MailService service: null
    property MailHost host: null
    property var group: ({})
    property var members: []
    property bool dirty: false
    property string error: ""
    readonly property bool isNew: String(group.id || "").length === 0
    readonly property var store: service !== null ? service.contacts : null
    readonly property RecipientCompleter completer: RecipientCompleter { service: editor.service; limit: 10 }
    signal saved(string contactId)
    signal deleteRequested(string contactId)

    color: Tk.Theme.color.surface
    padding: 0

    function edit(map) {
        editor.group = map || ({})
        nameField.text = String(editor.group.formattedName || editor.group.displayName || "")
        notes.text = String(editor.group.notes || "")
        editor.members = (editor.group.members || []).map(function(m) { return Object.assign({}, m) })
        editor.dirty = false
        editor.error = ""
    }
    function focusFirst() { nameField.forceActiveFocus() }
    // A suggestion or a typed address becomes a member: a contact of the
    // address book when its address is one, else the address itself.
    function addTokens(tokens) {
        const next = editor.members.slice()
        for (let i = 0; i < tokens.length; ++i) {
            const label = String(tokens[i].label || "")
            const groupId = editor.completer.groupIdFor(label)
            let member = null
            if (groupId.length > 0) {
                const g = editor.store.contactById(groupId)
                member = { contactId: groupId, displayName: String(g.displayName || label), kind: "group", address: "" }
            } else {
                const match = /^(.*)<([^<>]+)>\s*$/.exec(label)
                const address = String(tokens[i].value || (match ? match[2] : label)).trim()
                const name = match ? match[1].replace(/^"|"\s*$/g, "").trim() : ""
                if (address.indexOf("@") < 1) { editor.error = qsTr("“%1” is not an e-mail address").arg(label); continue }
                const known = editor.store.contactForAddress(address)
                member = known.id ? { contactId: String(known.id), displayName: String(known.displayName), kind: "contact", address: address }
                                  : { uri: "mailto:" + address, name: name, displayName: name || address, kind: "address", address: address }
            }
            const duplicate = next.some(function(m) {
                return (member.contactId && m.contactId === member.contactId) || (!member.contactId && m.uri === member.uri) })
            if (!duplicate) next.push(member)
        }
        editor.members = next
        editor.dirty = true
    }
    function removeAt(index) {
        const next = editor.members.slice()
        next.splice(index, 1)
        editor.members = next
        editor.dirty = true
    }
    function collect() {
        return { id: String(editor.group.id || ""), kind: "group", displayName: nameField.text.trim(), notes: notes.text,
                 members: editor.members.map(function(m) {
                     return m.contactId ? { contactId: m.contactId } : { uri: m.uri, name: m.name || "" } }) }
    }
    function save() {
        if (editor.store === null) return false
        const map = editor.collect()
        if (editor.isNew) delete map.id
        const result = editor.isNew ? editor.store.add(map) : editor.store.update(map)
        if (!result.ok) {
            editor.error = String(result.error || qsTr("The group was not saved"))
            return false
        }
        editor.edit(editor.store.contactById(result.id))
        editor.saved(String(result.id))
        return true
    }
    function mailGroup() {
        if (editor.service === null || editor.host === null || editor.isNew) return
        const expanded = editor.store.expandGroup(String(editor.group.id))
        const draft = editor.service.newDraft("")
        draft.to = (expanded.recipients || []).join(", ")
        editor.host.compose(draft)
    }

    Tk.Scroll {
        id: scroll
        objectName: "contactGroupScroll"
        anchors.fill: parent
        Tk.Flex {
            id: form
            width: scroll.viewportWidth
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.xl
            // AGENT-GUARD: every row takes this width (as in ContactEditor);
            // left to their implicit widths the rows overflow at 520 px.
            readonly property real inner: width - Tk.Theme.space.xl * 2

            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Center
                gap: Tk.Theme.space.md
                width: form.inner
                Tk.Icon { name: "users-round"; size: Tk.Theme.size.iconXl; color: Tk.Theme.color.accent; Tk.Flex.shrink: 0 }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    Tk.Heading {
                        objectName: "contactGroupTitle"
                        level: 2
                        text: editor.isNew ? qsTr("New contact group") : String(editor.group.displayName || "")
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.xs
                        width: parent.width
                        Tk.Button { objectName: "contactGroupSave"; small: true; variant: "accent"; iconName: "save"; text: qsTr("Save"); tooltip: qsTr("Save this group"); available: editor.dirty || editor.isNew; onClicked: editor.save() }
                        Tk.Button { objectName: "contactGroupRevert"; small: true; variant: "ghost"; text: qsTr("Revert"); tooltip: qsTr("Undo the changes since the last save"); available: editor.dirty && !editor.isNew; onClicked: editor.edit(editor.store.contactById(String(editor.group.id))) }
                        Tk.Button { objectName: "contactGroupMail"; small: true; variant: "ghost"; iconName: "mail-plus"; text: qsTr("New message"); tooltip: qsTr("Write to everyone in the group"); available: !editor.isNew && editor.members.length > 0; onClicked: editor.mailGroup() }
                        Tk.Button { objectName: "contactGroupDelete"; small: true; variant: "ghost"; iconName: "trash-2"; text: qsTr("Delete"); tooltip: qsTr("Delete this group"); visible: !editor.isNew; onClicked: editor.deleteRequested(String(editor.group.id)) }
                    }
                }
            }
            Tk.Notice { objectName: "contactGroupError"; width: form.inner; visible: editor.error.length > 0; variant: "danger"; title: qsTr("Check the group"); text: editor.error }
            Tk.PropertyRow {
                width: form.inner
                label: qsTr("Group name")
                Tk.TextField { id: nameField; objectName: "contactGroupName"; placeholderText: qsTr("Book club"); onTextEdited: editor.dirty = true }
            }
            Tk.PropertyRow {
                width: form.inner
                label: qsTr("Add members")
                hint: qsTr("Type a name or an e-mail address; contacts and groups are suggested")
                Tk.TokenField {
                    id: addField
                    objectName: "contactGroupAdd"
                    standalone: true
                    completionProvider: editor.completer
                    placeholderText: qsTr("Name or address")
                    tooltip: qsTr("Members · press Enter after each address")
                    onTokensEdited: function(tokens) {
                        if (tokens.length === 0) return
                        editor.addTokens(tokens)
                        Qt.callLater(function() { addField.tokens = [] })
                    }
                }
            }
            Tk.Caption {
                objectName: "contactGroupCount"
                text: qsTr("%n member(s)", "", editor.members.length)
            }
            Repeater {
                model: editor.members
                delegate: Tk.Flex {
                    id: memberRow
                    required property var modelData
                    required property int index
                    objectName: "contactGroupMember_" + index
                    width: form.inner
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Icon {
                        name: memberRow.modelData.kind === "group" ? "users-round" : memberRow.modelData.kind === "missing" ? "circle-alert"
                            : memberRow.modelData.kind === "address" ? "at-sign" : "user-round"
                        size: Tk.Theme.size.iconSm
                        color: memberRow.modelData.kind === "missing" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: 0
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.Label { text: String(memberRow.modelData.displayName || memberRow.modelData.address || ""); elide: Text.ElideRight; width: parent.width }
                        Tk.Caption {
                            text: memberRow.modelData.kind === "missing" ? qsTr("Not in your contacts")
                                : memberRow.modelData.kind === "group" ? qsTr("Contact group") : String(memberRow.modelData.address || "")
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                    Tk.IconButton {
                        objectName: "contactGroupRemove_" + memberRow.index
                        small: true
                        iconName: "x"
                        tooltip: qsTr("Remove %1 from the group").arg(String(memberRow.modelData.displayName || ""))
                        onClicked: editor.removeAt(memberRow.index)
                    }
                }
            }
            Tk.PropertyRow {
                width: form.inner
                label: qsTr("Notes")
                Tk.TextArea { id: notes; objectName: "contactGroupNotes"; rows: 3; onTextChanged: if (activeFocus) editor.dirty = true }
            }
        }
    }
}
