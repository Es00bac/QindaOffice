// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// The reading pane's info bar for a message's tags (M81, D-090), as
// Outlook shows it above the body: the follow-up flag ("Follow up. Start
// by … Due by …", or "Completed on …") with Mark Complete / Clear Flag,
// the importance it was sent with, and its categories as coloured chips
// (a click opens Categorize).
Tk.Flex {
    id: bar

    property MailService service: null
    property MailHost host: null
    property var message: ({})
    readonly property string messageId: String(message.messageId || "")
    readonly property var followUp: service !== null && messageId.length > 0 && service.organizer.revision >= 0
                                    && message.followUpStatus !== undefined ? service.organizer.followUpOf(messageId) : ({})
    readonly property var chips: message.categoryChips || []
    readonly property string importance: String(message.importance || "normal")

    objectName: "readingTags"
    visible: String(followUp.status || "").length > 0 || chips.length > 0 || importance !== "normal"
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Tk.Flex {
        objectName: "readingFollowUp"
        visible: String(bar.followUp.status || "").length > 0
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Icon {
            name: bar.followUp.status === "complete" ? "check-check" : "flag"
            size: Tk.Theme.size.iconSm
            color: bar.followUp.status === "complete" ? Tk.Theme.color.success : Tk.Theme.color.danger
        }
        Tk.Caption { objectName: "readingFollowUpText"; text: String(bar.followUp.text || ""); wrapMode: Text.Wrap; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
        Tk.Button {
            objectName: "readingFlagComplete"
            visible: bar.followUp.status === "flagged"
            small: true
            variant: "ghost"
            iconName: "check-check"
            text: qsTr("Mark Complete")
            tooltip: qsTr("The follow-up is done")
            onClicked: bar.host.actions.trigger("mailFlagCompleteAction")
        }
        Tk.Button {
            objectName: "readingFlagClear"
            small: true
            variant: "ghost"
            iconName: "circle-x"
            text: qsTr("Clear Flag")
            tooltip: qsTr("Take the flag off")
            onClicked: bar.host.actions.trigger("mailFlagClearAction")
        }
    }
    Tk.Flex {
        objectName: "readingImportance"
        visible: bar.importance !== "normal"
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Icon { name: bar.importance === "high" ? "circle-alert" : "arrow-down"; size: Tk.Theme.size.iconSm
                  color: bar.importance === "high" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted }
        Tk.Caption { text: bar.importance === "high" ? qsTr("This message was sent with High importance.")
                                                     : qsTr("This message was sent with Low importance.") }
    }
    Tk.Flex {
        objectName: "readingCategories"
        visible: bar.chips.length > 0
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.xs
        Repeater {
            model: bar.chips
            delegate: Tk.Flex {
                id: chipRow
                required property var modelData
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                // The category's own colour (user data, AGENTS.md rule 3).
                Rectangle {
                    implicitWidth: Tk.Theme.space.md
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: String(chipRow.modelData.color || "").length > 0 ? String(chipRow.modelData.color) : Tk.Theme.color.textMuted
                }
                Tk.Chip {
                    objectName: "readingCategory_" + String(chipRow.modelData.name)
                    small: true
                    text: String(chipRow.modelData.name)
                    tooltip: qsTr("Category %1 — click to change").arg(String(chipRow.modelData.name))
                    onClicked: bar.host.actions.trigger("mailCategorizeAction")
                }
            }
        }
    }
}
