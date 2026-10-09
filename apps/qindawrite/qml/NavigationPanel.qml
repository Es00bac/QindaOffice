// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// View ▸ Navigation Pane (M73, D-082): the document's headings by outline
// level (NavigationModel, write_navigation.h). A click goes to a heading;
// its arrow collapses or expands the headings under it; Move Up / Move
// Down move the marked heading with its whole section. The caret's
// heading is marked as Word marks it.
Tk.Flex {
    id: panel

    required property var navigation
    readonly property var rows: navigation.headings.filter(function(row) { return row.visible })

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Tk.EmptyState {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.navigation.headings.length === 0
        iconName: "navigation"
        title: qsTr("No headings")
        text: qsTr("Give titles a heading style (Heading 1, Heading 2, …) or an outline level, and they appear here.")
    }
    Tk.Scroll {
        id: scroll
        objectName: "navigationScroll"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        overflowX: Tk.Scroll.Hidden
        visible: panel.navigation.headings.length > 0
        Tk.Flex {
            direction: Tk.Flex.Column
            width: scroll.width - Tk.Theme.space.sm
            Repeater {
                model: panel.rows
                Tk.TreeRow {
                    required property var modelData
                    objectName: "heading_" + modelData.index
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    depth: modelData.level - 1
                    expandable: modelData.expandable
                    expanded: modelData.expanded
                    text: (modelData.number.length > 0 ? modelData.number + " " : "") + modelData.text
                    selected: panel.navigation.current === modelData.index
                    tooltip: modelData.text
                    onClicked: panel.navigation.go(modelData.index)
                    onExpansionToggled: panel.navigation.toggle(modelData.index)
                }
            }
        }
    }
    Tk.Flex {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        gap: Tk.Theme.space.xs
        visible: panel.navigation.headings.length > 0
        Tk.Button {
            objectName: "navigationUp"; small: true; iconName: "arrow-up"; text: qsTr("Move Up")
            enabled: panel.navigation.current >= 0
            onClicked: panel.navigation.move(panel.navigation.current, true)
        }
        Tk.Button {
            objectName: "navigationDown"; small: true; iconName: "arrow-down"; text: qsTr("Move Down")
            enabled: panel.navigation.current >= 0
            onClicked: panel.navigation.move(panel.navigation.current, false)
        }
    }
}
