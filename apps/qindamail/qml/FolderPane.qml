// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Accounts and their folders (D-042): the service's flattened folder tree.
// Account rows are headers (name, sync state, sign-in when a token died) and
// fold their folders; folder rows show the unread count. "All inboxes" sits
// on top. Selecting a row sets MailService.currentFolderKey — the message
// list follows the service, so every window agrees on nothing but the data.
Tk.Box {
    id: pane

    property MailService service: null
    property MailHost host: null

    // AGENT-CONTRACT: the workspace routes this account intent through its
    // window's MailHost; shared service signIn is browser-only (docs/mail.md).
    signal signInRequested(string accountId)

    color: Tk.Theme.color.panel
    padding: 0

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm

        Tk.Button {
            objectName: "folderNewMessage"
            variant: "accent"
            large: true
            iconName: "mail-plus"
            text: qsTr("New message")
            tooltip: qsTr("Write a new message (Ctrl+N)")
            onClicked: pane.host.actions.trigger("mailNewMessageAction")
        }

        ListView {
            id: folders
            objectName: "folderList"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            model: pane.service !== null ? pane.service.folderModel : null
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.Tree
            Accessible.name: qsTr("Folders")
            delegate: Loader {
                id: row
                required property int index
                required property string key
                required property string accountId
                required property string name
                required property int depth
                required property string iconName
                required property int unread
                required property bool selectable
                required property bool isAccount
                required property bool expanded
                width: folders.width
                sourceComponent: isAccount ? accountRow : folderRow
                Component {
                    id: accountRow
                    Tk.Flex {
                        readonly property var account: pane.service.accounts.find(function(a) { return a.id === row.accountId }) || ({})
                        objectName: "folderAccount_" + row.accountId
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        paddingTop: Tk.Theme.space.md
                        paddingLeft: Tk.Theme.space.xs
                        Tk.IconButton {
                            small: true
                            iconName: row.expanded ? "chevron-down" : "chevron-right"
                            tooltip: row.expanded ? qsTr("Collapse %1").arg(row.name) : qsTr("Expand %1").arg(row.name)
                            onClicked: pane.service.toggleFolderExpanded(row.key)
                        }
                        Tk.Overline { title: row.name; Tk.Flex.grow: 1; Tk.Flex.basis: 0; elide: Text.ElideRight }
                        Tk.Spinner { visible: parent.account.status === "syncing"; size: Tk.Theme.size.iconSm }
                        Tk.Icon {
                            visible: parent.account.status === "offline" || parent.account.status === "error"
                            name: parent.account.status === "offline" ? "cloud" : "circle-alert"
                            size: Tk.Theme.size.iconSm
                            color: parent.account.status === "offline" ? Tk.Theme.color.warning : Tk.Theme.color.danger
                            Tk.ToolTip { text: String(parent.parent.account.lastError || ""); visible: hover.hovered }
                            HoverHandler { id: hover }
                        }
                        Tk.Button {
                            objectName: "folderSignIn_" + row.accountId
                            visible: parent.account.status === "needsSignIn"
                            small: true
                            variant: "outline"
                            text: qsTr("Sign in")
                            tooltip: qsTr("Sign in again to %1").arg(row.name)
                            onClicked: pane.signInRequested(row.accountId)
                        }
                    }
                }
                Component {
                    id: folderRow
                    Tk.ListRow {
                        objectName: "folder_" + row.key
                        indent: row.depth * Tk.Theme.space.lg
                        iconName: row.iconName
                        text: row.name
                        enabled: row.selectable
                        selected: pane.service.currentFolderKey === row.key
                        active: row.unread > 0
                        tooltip: row.unread > 0 ? qsTr("%1 · %2 unread").arg(row.name).arg(row.unread) : row.name
                        onClicked: pane.service.currentFolderKey = row.key
                        trailing: Tk.Badge {
                            visible: row.unread > 0
                            text: row.unread > 999 ? "999+" : String(row.unread)
                            variant: "accent"
                        }
                    }
                }
            }
        }

        Tk.Divider { }
        Tk.Button {
            objectName: "folderAddAccount"
            variant: "ghost"
            iconName: "user-plus"
            text: qsTr("Add account")
            tooltip: qsTr("Add a mail or calendar account")
            onClicked: pane.host.actions.trigger("mailAddAccountAction")
        }
    }
}
