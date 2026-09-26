// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The inspector with nothing selected (D-050): the page — its name, paper,
// orientation and size, background, grid and line jumps — and the
// drawing's theme. The same settings are on the Design tab.
Tk.Flex {
    id: section
    objectName: "diagramInspectorPage"

    required property DiagramService diagram
    readonly property var page: diagram.page
    readonly property var papers: diagram.paperSizes

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.sm

    Tk.Overline { title: qsTr("PAGE") }
    Tk.PropertyRow {
        label: qsTr("Name")
        Tk.TextField {
            objectName: "diagramPageName"
            small: true
            text: section.page.name || ""
            tooltip: qsTr("Name of this page (its tab)")
            onEditingFinished: if (text.trim().length > 0 && text !== section.page.name)
                section.diagram.renamePage("", text)
        }
    }
    Tk.PropertyRow {
        label: qsTr("Paper")
        Tk.ComboBox {
            objectName: "diagramPaper"
            model: section.papers.map(function(p) { return p.label }).concat([qsTr("Custom")])
            currentIndex: {
                for (let i = 0; i < section.papers.length; ++i) if (section.papers[i].id === section.page.paper) return i
                return section.papers.length
            }
            tooltip: qsTr("Paper size of this page")
            onActivated: function(index) { if (index < section.papers.length) section.diagram.setPaper(section.papers[index].id) }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Orientation")
        Tk.Segmented {
            objectName: "diagramOrientation"
            stretch: true
            model: [{ text: qsTr("Portrait"), iconName: "file" }, { text: qsTr("Landscape"), iconName: "monitor" }]
            currentIndex: section.page.landscape ? 1 : 0
            tooltip: qsTr("Page orientation")
            onActivated: function(index) { section.diagram.setLandscape(index === 1) }
        }
    }
    Tk.Caption {
        objectName: "diagramPageSizeText"
        text: qsTr("%1 · %2 shapes, %3 connectors").arg(section.page.sizeText || "")
                  .arg(section.page.shapeCount || 0).arg(section.page.connectorCount || 0)
        wrapMode: Text.WordWrap
    }
    Tk.PropertyRow {
        label: qsTr("Background")
        Tk.ColorButton {
            objectName: "diagramPageBackground"
            color: section.page.background || "#ffffff"
            colors: ["#ffffff", "#f8fafc", "#fdfaf3", "#f2f7fb", "#f4f8f4", "#fbf5f7"]
            tooltip: qsTr("Page colour (prints)")
            onColorSelected: function(color) { section.diagram.setPageBackground(String(color)) }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Grid")
        Tk.Switch {
            objectName: "diagramGridSwitch"
            checked: !!section.page.grid
            tooltip: qsTr("Background grid on screen (never printed)")
            onToggled: section.diagram.setGridVisible(checked)
        }
    }
    Tk.PropertyRow {
        label: qsTr("Line jumps")
        Tk.Switch {
            objectName: "diagramJumpsSwitch"
            checked: !!section.page.lineJumps
            tooltip: qsTr("Connectors hop where they cross")
            onToggled: section.diagram.setLineJumps(checked)
        }
    }
    Tk.Button {
        objectName: "diagramFitPageButton"
        text: qsTr("Fit page to drawing")
        iconName: "shrink"
        variant: "outline"
        tooltip: qsTr("Shrink or grow the page around what is on it")
        onClicked: section.diagram.fitPageToContents()
    }

    Tk.Overline { title: qsTr("THEME") }
    Repeater {
        model: section.diagram.themes
        delegate: Tk.ListRow {
            id: themeRow
            required property var modelData
            objectName: "diagramTheme_" + modelData.id
            text: modelData.name
            secondaryText: modelData.font
            selected: modelData.id === section.diagram.themeId
            tooltip: qsTr("Restyle every shape with the %1 theme").arg(modelData.name)
            onClicked: section.diagram.applyTheme(modelData.id)
            trailing: Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Repeater {
                    model: themeRow.modelData.swatches
                    delegate: Rectangle {
                        required property var modelData
                        implicitWidth: Tk.Theme.size.iconSm
                        implicitHeight: Tk.Theme.size.iconSm
                        radius: Tk.Theme.radius.full
                        color: modelData
                    }
                }
            }
        }
    }
}
