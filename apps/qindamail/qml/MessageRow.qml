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
            Tk.Icon { visible: row.hasAttachments; name: "paperclip"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            Tk.Icon { visible: row.flagged && !row.hovered; name: "flag"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.danger }
        }
        Tk.Caption {
            text: row.snippet
            elide: Text.ElideRight
            maximumLineCount: 1
            Tk.Flex.shrink: 1
        }
    }
}
