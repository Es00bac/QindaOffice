// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The message list of the current folder (D-042): search, a filter
// (All · Unread · Flagged · Attachments), day headings, three-line rows
// (sender and time, subject, preview) with unread emphasis, quick actions
// on hover, a context menu, and Up/Down/Delete from the keyboard. The rows
// come from MailService.messageModel, which owns filtering and sorting.
Tk.Box {
    id: list

    property MailService service: null
    property MailHost host: null
    readonly property string selectedId: service !== null ? service.selectedMessageId : ""
    readonly property var filters: [
        { text: qsTr("All"), value: "all" }, { text: qsTr("Unread"), value: "unread" },
        { text: qsTr("Flagged"), value: "flagged" }, { text: qsTr("Attachments"), value: "attachments" }]

    color: Tk.Theme.color.surface
    padding: 0

    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function select(messageId) { if (list.service !== null) list.service.selectedMessageId = messageId }
    function step(delta) {
        const next = Math.max(0, Math.min(view.count - 1, view.currentIndex + delta))
        view.currentIndex = next
        const item = view.itemAtIndex(next)
        if (item !== null) list.select(item.messageId)
    }
    function openMoveMenu() {
        if (list.service === null || list.selectedId.length === 0) return
        moveMenu.targets = list.service.folderTargets(list.service.selectedMessage.accountId || "")
        moveMenu.popup()
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.md
            paddingBottom: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Heading {
                    objectName: "messageListTitle"
                    level: 3
                    text: list.service !== null ? list.service.currentFolderName : ""
                    elide: Text.ElideRight
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Caption { text: view.count > 0 ? qsTr("%n message(s)", "", view.count) : "" }
                Tk.IconButton {
                    objectName: "messageListSync"
                    iconName: "refresh-cw"
                    tooltip: qsTr("Send and receive (F5)")
                    enabled: list.service !== null && !list.service.busy
                    onClicked: list.host.actions.trigger("mailSyncAction")
                }
            }
            Tk.SearchField {
                id: search
                objectName: "messageSearch"
                placeholderText: qsTr("Search mail (Ctrl+E)")
                text: list.service !== null ? list.service.searchText : ""
                onSearched: function(value) { if (list.service !== null) list.service.searchText = value }
            }
            Tk.Segmented {
                objectName: "messageFilter"
                small: true
                model: list.filters
                currentIndex: list.service !== null ? Math.max(0, list.filters.map(function(f) { return f.value }).indexOf(list.service.filter)) : 0
                tooltip: qsTr("Show only some messages")
                onActivated: function(index) { list.service.filter = list.filters[index].value }
            }
        }
        Tk.Divider { }
        ListView {
            id: view
            objectName: "messageRows"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            focus: true
            model: list.service !== null ? list.service.messageModel : null
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Messages")
            delegate: MessageRow {
                width: view.width
                // The first message of each day carries the day's heading.
                showDay: index === 0 || String(view.model.get(index - 1).dayGroup || "") !== dayGroup
                selected: list.selectedId === messageId
                onClicked: { view.currentIndex = index; list.select(messageId); view.forceActiveFocus() }
                onQuickAction: function(action) {
                    list.select(messageId)
                    list.host.actions.trigger(action)
                }
                onContextRequested: function(point) { list.select(messageId); contextMenu.popup() }
            }
            Keys.onUpPressed: list.step(-1)
            Keys.onDownPressed: list.step(1)
            Keys.onDeletePressed: list.host.actions.trigger("mailDeleteAction")
            Tk.ScrollBar.vertical: Tk.ScrollBar { }

            Tk.EmptyState {
                anchors.centerIn: parent
                visible: view.count === 0
                iconName: list.service !== null && list.service.searchText.length > 0 ? "search" : "inbox"
                title: list.service !== null && list.service.searchText.length > 0 ? qsTr("No matches") : qsTr("Nothing here")
                text: list.service !== null && list.service.searchText.length > 0
                      ? qsTr("No message in this folder matches “%1”.").arg(list.service.searchText)
                      : qsTr("New mail arrives on the next sync.")
            }
        }
    }

    Tk.Menu {
        id: contextMenu
        objectName: "messageContextMenu"
        Repeater {
            model: ["mailReplyAction", "mailReplyAllAction", "mailForwardAction", "|", "mailToggleReadAction",
                    "mailToggleFlagAction", "mailMoveAction", "|", "mailArchiveAction", "mailDeleteAction"]
            delegate: Tk.MenuItem {
                required property string modelData
                readonly property var entry: modelData === "|" ? null : list.host.actions.entry(modelData)
                visible: entry !== null
                height: visible ? implicitHeight : 0
                text: entry !== null ? entry.text : ""
                iconName: entry !== null ? entry.iconName : ""
                shortcut: entry !== null ? entry.shortcutText : ""
                enabled: entry !== null && entry.enabled
                onTriggered: entry.trigger()
            }
        }
    }
    Tk.Menu {
        id: moveMenu
        objectName: "messageMoveMenu"
        property var targets: []
        title: qsTr("Move to")
        Repeater {
            model: moveMenu.targets
            delegate: Tk.MenuItem {
                required property var modelData
                text: String(modelData.name || "")
                iconName: String(modelData.iconName || "folder")
                leftPadding: Tk.Theme.space.md + (modelData.depth || 0) * Tk.Theme.space.lg
                onTriggered: list.service.moveTo([list.selectedId], String(modelData.key))
            }
        }
    }
}
