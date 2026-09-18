// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Show

// The slide sorter: one thumbnail per slide (painted through the shared
// renderer), the current one outlined; click selects, drag reorders
// (moveSlide(from, to) → one SlideListCommand), right-click shows the
// Slide menu.
Tk.Scroll {
    id: sorter
    required property ShowHost showHost
    objectName: "slideSorter"
    Tk.Flex {
        id: column
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm
        Repeater {
            id: thumbs
            model: sorter.showHost.slideCount
            Tk.Card {
                id: card
                required property int index
                objectName: "slideThumb_" + index
                interactive: true
                selected: index === sorter.showHost.currentSlide
                padding: Tk.Theme.space.xs
                implicitWidth: 176
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    SlideThumbItem {
                        objectName: "slideThumbImage_" + card.index
                        host: sorter.showHost
                        slideIndex: card.index
                        revision: sorter.showHost.deckRevision
                        implicitWidth: 160
                        implicitHeight: 90
                    }
                    Tk.Caption { text: (card.index + 1) + ". " + sorter.showHost.slideTitle(card.index); width: 160 }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property int dragTarget: -1
                    onPressed: function(mouse) {
                        sorter.showHost.currentSlide = card.index
                        dragTarget = card.index
                        if (mouse.button === Qt.RightButton) slideMenu.popup()
                    }
                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton)) return
                        // The thumbnail the pointer is over, from the column's
                        // slot height (cards are uniform).
                        const y = mapToItem(column, mouse.x, mouse.y).y
                        const slot = card.height + column.gap
                        dragTarget = Math.max(0, Math.min(thumbs.count - 1, Math.floor((y - column.padding) / slot)))
                    }
                    onReleased: function(mouse) {
                        if (mouse.button === Qt.LeftButton && dragTarget >= 0 && dragTarget !== card.index) {
                            sorter.showHost.moveSlide(card.index, dragTarget)
                        }
                        dragTarget = -1
                    }
                }
            }
        }
    }
    MenuFromNode {
        id: slideMenu
        objectName: "slideSorterMenu"
        actions: sorter.showHost.actions
        node: {
            const menus = sorter.showHost.menuTree.menus
            for (let i = 0; i < menus.length; ++i) if (menus[i].id === "showSlideMenu") return menus[i]
            return null
        }
    }
}
