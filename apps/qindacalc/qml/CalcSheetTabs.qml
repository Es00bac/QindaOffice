// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Calc

// The sheet tabs under the grid: one Tk.TabStrip over the host's sheet
// names, an add button, double-click renames, right-click opens the
// Sheet menu for THAT sheet (the host activates it first).
Tk.Box {
    id: bar

    required property CalcHost host
    property var sheetMenuNode: null

    objectName: "sheetTabs"
    color: Tk.Theme.color.surface
    borderTop: Tk.Theme.size.border
    borderColor: Tk.Theme.color.divider
    implicitHeight: row.implicitHeight

    Tk.Flex {
        id: row
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        paddingLeft: Tk.Theme.space.xs
        Tk.IconButton {
            objectName: "sheetAdd"
            iconName: "plus"
            small: true
            tooltip: qsTr("Insert sheet")
            onClicked: bar.host.actions.trigger("insertSheetAction")
        }
        Tk.TabStrip {
            id: strip
            objectName: "sheetStrip"
            Tk.Flex.grow: 1
            small: true
            style: "pill"
            model: bar.host.sheetNames
            currentIndex: bar.host.activeSheet
            onTabActivated: function(index) { bar.host.activeSheet = index }
            // Double-click → rename; right-click → the Sheet menu.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: function(eventPoint) {
                    const index = strip.indexAt(eventPoint.position)
                    if (index >= 0) {
                        bar.host.activeSheet = index
                        bar.host.actions.trigger("calcSheetRenameAction")
                    }
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: function(eventPoint) {
                    const index = strip.indexAt(eventPoint.position)
                    if (index >= 0) {
                        bar.host.activateSheetForMenu(index)
                        sheetMenu.popup()
                    }
                }
            }
            function indexAt(point) {
                for (let i = 0; i < strip.count; ++i) {
                    const tab = strip.tabItem(i)
                    if (tab !== null) {
                        const p = strip.mapToItem(tab, point.x, point.y)
                        if (p.x >= 0 && p.x <= tab.width && p.y >= 0 && p.y <= tab.height) {
                            return i
                        }
                    }
                }
                return -1
            }
        }
    }
    MenuFromNode {
        id: sheetMenu
        objectName: "calcSheetTabMenu"
        node: bar.sheetMenuNode
        actions: bar.host.actions
    }
}
