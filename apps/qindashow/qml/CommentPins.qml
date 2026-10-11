// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Comment anchors are editor chrome in slide coordinates. They follow a
// shape until moved in the Comments pane, and never enter print or the show.
Item {
    id: pins
    required property ShowHost showHost
    objectName: "commentPins"
    Repeater {
        model: pins.showHost.comments.comments
        Tk.IconButton {
            required property var modelData
            objectName: "commentPin_" + modelData.id
            x: Math.max(0, Math.min(pins.width - width, modelData.x))
            y: Math.max(0, Math.min(pins.height - height, modelData.y))
            iconName: modelData.done ? "check" : "message-square"
            ghost: false
            checked: pins.showHost.comments.activeId === modelData.id
            tooltip: qsTr("%1: %2").arg(modelData.author).arg(modelData.text)
            onClicked: {
                pins.showHost.comments.select(modelData.id)
                if (!pins.showHost.actions.entry("showCommentsPaneAction").checked)
                    pins.showHost.actions.trigger("showCommentsPaneAction")
            }
        }
    }
}
