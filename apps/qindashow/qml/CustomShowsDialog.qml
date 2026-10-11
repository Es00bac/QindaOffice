// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Slide Show ▸ Custom Slide Show ▸ Custom Shows… (M91, D-099), as
// PowerPoint's dialog: the deck's custom shows; New… and Edit… open Define
// Custom Show (DefineShowDialog.qml), Remove and Copy change the list —
// each one undo step (ShowSlideShow) — and Show plays the chosen one.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property ShowSlideShow slideShow: showHost.slideShow
    property int current: -1
    // Open again once Define Custom Show closes (it was opened from here).
    property bool reopen: false
    signal defineRequested(int index)

    function define(index) {
        dialog.reopen = true
        dialog.close()
        dialog.defineRequested(index)
    }

    objectName: "customShowsDialog"
    title: qsTr("Custom Shows")
    primaryText: qsTr("Close")
    secondaryText: ""
    dialogWidth: Tk.Theme.size.dialogWidth
    onAboutToShow: {
        dialog.reopen = false
        if (dialog.current >= dialog.slideShow.customShows.length) dialog.current = dialog.slideShow.customShows.length - 1
        if (dialog.current < 0 && dialog.slideShow.customShows.length > 0) dialog.current = 0
    }

    Tk.Flex {
        direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.md
        Tk.Scroll {
            implicitWidth: Tk.Theme.size.dialogWidth * 0.46
            implicitHeight: Tk.Theme.size.control * 8
            Tk.Flex {
                direction: Tk.Flex.Column
                width: Tk.Theme.size.dialogWidth * 0.45
                Tk.Caption {
                    visible: dialog.slideShow.customShows.length === 0
                    width: Tk.Theme.size.dialogWidth * 0.45
                    wrapMode: Text.Wrap
                    text: qsTr("No custom shows yet. New… makes one from the slides you pick.")
                }
                Repeater {
                    model: dialog.slideShow.customShows
                    Tk.ListRow {
                        required property var modelData
                        required property int index
                        objectName: "customShow_" + index
                        width: Tk.Theme.size.dialogWidth * 0.45
                        iconName: "list-ordered"
                        text: modelData.name
                        secondaryText: qsTr("%n slide(s)", "", modelData.slides.length)
                        selected: dialog.current === index
                        onClicked: dialog.current = index
                        onDoubleClicked: dialog.define(index)
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Button { objectName: "customShowNew"; text: qsTr("New…"); onClicked: dialog.define(-1) }
            Tk.Button {
                objectName: "customShowEdit"
                text: qsTr("Edit…")
                enabled: dialog.current >= 0
                onClicked: dialog.define(dialog.current)
            }
            Tk.Button {
                objectName: "customShowRemove"
                text: qsTr("Remove")
                enabled: dialog.current >= 0
                onClicked: {
                    dialog.slideShow.removeCustomShow(dialog.current)
                    dialog.current = Math.min(dialog.current, dialog.slideShow.customShows.length - 1)
                }
            }
            Tk.Button {
                objectName: "customShowCopy"
                text: qsTr("Copy")
                enabled: dialog.current >= 0
                onClicked: dialog.current = dialog.slideShow.copyCustomShow(dialog.current)
            }
            Tk.Button {
                objectName: "customShowPlay"
                text: qsTr("Show")
                variant: "accent"
                enabled: dialog.current >= 0
                onClicked: {
                    const index = dialog.current
                    dialog.close()
                    dialog.slideShow.playCustomShow(index)
                }
            }
        }
    }
}
