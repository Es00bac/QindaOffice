// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// One row of the message list: sender and time, subject, a one-line
// preview; unread rows carry an accent edge and semibold text; hovering
// shows flag / archive / delete. The roles are MailService.messageModel's.
T.ItemDelegate {
    id: row

    required property int index
    required property string messageId
    required property string sender
    required property string subject
    required property string snippet
    required property string dateText
    required property bool unread
    required property bool flagged
    required property bool hasAttachments
    required property int threadCount
    required property string accountColor
    required property string dayGroup
    // M81 (D-090): categories, follow-up and importance of the newest message.
    required property var categoryChips
    required property string followUpStatus
    required property string followUpDue
    required property string importance
    property bool selected: false
    property bool showDay: false
    readonly property real dayHeight: showDay ? Tk.Theme.size.row + Tk.Theme.space.sm : 0
    signal quickAction(string action)
    signal contextRequested(point position)

    objectName: "messageRow_" + messageId
    implicitHeight: Tk.Theme.size.row * 3 + Tk.Theme.space.sm * 2 + dayHeight
    topInset: dayHeight
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.ListItem
    Accessible.name: (unread ? qsTr("Unread. ") : "") + qsTr("From %1: %2").arg(sender).arg(subject)
    Accessible.description: snippet

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) { row.contextRequested(point.position) }
    }

    background: Rectangle {
        color: row.selected ? Tk.Theme.color.selection : row.hovered ? Tk.Theme.color.hover : Tk.Theme.alpha(Tk.Theme.color.hover, Tk.Theme.opacity.hidden)
        Rectangle {
            visible: row.unread
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Tk.Theme.space.sm
            width: Tk.Theme.size.focusRing + 1
            radius: width
            color: Tk.Theme.color.accent
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Tk.Theme.space.md
            height: Tk.Theme.size.border
            color: Tk.Theme.color.divider
        }
    }

    // The day heading sits above the row's own background (topInset).
    Tk.Overline {
        objectName: "messageDay"
        visible: row.showDay
        x: Tk.Theme.space.md
        y: Tk.Theme.space.sm
        title: row.dayGroup
    }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: 0
        paddingTop: row.dayHeight + Tk.Theme.space.sm
        paddingLeft: Tk.Theme.space.lg
        paddingRight: Tk.Theme.space.md
        paddingBottom: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Rectangle {
                visible: row.accountColor.length > 0
                width: Tk.Theme.space.sm; height: width; radius: width / 2
                color: row.accountColor.length > 0 ? row.accountColor : Tk.Theme.color.accent
            }
            Tk.Label {
                objectName: "messageRowSender"
                text: row.sender
                font.weight: row.unread ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Badge { visible: row.threadCount > 1; text: String(row.threadCount); tooltip: qsTr("%1 messages in this conversation").arg(row.threadCount) }
            Tk.Caption {
                visible: !row.hovered
                text: row.dateText
                color: row.unread ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
            }
            Tk.IconButton {
                objectName: "messageRowFlag"
                visible: row.hovered
                small: true
                iconName: "flag"
                checked: row.flagged
                tooltip: row.flagged ? qsTr("Clear the flag") : qsTr("Flag")
                onClicked: row.quickAction("mailToggleFlagAction")
            }
            Tk.IconButton {
                objectName: "messageRowArchive"
                visible: row.hovered
                small: true
                iconName: "archive"
                tooltip: qsTr("Archive")
                onClicked: row.quickAction("mailArchiveAction")
            }
            Tk.IconButton {
                objectName: "messageRowDelete"
                visible: row.hovered
                small: true
                danger: true
                iconName: "trash-2"
                tooltip: qsTr("Delete")
                onClicked: row.quickAction("mailDeleteAction")
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Label {
                text: row.subject.length > 0 ? row.subject : qsTr("(no subject)")
                color: row.unread ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                font.weight: row.unread ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Repeater {
                model: (row.categoryChips || []).slice(0, 3)
                // A category's own colour (user data, AGENTS.md rule 3).
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: Tk.Theme.space.md
                    implicitHeight: implicitWidth
                    radius: Tk.Theme.radius.xs
                    color: String(modelData.color || "").length > 0 ? String(modelData.color) : Tk.Theme.color.textMuted
                    Tk.ToolTip { text: String(parent.modelData.name || ""); visible: chipHover.hovered }
                    HoverHandler { id: chipHover }
                }
            }
            Tk.Icon { objectName: "messageRowImportance"; visible: row.importance !== "normal"; name: row.importance === "high" ? "circle-alert" : "arrow-down"
                      size: Tk.Theme.size.iconSm; color: row.importance === "high" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted }
            Tk.Icon { visible: row.hasAttachments; name: "paperclip"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            Tk.Caption { objectName: "messageRowDue"; visible: row.followUpStatus === "flagged" && row.followUpDue.length > 0 && !row.hovered
                         text: row.followUpDue; color: Tk.Theme.color.danger }
            Tk.Icon { visible: row.flagged && !row.hovered; name: "flag"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.danger }
            Tk.Icon { objectName: "messageRowComplete"; visible: row.followUpStatus === "complete" && !row.hovered; name: "check-check"
                      size: Tk.Theme.size.iconSm; color: Tk.Theme.color.success }
        }
        Tk.Caption {
            text: row.snippet
            elide: Text.ElideRight
            maximumLineCount: 1
            Tk.Flex.shrink: 1
        }
    }
}
