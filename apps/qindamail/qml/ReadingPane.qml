// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Mail.App

// The reading pane (D-042, M64 D-072): subject, sender card, recipients, the
// actions for this message, then the body as Outlook shows it — HTML mail
// formatted (sanitized; pictures from the internet blocked until "Show
// pictures"), inline pictures in place, attached pictures below, plain text
// with clickable links — on paper, selectable for copying; then the
// attachments with open, save and save all.
//
// AGENT-NOTE: the body is a TextEdit whose document MessageView fills; its
// `text` is never bound (message_view.h). The paper is the one colour
// literal QindaQt allows (AGENTS.md rule 3, D-004): mail is written for a
// white page with black ink, on every theme.
Tk.Box {
    id: pane

    property MailService service: null
    property MailHost host: null
    readonly property var message: service !== null ? service.selectedMessage : ({})
    readonly property bool hasMessage: message !== null && message !== undefined && String(message.messageId || "").length > 0
    readonly property var attachments: (message.attachments || []).filter(function(a) {
        return bodyView.hiddenAttachmentIds.indexOf(String(a.id)) < 0 })

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
    function activateLink(link) {
        if (String(link).startsWith("#")) return          // an anchor inside the message: nothing to open
        if (String(link).toLowerCase().startsWith("mailto:")) {
            if (pane.host !== null) pane.host.compose(pane.service.mailtoDraft(link))
        } else if (pane.service !== null) {
            pane.service.openLink(link)
        }
    }
    function meetingWhen(meeting) {
        const start = new Date(String(meeting.startLocal || "").replace(" ", "T"))
        const end = new Date(String(meeting.endLocal || "").replace(" ", "T"))
        if (isNaN(start.getTime())) return ""
        if (meeting.allDay) return start.toLocaleDateString(Qt.locale(), Locale.LongFormat)
        const sameDay = !isNaN(end.getTime()) && start.toDateString() === end.toDateString()
        return start.toLocaleString(Qt.locale(), Locale.LongFormat) + " – "
            + (isNaN(end.getTime()) ? "" : sameDay ? end.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                                                   : end.toLocaleString(Qt.locale(), Locale.LongFormat))
    }
    function meetingStatus(status) {
        return status === "accepted" ? qsTr("You accepted this meeting.")
             : status === "tentative" ? qsTr("You tentatively accepted this meeting.")
             : status === "declined" ? qsTr("You declined this meeting.")
             : qsTr("You have not answered yet.")
    }
    function saveAttachment(attachment) {
        attachmentSaveDialog.attachmentId = String(attachment.id)
        attachmentSaveDialog.selectedFile = pane.host.suggestedSaveUrl(String(attachment.name || ""))
        attachmentSaveDialog.open()
    }

    MessageView {
        id: bodyView
        objectName: "readingBodyView"
        service: pane.service
        messageId: pane.hasMessage ? String(pane.message.messageId) : ""
        document: body.textDocument
        maxImageWidth: body.width
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
            id: column
            width: scroll.viewportWidth
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            padding: Tk.Theme.space.xl
            readonly property real inner: width - Tk.Theme.space.xl * 2
            Tk.Heading {
                objectName: "readingSubject"
                level: 2
                text: String(pane.message.subject || qsTr("(no subject)"))
                wrapMode: Text.Wrap
                width: column.inner
            }
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.md
                width: column.inner
                Rectangle {
                    objectName: "readingAvatar"
                    Tk.Flex.shrink: 0
                    // Implicit size: Tk.Flex sizes its children from it.
                    implicitWidth: Tk.Theme.size.chip + Tk.Theme.space.xs
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: Tk.Theme.color.accentSubtle
                    border.width: Tk.Theme.size.border
                    border.color: Tk.Theme.color.controlActiveBorder
                    Tk.Label { anchors.centerIn: parent; visible: senderPhoto.status !== Image.Ready; text: pane.initials(pane.message.sender); color: Tk.Theme.color.accentText; font.weight: Font.DemiBold }
                    // A contact's picture (a local file of the address book, never a URL from mail).
                    Image {
                        id: senderPhoto
                        objectName: "readingSenderPhoto"
                        anchors.fill: parent
                        anchors.margins: Tk.Theme.size.border
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: width * 2
                        source: pane.service !== null && pane.hasMessage
                                ? String(pane.service.contacts.contactForAddress(String(pane.message.senderAddress || "")).photoUrl || "") : ""
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: 0
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    Tk.Label {
                        objectName: "readingSender"
                        text: String(pane.message.sender || "") + (String(pane.message.senderAddress || "").length > 0
                              && pane.message.senderAddress !== pane.message.sender ? "  <" + pane.message.senderAddress + ">" : "")
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        width: parent.width
                    }
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
                width: column.inner
                Repeater {
                    model: [{ id: "mailReplyAction", accent: true }, { id: "mailReplyAllAction" }, { id: "mailForwardAction" },
                            { id: "mailArchiveAction" }, { id: "mailToggleFlagAction" }, { id: "mailDeleteAction" },
                            { id: "mailAddSenderAction" }]
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
            // M81 (D-090): the follow-up flag, the importance and the categories.
            MessageTagsBar {
                width: column.inner
                service: pane.service
                host: pane.host
                message: pane.message
            }
            Tk.Flex {
                objectName: "readingPicturesBar"
                visible: bodyView.remoteBlocked || (bodyView.senderTrusted && bodyView.remoteImageCount > 0)
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                width: column.inner
                Tk.Icon { name: bodyView.remoteBlocked ? "image-off" : "image"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                Tk.Caption {
                    objectName: "readingPicturesText"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    wrapMode: Text.Wrap
                    text: bodyView.remoteBlocked
                          ? qsTr("Pictures from the internet are not downloaded, so the sender cannot see that you opened this message.")
                          : qsTr("Pictures from %1 are always shown.").arg(bodyView.senderAddress)
                }
                Tk.Button {
                    objectName: "readingShowPictures"
                    visible: bodyView.remoteBlocked
                    small: true
                    variant: "outline"
                    iconName: "image"
                    text: qsTr("Show pictures")
                    tooltip: qsTr("Download the pictures of this message")
                    onClicked: bodyView.showPictures()
                }
                Tk.Button {
                    objectName: "readingTrustSender"
                    visible: bodyView.senderAddress.length > 0
                    small: true
                    variant: "ghost"
                    text: bodyView.senderTrusted ? qsTr("Stop showing them") : qsTr("Always show from this sender")
                    tooltip: bodyView.senderAddress
                    onClicked: bodyView.setSenderTrusted(!bodyView.senderTrusted)
                }
            }
            // A meeting request (iMIP): what, when, where, and the answer.
            Rectangle {
                id: meetingCard
                objectName: "readingMeeting"
                readonly property bool cancelled: String(bodyView.meeting.method || "") === "CANCEL"
                visible: String(bodyView.meeting.method || "") === "REQUEST" || cancelled
                width: column.inner
                // implicitHeight: Tk.Flex lays its children out from their
                // implicit sizes (a plain `height` is overwritten on relayout).
                implicitHeight: meetingColumn.implicitHeight + Tk.Theme.space.md * 2
                radius: Tk.Theme.radius.sm
                color: Tk.Theme.color.accentSubtle
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.controlActiveBorder
                Tk.Flex {
                    id: meetingColumn
                    x: Tk.Theme.space.md
                    y: Tk.Theme.space.md
                    width: parent.width - Tk.Theme.space.md * 2
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        width: parent.width
                        Tk.Icon { name: "calendar-days"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accentText }
                        Tk.Label { objectName: "readingMeetingTitle"; text: String(bodyView.meeting.title || qsTr("Meeting")); font.weight: Font.DemiBold; elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    }
                    Tk.Caption { objectName: "readingMeetingWhen"; text: pane.meetingWhen(bodyView.meeting); wrapMode: Text.Wrap; width: parent.width }
                    Tk.Caption { visible: String(bodyView.meeting.location || "").length > 0; text: qsTr("Where: %1").arg(String(bodyView.meeting.location || "")); wrapMode: Text.Wrap; width: parent.width }
                    Tk.Caption { text: qsTr("Organizer: %1").arg(String(bodyView.meeting.organizerName || bodyView.meeting.organizer || "")); elide: Text.ElideRight; width: parent.width }
                    Tk.Caption {
                        objectName: "readingMeetingStatus"
                        text: meetingCard.cancelled ? qsTr("The organizer cancelled this meeting.")
                                                    : pane.meetingStatus(String(bodyView.meeting.myStatus || ""))
                        width: parent.width
                        wrapMode: Text.Wrap
                    }
                    Tk.Button {
                        objectName: "readingMeetingRemove"
                        visible: meetingCard.cancelled && String(bodyView.meeting.eventId || "").length > 0
                        Tk.Flex.alignSelf: Tk.Flex.Start
                        small: true
                        variant: "outline"
                        iconName: "trash-2"
                        text: qsTr("Remove from calendar")
                        tooltip: qsTr("Take the cancelled meeting out of your calendar")
                        onClicked: pane.service.respondToMeeting(pane.message.messageId, "removed")
                    }
                    Tk.Flex {
                        visible: !meetingCard.cancelled
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.xs
                        width: parent.width
                        Repeater {
                            model: [{ id: "accepted", text: qsTr("Accept"), icon: "check" }, { id: "tentative", text: qsTr("Tentative"), icon: "circle-help" },
                                    { id: "declined", text: qsTr("Decline"), icon: "x" }]
                            delegate: Tk.Button {
                                required property var modelData
                                objectName: "readingMeeting_" + modelData.id
                                small: true
                                variant: String(bodyView.meeting.myStatus || "") === modelData.id ? "accent" : "outline"
                                iconName: modelData.icon
                                text: modelData.text
                                tooltip: bodyView.meeting.canReply ? qsTr("Answer the organizer and update your calendar")
                                                                   : qsTr("Update your calendar (no account here can send the answer)")
                                onClicked: pane.service.respondToMeeting(pane.message.messageId, modelData.id)
                            }
                        }
                    }
                }
            }
            Tk.Flex {
                visible: bodyView.loading
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Spinner { size: Tk.Theme.size.iconSm }
                Tk.Caption { objectName: "readingLoading"; text: qsTr("Downloading the full message…") }
            }
            Rectangle {
                objectName: "readingPaper"
                width: column.inner
                implicitHeight: body.height + Tk.Theme.space.lg * 2
                color: "white"
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.divider
                radius: Tk.Theme.radius.sm
                TextEdit {
                    id: body
                    objectName: "readingBody"
                    x: Tk.Theme.space.lg
                    y: Tk.Theme.space.lg
                    width: parent.width - Tk.Theme.space.lg * 2
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    color: "black"
                    selectionColor: Tk.Theme.color.selection
                    selectedTextColor: "black"
                    font.family: Tk.Theme.font.family
                    font.pixelSize: Tk.Theme.font.medium
                    Accessible.role: Accessible.Document
                    Accessible.name: qsTr("Message body")
                    onLinkActivated: function(link) { pane.activateLink(link) }
                    onHoveredLinkChanged: if (pane.host !== null && hoveredLink.length > 0) pane.host.showStatusMessage(hoveredLink, 4000)
                    HoverHandler { cursorShape: body.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor }
                }
            }
            Tk.Notice {
                objectName: "readingTruncated"
                visible: bodyView.truncated
                variant: "info"
                title: qsTr("Only the beginning is shown")
                text: qsTr("This message is very long; save it (File ▸ Save Message As) to read all of it.")
            }
            Tk.Notice {
                visible: pane.message.bodyTruncated === true
                variant: "info"
                title: qsTr("Only the beginning was downloaded")
                text: qsTr("This message is larger than the download limit; open it in webmail for the rest.")
            }
            Tk.Flex {
                objectName: "readingAttachmentsHeader"
                visible: pane.attachments.length > 0
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                width: column.inner
                Tk.Overline { title: qsTr("%n attachment(s)", "", pane.attachments.length); Tk.Flex.grow: 1 }
                Tk.Button {
                    objectName: "readingSaveAll"
                    small: true
                    variant: "ghost"
                    iconName: "download"
                    text: qsTr("Save all")
                    tooltip: qsTr("Save every attachment into a folder")
                    onClicked: pane.host.actions.trigger("mailSaveAttachmentsAction")
                }
            }
            Tk.Flex {
                objectName: "readingAttachments"
                visible: pane.attachments.length > 0
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                width: column.inner
                Repeater {
                    model: pane.attachments
                    delegate: Tk.Flex {
                        id: chipRow
                        required property var modelData
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: 0
                        Tk.Chip {
                            objectName: "readingAttachment_" + String(chipRow.modelData.id || "")
                            iconName: chipRow.modelData.state === "local" ? "paperclip" : "download"
                            text: String(chipRow.modelData.name || qsTr("attachment")) + " · " + pane.sizeText(chipRow.modelData.size)
                            tooltip: chipRow.modelData.state === "local" ? qsTr("Open %1").arg(chipRow.modelData.name) : qsTr("Download %1").arg(chipRow.modelData.name)
                            onClicked: chipRow.modelData.state === "local" ? pane.service.openAttachment(pane.message.messageId, chipRow.modelData.id)
                                                                          : pane.service.fetchAttachment(pane.message.messageId, chipRow.modelData.id)
                        }
                        Tk.IconButton {
                            objectName: "readingAttachmentSave_" + String(chipRow.modelData.id || "")
                            small: true
                            iconName: "save"
                            tooltip: qsTr("Save %1 as…").arg(chipRow.modelData.name)
                            onClicked: pane.saveAttachment(chipRow.modelData)
                        }
                    }
                }
            }
        }
    }

    Dialogs.FileDialog {
        id: attachmentSaveDialog
        property string attachmentId: ""
        title: qsTr("Save Attachment")
        fileMode: Dialogs.FileDialog.SaveFile
        onAccepted: pane.service.saveAttachmentAs(pane.message.messageId, attachmentId, selectedFile)
    }
}
