// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The inspector (D-050): with a selection, what it is and three sections —
// Style (geometry, fill, line, effects; routing for connectors), Text and
// Data (Visio's Shape Data); with nothing selected, the page and the
// drawing's theme. Every field edits through DiagramService, one undo step
// per change.
Tk.Box {
    id: panel
    objectName: "diagramInspector"

    required property DiagramService diagram
    required property var spatialTransform
    property string section: "style"
    readonly property bool hasSelection: diagram.selection.length > 0
    readonly property var sections: ["style", "text", "data"]

    function show(name) {
        if (name === "page") return
        if (sections.indexOf(name) >= 0) section = name
    }

    color: Tk.Theme.color.panel

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: 0

        Tk.Box {
            objectName: "diagramInspectorHeader"
            paddingLeft: Tk.Theme.space.md
            paddingRight: Tk.Theme.space.md
            paddingTop: Tk.Theme.space.md
            paddingBottom: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.sm
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    Tk.Icon {
                        name: !panel.hasSelection ? "file"
                            : panel.diagram.selectionKind === "connector" ? "spline" : "shapes"
                        size: Tk.Theme.size.iconLg
                        color: Tk.Theme.color.accentText
                    }
                    Tk.Label {
                        objectName: "diagramInspectorTitle"
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        font.pixelSize: Tk.Theme.font.medium
                        font.weight: Font.DemiBold
                        text: !panel.hasSelection ? qsTr("Page")
                            : panel.diagram.selection.length > 1 ? qsTr("%1 items").arg(panel.diagram.selection.length)
                            : (panel.diagram.selected.typeTitle || "")
                    }
                }
                Tk.TabStrip {
                    objectName: "diagramInspectorTabs"
                    visible: panel.hasSelection
                    stretch: true
                    small: true
                    model: [qsTr("Style"), qsTr("Text"), qsTr("Data")]
                    currentIndex: Math.max(0, panel.sections.indexOf(panel.section))
                    onTabActivated: function(index) { panel.section = panel.sections[index] }
                }
            }
        }
        Tk.Divider {}
        Tk.Scroll {
            id: scroll
            objectName: "diagramInspectorScroll"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            overflowY: Tk.Scroll.Auto
            Tk.Box {
                width: scroll.viewportWidth
                padding: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Column
                    align: Tk.Flex.Stretch
                    gap: 0
                    InspectorShape {
                        visible: panel.hasSelection && panel.section === "style"
                        diagram: panel.diagram
                        spatialTransform: panel.spatialTransform
                    }
                    InspectorText {
                        visible: panel.hasSelection && panel.section === "text"
                        diagram: panel.diagram
                    }
                    InspectorData {
                        visible: panel.hasSelection && panel.section === "data"
                        diagram: panel.diagram
                    }
                    InspectorPage {
                        visible: !panel.hasSelection
                        diagram: panel.diagram
                    }
                }
            }
        }
    }
}
