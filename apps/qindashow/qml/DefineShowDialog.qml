// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Define Custom Show (M91, D-099), as PowerPoint's: the show's name, the
// presentation's slides on the left (check the ones to add, Add >> puts
// them at the end), the show's slides on the right in show order (Up,
// Down, Remove; a slide may appear more than once). OK makes the new show
// or changes the one being edited — one undo step (ShowSlideShow).
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    readonly property ShowSlideShow slideShow: showHost.slideShow
    property int index: -1         // the show edited; -1 makes a new one
    property var picked: []        // deck positions checked on the left
    property var chosen: []        // the show's slides (deck positions), in order
    property int current: -1       // the row chosen on the right

    function edit(showIndex) {
        dialog.index = showIndex
        const shows = dialog.slideShow.customShows
        const show = showIndex >= 0 && showIndex < shows.length ? shows[showIndex] : null
        nameField.text = show !== null ? show.name : qsTr("Custom Show %1").arg(shows.length + 1)
        dialog.chosen = show !== null ? show.slides.slice() : []
        dialog.picked = []
        dialog.current = -1
        dialog.open()
    }
    function slideLabel(position) {
        const slides = dialog.slideShow.slides
        return position >= 0 && position < slides.length ? (position + 1) + ". " + slides[position].title : ""
    }
    function move(delta) {
        const to = dialog.current + delta
        if (dialog.current < 0 || to < 0 || to >= dialog.chosen.length) return
        const next = dialog.chosen.slice()
        const item = next.splice(dialog.current, 1)[0]
        next.splice(to, 0, item)
        dialog.chosen = next
        dialog.current = to
    }

    objectName: "defineShowDialog"
    title: dialog.index >= 0 ? qsTr("Edit Custom Show") : qsTr("Define Custom Show")
    primaryText: qsTr("OK")
    primaryEnabled: nameField.text.trim().length > 0 && dialog.chosen.length > 0
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    onAccepted: {
        if (dialog.index >= 0) dialog.slideShow.editCustomShow(dialog.index, nameField.text, dialog.chosen)
        else dialog.slideShow.addCustomShow(nameField.text, dialog.chosen)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Slide show name")
            Tk.TextField { id: nameField; objectName: "defineShowName"; width: Tk.Theme.size.dialogWidth * 0.57 }
        }
        Tk.Flex {
            direction: dialog.width < dialog.dialogWidth ? Tk.Flex.Column : Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { text: qsTr("Slides in presentation") }
                Tk.Scroll {
                    implicitWidth: Tk.Theme.size.dialogWidth * 0.45
                    implicitHeight: Tk.Theme.size.control * 8
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        width: Tk.Theme.size.dialogWidth * 0.43
                        Repeater {
                            model: dialog.slideShow.slides
                            Tk.CheckBox {
                                required property var modelData
                                required property int index
                                objectName: "defineSlide_" + index
                                text: (index + 1) + ". " + modelData.title + (modelData.hidden ? qsTr(" (hidden)") : "")
                                checked: dialog.picked.indexOf(index) >= 0
                                onToggled: {
                                    const next = dialog.picked.filter(function(p) { return p !== index })
                                    if (checked) next.push(index)
                                    next.sort(function(a, b) { return a - b })
                                    dialog.picked = next
                                }
                            }
                        }
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                justify: Tk.Flex.Center
                Tk.Button {
                    objectName: "defineAdd"
                    text: qsTr("Add >>")
                    enabled: dialog.picked.length > 0
                    onClicked: { dialog.chosen = dialog.chosen.concat(dialog.picked); dialog.picked = [] }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { text: qsTr("Slides in custom show") }
                Tk.Scroll {
                    implicitWidth: Tk.Theme.size.dialogWidth * 0.45
                    implicitHeight: Tk.Theme.size.control * 8
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        width: Tk.Theme.size.dialogWidth * 0.43
                        Repeater {
                            model: dialog.chosen
                            Tk.ListRow {
                                required property var modelData
                                required property int index
                                objectName: "defineChosen_" + index
                                width: Tk.Theme.size.dialogWidth * 0.43
                                small: true
                                text: dialog.slideLabel(modelData)
                                selected: dialog.current === index
                                onClicked: dialog.current = index
                            }
                        }
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                justify: Tk.Flex.Center
                Tk.IconButton { objectName: "defineUp"; iconName: "arrow-up"; tooltip: qsTr("Move up"); enabled: dialog.current > 0; onClicked: dialog.move(-1) }
                Tk.IconButton {
                    objectName: "defineRemove"
                    iconName: "x"
                    tooltip: qsTr("Remove from the show")
                    enabled: dialog.current >= 0
                    onClicked: {
                        const next = dialog.chosen.slice()
                        next.splice(dialog.current, 1)
                        dialog.chosen = next
                        dialog.current = Math.min(dialog.current, next.length - 1)
                    }
                }
                Tk.IconButton {
                    objectName: "defineDown"
                    iconName: "arrow-down"
                    tooltip: qsTr("Move down")
                    enabled: dialog.current >= 0 && dialog.current < dialog.chosen.length - 1
                    onClicked: dialog.move(1)
                }
            }
        }
    }
}
