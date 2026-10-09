// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Reuse Slides (M82, D-091): the slides of the presentation chosen in Home ▸
// New Slide ▸ Reuse Slides…, each with its thumbnail and a check box, and
// PowerPoint's "Keep source formatting". Insert puts the checked ones after
// the current slide as ONE undo step (ShowSlideLayouts::reuse): with their
// own master, layouts and theme, or on this deck's layout of the same name.
Tk.Dialog {
    id: dialog
    required property ShowHost showHost
    property var chosen: []
    objectName: "reuseSlidesDialog"
    title: qsTr("Reuse Slides")
    primaryText: qsTr("Insert")
    primaryEnabled: dialog.chosen.length > 0
    onAboutToShow: {
        const all = []
        for (let i = 0; i < dialog.showHost.layouts.reuseSlides.length; ++i) all.push(i)
        dialog.chosen = all
    }
    onAccepted: dialog.showHost.layouts.reuse(dialog.chosen, keepFormatting.checked)
    function toggle(index, on) {
        const next = dialog.chosen.filter(function(i) { return i !== index })
        if (on) next.push(index)
        next.sort(function(a, b) { return a - b })
        dialog.chosen = next
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption { text: dialog.showHost.layouts.reuseFile; width: Tk.Theme.size.dialogWidth }
        Tk.Scroll {
            implicitWidth: Tk.Theme.size.dialogWidth
            implicitHeight: 300
            Tk.Flex {
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.sm
                width: Tk.Theme.size.dialogWidth - Tk.Theme.space.md
                Repeater {
                    model: dialog.showHost.layouts.reuseSlides
                    Tk.Card {
                        id: card
                        required property var modelData
                        required property int index
                        objectName: "reuseSlide_" + (index + 1)
                        padding: Tk.Theme.space.xs
                        selected: dialog.chosen.indexOf(index) >= 0
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            SlideThumbItem {
                                host: dialog.showHost
                                reuseIndex: card.index
                                implicitWidth: 128
                                implicitHeight: 72
                            }
                            Tk.CheckBox {
                                objectName: "reuseSlideCheck_" + (card.index + 1)
                                text: (card.index + 1) + ". " + card.modelData.title
                                checked: dialog.chosen.indexOf(card.index) >= 0
                                onToggled: dialog.toggle(card.index, checked)
                            }
                        }
                    }
                }
            }
        }
        Tk.CheckBox {
            id: keepFormatting
            objectName: "reuseKeepFormatting"
            text: qsTr("Keep source formatting")
        }
    }
}
