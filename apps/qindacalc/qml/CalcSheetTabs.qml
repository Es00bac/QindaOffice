// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Calc

// The sheet tabs under the grid: one Tk.TabStrip over the host's sheet
// names inside a Flickable, an add button, double-click renames,
// right-click opens the Sheet menu for THAT sheet (the host activates it
// first). AGENT-NOTE: the Flickable is load-bearing — a workbook's tabs
// outgrow narrow windows (eleven analysis sheets need ~700px), and the
// strip alone has no overflow behavior; the nudge buttons appear only
// while the model overflows.
Tk.Box {
    id: bar

    required property CalcHost host
    property var sheetMenuNode: null

    objectName: "sheetTabs"
    color: Tk.Theme.color.surface
    borderTop: Tk.Theme.size.border
    borderColor: Tk.Theme.color.divider
    implicitHeight: row.implicitHeight

    // The tab the host just switched to must be reachable: scroll it into
    // the viewport (a keyboard switch or a right-click on an off-screen
    // tab would otherwise leave the active tab invisible).
    function revealActiveTab() {
        flicker.ensureVisible(strip.currentIndex)
    }

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
        Tk.IconButton {
            objectName: "sheetNudgeLeft"
            iconName: "chevron-left"
            small: true
            visible: flicker.overflows
            tooltip: qsTr("Scroll sheet tabs left")
            onClicked: flicker.nudge(-1)
        }
        Flickable {
            id: flicker
            objectName: "sheetTabFlick"
            Tk.Flex.grow: 1
            implicitHeight: strip.implicitHeight
            clip: true
            interactive: overflows
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: strip.implicitWidth
            contentHeight: height
            readonly property bool overflows: contentWidth > width + 1

            function nudge(direction) {
                const step = width * 0.75 * direction
                contentX = Math.max(0, Math.min(contentWidth - width, contentX + step))
            }
            function ensureVisible(index) {
                const tab = strip.tabItem(index)
                if (tab === null || !overflows) {
                    return
                }
                if (tab.x < contentX) {
                    contentX = tab.x
                } else if (tab.x + tab.width > contentX + width) {
                    contentX = tab.x + tab.width - width
                }
            }

            Tk.TabStrip {
                id: strip
                objectName: "sheetStrip"
                width: Math.max(implicitWidth, flicker.width)
                small: true
                style: "pill"
                model: bar.host.sheetNames
                currentIndex: bar.host.activeSheet
                onTabActivated: function(index) { bar.host.activeSheet = index }
                onCurrentIndexChanged: flicker.ensureVisible(currentIndex)
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
        Tk.IconButton {
            objectName: "sheetNudgeRight"
            iconName: "chevron-right"
            small: true
            visible: flicker.overflows
            tooltip: qsTr("Scroll sheet tabs right")
            onClicked: flicker.nudge(1)
        }
    }
    MenuFromNode {
        id: sheetMenu
        objectName: "calcSheetTabMenu"
        node: bar.sheetMenuNode
        actions: bar.host.actions
    }
}
