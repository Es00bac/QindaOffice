// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The reading pane (D-042): subject, sender card, recipients, the actions
// for this message, the body and its attachments. Bodies are plain text —
// the providers reduce HTML mail to text and never fetch remote content —
// so the pane shows exactly what arrived, selectable for copying.
Tk.Box {
    id: pane

    property MailService service: null
    property MailHost host: null
    readonly property var message: service !== null ? service.selectedMessage : ({})
    readonly property bool hasMessage: message !== null && message !== undefined && String(message.messageId || "").length > 0

    color: Tk.Theme.color.surface
    padding: 0

    function initials(name) {
        const words = String(name || "?").replace(/<.*>/, "").trim().split(/\s+/).filter(Boolean)
        return words.length === 0 ? "?" : (words[0].charAt(0) + (words.length > 1 ? words[words.length - 1].charAt(0) : "")).toUpperCase()
    }
    function fullDate(iso) {
        const date = new Date(String(iso || ""))
        return isNaN(date.getTime()) ? "" : date.toLocaleString(Qt.locale(), Locale.LongFormat)
    }
    function sizeText(bytes) {
        const n = Number(bytes || 0)
        return n >= 1048576 ? qsTr("%1 MB").arg((n / 1048576).toFixed(1)) : n >= 1024 ? qsTr("%1 KB").arg(Math.round(n / 1024)) : qsTr("%1 B").arg(n)
    }

    Tk.EmptyState {
        anchors.centerIn: parent
        visible: !pane.hasMessage
        iconName: "mail-open"
        title: qsTr("No message selected")
        text: qsTr("Pick a message to read it here.")
    }

    Tk.Scroll {
        id: scroll
        objectName: "readingScroll"
        anchors.fill: parent
        visible: pane.hasMessage
        Tk.Flex {
            width: scroll.viewportWidth
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            padding: Tk.Theme.space.xl
            Tk.Heading {
                objectName: "readingSubject"
                level: 2
                text: String(pane.message.subject || qsTr("(no subject)"))
                wrapMode: Text.Wrap
                width: parent.width - Tk.Theme.space.xl * 2
            }
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.md
                Rectangle {
                    objectName: "readingAvatar"
                    width: Tk.Theme.size.chip + Tk.Theme.space.xs
                    height: width
                    radius: width / 2
                    color: Tk.Theme.color.accentSubtle
                    border.width: Tk.Theme.size.border
                    border.color: Tk.Theme.color.controlActiveBorder
                    Tk.Label { anchors.centerIn: parent; text: pane.initials(pane.message.sender); color: Tk.Theme.color.accentText; font.weight: Font.DemiBold }
                }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    Tk.Label { objectName: "readingSender"; text: String(pane.message.sender || ""); font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Tk.Caption { text: qsTr("To %1").arg(String(pane.message.to || "")); elide: Text.ElideRight; width: parent.width }
                    Tk.Caption { visible: String(pane.message.cc || "").length > 0; text: qsTr("Cc %1").arg(String(pane.message.cc || "")); elide: Text.ElideRight; width: parent.width }
                }
                Tk.Caption { objectName: "readingDate"; text: pane.fullDate(pane.message.date) }
            }
            Tk.Flex {
                objectName: "readingActions"
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.xs
                Repeater {
                    model: [{ id: "mailReplyAction", accent: true }, { id: "mailReplyAllAction" }, { id: "mailForwardAction" },
                            { id: "mailArchiveAction" }, { id: "mailToggleFlagAction" }, { id: "mailDeleteAction" }]
                    delegate: Tk.Button {
                        required property var modelData
                        readonly property var entry: pane.host !== null ? pane.host.actions.entry(modelData.id) : null
                        objectName: "readingAction_" + modelData.id
                        visible: entry !== null
                        small: true
                        variant: modelData.accent ? "outline" : "ghost"
                        text: entry !== null ? entry.plainText : ""
                        iconName: entry !== null ? entry.iconName : ""
                        tooltip: entry !== null ? entry.shortcutText : ""
                        available: entry !== null && entry.enabled
                        onClicked: entry.trigger()
                    }
                }
            }
            Tk.Divider { width: parent.width - Tk.Theme.space.xl * 2 }
            TextEdit {
                objectName: "readingBody"
                width: parent.width - Tk.Theme.space.xl * 2
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                text: String(pane.message.bodyPlain || pane.message.snippet || "")
                color: Tk.Theme.color.text
                selectionColor: Tk.Theme.color.selection
                selectedTextColor: Tk.Theme.color.text
                font.family: Tk.Theme.font.family
                font.pixelSize: Tk.Theme.font.medium
                Accessible.role: Accessible.Document
                Accessible.name: qsTr("Message body")
            }
            Tk.Notice {
                visible: pane.message.bodyTruncated === true
                variant: "info"
                title: qsTr("Only the beginning was downloaded")
                text: qsTr("This message is larger than the download limit; open it in webmail for the rest.")
            }
            Tk.Flex {
                objectName: "readingAttachments"
                visible: (pane.message.attachments || []).length > 0
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                width: parent.width - Tk.Theme.space.xl * 2
                Repeater {
                    model: pane.message.attachments || []
                    delegate: Tk.Chip {
                        required property var modelData
                        objectName: "readingAttachment_" + String(modelData.id || "")
                        iconName: modelData.state === "local" ? "paperclip" : "download"
                        text: String(modelData.name || qsTr("attachment")) + " · " + pane.sizeText(modelData.size)
                        tooltip: modelData.state === "local" ? qsTr("Open %1").arg(modelData.name) : qsTr("Download %1").arg(modelData.name)
                        onClicked: modelData.state === "local" ? pane.service.openAttachment(pane.message.messageId, modelData.id)
                                                              : pane.service.fetchAttachment(pane.message.messageId, modelData.id)
                    }
                }
            }
        }
    }
}
