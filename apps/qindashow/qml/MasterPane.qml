// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Show

// The Slide Master view's left pane (M75, D-084), in place of the slide
// sorter while View ▸ Slide Master is on: each master, then its layouts
// indented under it, painted through the same renderer the slides use
// (SlideThumbItem.templateIndex → ShowMasterView::renderItem). A click
// puts that master or layout on the canvas; right-click shows the Master
// Tools menu; the caption says how many slides use a layout.
Tk.Scroll {
    id: pane
    required property ShowHost showHost
    readonly property ShowMasterView view: showHost.masterView

    objectName: "masterPane"

    function findMenu(nodes, id) {
        for (let i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === id) return nodes[i]
            const found = nodes[i].children ? findMenu(nodes[i].children, id) : null
            if (found !== null) return found
        }
        return null
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm
        Repeater {
            model: pane.view.items
            Tk.Card {
                id: card
                required property var modelData
                required property int index
                readonly property bool master: modelData.master
                objectName: "masterThumb_" + index
                interactive: true
                selected: index === pane.view.currentIndex
                padding: Tk.Theme.space.xs
                Tk.Flex.alignSelf: master ? Tk.Flex.Start : Tk.Flex.End
                tooltip: card.master ? qsTr("Slide master: %1").arg(modelData.name)
                       : modelData.uses > 0 ? qsTr("%1 layout: used by %2 slide(s)").arg(modelData.name).arg(modelData.uses)
                       : qsTr("%1 layout: used by no slides").arg(modelData.name)
                onClicked: pane.view.currentIndex = card.index
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    SlideThumbItem {
                        objectName: "masterThumbImage_" + card.index
                        host: pane.showHost
                        templateIndex: card.index
                        revision: pane.view.revision
                        implicitWidth: card.master ? 160 : 132
                        implicitHeight: implicitWidth * 9 / 16
                    }
                    Tk.Flex {
                        gap: Tk.Theme.space.xs
                        align: Tk.Flex.Center
                        Tk.Icon {
                            visible: card.modelData.preserve
                            name: "pin"
                            size: Tk.Theme.size.iconSm
                            color: Tk.Theme.color.textMuted
                        }
                        Tk.Caption {
                            text: card.modelData.name
                            width: card.master ? 160 : 132
                            elide: Text.ElideRight
                        }
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        pane.view.currentIndex = card.index
                        toolsMenu.popup()
                    }
                }
            }
        }
    }
    MenuFromNode {
        id: toolsMenu
        objectName: "masterToolsContextMenu"
        actions: pane.showHost.actions
        node: pane.findMenu(pane.showHost.menuTree.menus, "showMasterToolsMenu")
    }
}
