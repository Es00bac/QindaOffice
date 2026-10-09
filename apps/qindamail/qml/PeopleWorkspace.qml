// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Mail.App

// The People workspace (M64, D-072): QindaMail's address book beside Mail,
// Calendar and Tasks, as Outlook's People — a searchable list of contacts
// and the selected contact's card (ContactEditor). New / Delete / Import /
// Export are the host's commands (contactNewAction, contactDeleteAction,
// contactImportAction, contactExportAction); the selection is reported to
// the host (MailHost.selectedContactId) so Delete Contact enables.
Item {
    id: people

    property MailService service: null
    property MailHost host: null
    property string selectedId: ""
    property string query: ""
    readonly property var store: service !== null ? service.contacts : null
    readonly property var rows: {
        const all = people.store !== null ? people.store.contacts : []
        return people.query.trim().length > 0 && people.store !== null ? people.store.search(people.query, 500) : all
    }

    function select(id) {
        people.selectedId = id
        if (people.host !== null) people.host.selectedContactId = id
        editor.edit(id.length > 0 && people.store !== null ? people.store.contactById(id) : ({}))
    }
    function newContact() {
        people.select("")
        editor.focusFirst()
    }
    function deleteContact(id) {
        const contact = people.store !== null ? people.store.contactById(id) : ({})
        if (!contact || !contact.id) return
        confirmDelete.contactId = id
        confirmDelete.text = qsTr("Delete %1 from your contacts? This cannot be undone.").arg(String(contact.displayName || qsTr("this contact")))
        confirmDelete.open()
    }
    function initials(name) {
        const words = String(name || "?").trim().split(/\s+/).filter(Boolean)
        return words.length === 0 ? "?" : (words[0].charAt(0) + (words.length > 1 ? words[words.length - 1].charAt(0) : "")).toUpperCase()
    }

    Tk.Splitter {
        objectName: "peopleSplitter"
        anchors.fill: parent
        orientation: Qt.Horizontal
        tooltip: qsTr("Resize the people panes")
        Tk.Box {
            objectName: "peopleListPane"
            T.SplitView.preferredWidth: Tk.Theme.size.panelMinWidth * 1.6
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
            color: Tk.Theme.color.surface
            padding: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: 0
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    padding: Tk.Theme.space.md
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Heading { objectName: "peopleTitle"; level: 3; text: qsTr("People"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                        Tk.Caption { objectName: "peopleCount"; text: qsTr("%n contact(s)", "", people.store !== null ? people.store.count : 0) }
                        Tk.IconButton {
                            objectName: "peopleNew"
                            iconName: "user-plus"
                            tooltip: qsTr("New contact (Ctrl+Shift+C)")
                            onClicked: people.host.actions.trigger("contactNewAction")
                        }
                    }
                    Tk.SearchField {
                        objectName: "peopleSearch"
                        placeholderText: qsTr("Search people")
                        onSearched: function(value) { people.query = value }
                    }
                }
                Tk.Divider { }
                ListView {
                    id: view
                    objectName: "peopleRows"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    clip: true
                    model: people.rows
                    boundsBehavior: Flickable.StopAtBounds
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Contacts")
                    delegate: T.ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool selected: people.selectedId === String(modelData.id)
                        readonly property var primary: (modelData.emails || []).length > 0 ? modelData.emails[0].value : ""
                        objectName: "peopleRow_" + String(modelData.id)
                        width: view.width
                        implicitHeight: Tk.Theme.size.rowLg + Tk.Theme.space.sm
                        hoverEnabled: true
                        Accessible.role: Accessible.ListItem
                        Accessible.name: String(modelData.displayName || "")
                        onClicked: people.select(String(modelData.id))
                        background: Rectangle {
                            color: row.selected ? Tk.Theme.color.selection : row.hovered ? Tk.Theme.color.hover : Tk.Theme.alpha(Tk.Theme.color.hover, Tk.Theme.opacity.hidden)
                        }
                        contentItem: Tk.Flex {
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            gap: Tk.Theme.space.sm
                            padding: Tk.Theme.space.sm
                            Rectangle {
                                Tk.Flex.shrink: 0
                                implicitWidth: Tk.Theme.size.chip
                                implicitHeight: implicitWidth
                                radius: width / 2
                                color: Tk.Theme.color.accentSubtle
                                clip: true
                                Tk.Label { anchors.centerIn: parent; visible: avatar.status !== Image.Ready; text: people.initials(row.modelData.displayName); color: Tk.Theme.color.accentText; font.weight: Font.DemiBold }
                                Image { id: avatar; anchors.fill: parent; source: String(row.modelData.photoUrl || ""); fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: width * 2 }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                gap: 0
                                Tk.Flex.grow: 1
                                Tk.Flex.basis: 0
                                Tk.Label { text: String(row.modelData.displayName || qsTr("(no name)")); font.weight: Font.DemiBold; elide: Text.ElideRight; width: parent.width }
                                Tk.Caption { text: row.primary.length > 0 ? row.primary : String(row.modelData.organization || ""); elide: Text.ElideRight; width: parent.width }
                            }
                        }
                    }
                    Tk.ScrollBar.vertical: Tk.ScrollBar { }
                    Tk.EmptyState {
                        anchors.centerIn: parent
                        width: parent.width - Tk.Theme.space.lg * 2
                        visible: view.count === 0
                        iconName: people.query.length > 0 ? "search" : "book-user"
                        title: people.query.length > 0 ? qsTr("No matches") : qsTr("No contacts yet")
                        text: people.query.length > 0 ? qsTr("Try another name, address or company.")
                                                       : qsTr("Add one, import a vCard or an Outlook CSV file, or add a sender from a message.")
                    }
                }
            }
        }
        ContactEditor {
            id: editor
            objectName: "contactEditor"
            T.SplitView.fillWidth: true
            T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth * 1.5
            service: people.service
            host: people.host
            onSaved: function(contactId) { people.selectedId = contactId; if (people.host !== null) people.host.selectedContactId = contactId }
            onDeleteRequested: function(contactId) { people.deleteContact(contactId) }
        }
    }

    Tk.MessageDialog {
        id: confirmDelete
        objectName: "contactDeleteDialog"
        property string contactId: ""
        title: qsTr("Delete contact")
        variant: "warning"
        primaryText: qsTr("Delete")
        secondaryText: qsTr("Cancel")
        destructive: true
        onAccepted: {
            const result = people.store.remove(contactId)
            if (result.ok) people.select("")
        }
    }
}
