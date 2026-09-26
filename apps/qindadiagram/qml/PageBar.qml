// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The strip under the page (D-050): page tabs (Visio's sheet tabs) with
// add and a page menu, then the page size and the zoom. Every action here
// is also a command (Insert ▸ New Page, View ▸ Fit Page…).
Tk.Box {
    id: bar
    required property DiagramHost host
    property real zoom: 1
    readonly property DiagramService diagram: host.diagram

    signal zoomRequested(string how)
    signal zoomChosen(real value)

    implicitHeight: Tk.Theme.size.toolbar
    color: Tk.Theme.color.panel
    borderTop: Tk.Theme.size.border
    borderColor: Tk.Theme.color.divider
    paddingLeft: Tk.Theme.space.sm
    paddingRight: Tk.Theme.space.sm

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs

        Tk.IconButton {
            objectName: "diagramAddPage"
            iconName: "plus"
            tooltip: qsTr("New page")
            onClicked: bar.host.actions.trigger("diagramNewPageAction")
        }
        Tk.TabStrip {
            objectName: "diagramPageTabs"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            style: "pill"
            small: true
            model: bar.diagram.pages.map(function(p) { return { text: p.name, tooltip: qsTr("Page %1").arg(p.index + 1) } })
            currentIndex: bar.diagram.currentPageIndex
            onTabActivated: function(index) { bar.diagram.currentPageId = bar.diagram.pages[index].id }
        }
        Tk.IconButton {
            id: pageMenuButton
            objectName: "diagramPageMenuButton"
            iconName: "ellipsis"
            tooltip: qsTr("Page commands")
            onClicked: pageMenu.popup(pageMenuButton, 0, -pageMenu.implicitHeight)
            Tk.Menu {
                id: pageMenu
                Tk.MenuItem { text: qsTr("Rename page…"); iconName: "pencil"; onTriggered: bar.host.actions.trigger("diagramRenamePageAction") }
                Tk.MenuItem { text: qsTr("Duplicate page"); iconName: "copy"; onTriggered: bar.host.actions.trigger("diagramDuplicatePageAction") }
                Tk.MenuItem { text: qsTr("Move left"); iconName: "chevron-left"; enabled: bar.diagram.currentPageIndex > 0
                              onTriggered: bar.diagram.movePage("", -1) }
                Tk.MenuItem { text: qsTr("Move right"); iconName: "chevron-right"
                              enabled: bar.diagram.currentPageIndex < bar.diagram.pages.length - 1
                              onTriggered: bar.diagram.movePage("", 1) }
                Tk.MenuSeparator {}
                Tk.MenuItem { text: qsTr("Delete page"); iconName: "trash-2"; danger: true; enabled: bar.diagram.pages.length > 1
                              onTriggered: bar.host.actions.trigger("diagramDeletePageAction") }
            }
        }
        Tk.Caption {
            objectName: "diagramPageSize"
            text: bar.diagram.page.sizeText || ""
            Tk.Flex.shrink: 0
            visible: bar.width > Tk.Theme.size.panelMinWidth * 3
        }
        Tk.IconButton {
            objectName: "diagramFitPage"
            iconName: "maximize"
            tooltip: qsTr("Fit the page in the window")
            onClicked: bar.zoomRequested("page")
        }
        Tk.ZoomControl {
            objectName: "diagramZoom"
            value: bar.zoom
            from: 0.1
            to: 8
            onValueModified: function(value) { bar.zoomChosen(value) }
            onResetRequested: bar.zoomChosen(1)
        }
    }
}
