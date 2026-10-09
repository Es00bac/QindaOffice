// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// The deck's layouts as PowerPoint's Layout gallery shows them (M82, D-091):
// a thumbnail of each layout of each master — QindaShow's six slide kinds
// for a deck without layouts — with the current slide's outlined. A click
// puts the current slide on that layout; the + button adds a new slide on
// it after the current one. Both run the same ActionEntries as the Slide
// menu and the ribbon (showSlideLayout_<n>, showNewSlideLayout_<n>).
Tk.Flex {
    id: gallery
    required property ShowHost showHost
    objectName: "layoutGallery"
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs
    Repeater {
        model: gallery.showHost.layouts.gallery
        Tk.Card {
            id: card
            required property var modelData
            required property int index
            objectName: "layoutCard_" + (index + 1)
            interactive: true
            selected: modelData.current
            tooltip: qsTr("Put the current slide on “%1”").arg(modelData.name)
            padding: Tk.Theme.space.xs
            onClicked: gallery.showHost.actions.trigger(card.modelData.applyId)
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                SlideThumbItem {
                    objectName: "layoutThumb_" + (card.index + 1)
                    host: gallery.showHost
                    galleryIndex: card.index
                    revision: gallery.showHost.deckRevision
                    implicitWidth: 112
                    implicitHeight: 63
                }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Label { text: card.modelData.name; width: 96; elide: Text.ElideRight }
                    Tk.Caption { text: card.modelData.master; visible: text.length > 0; width: 96 }
                    Tk.IconButton {
                        objectName: "layoutNewSlide_" + (card.index + 1)
                        small: true
                        iconName: "file-plus"
                        tooltip: qsTr("New slide on “%1”").arg(card.modelData.name)
                        onClicked: gallery.showHost.actions.trigger(card.modelData.newId)
                    }
                }
            }
        }
    }
}
