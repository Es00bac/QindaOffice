// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// The Style section of the inspector (D-050): the selection's geometry
// (Tk.SpatialTransformEditor over the canvas's SpatialTransform, so a typed
// value is the same one-step edit as a handle drag), fill and line
// (Tk.FillStyleEditor / Tk.StrokeStyleEditor, mapped onto the diagram's
// solid fill and line model), effects and quick styles; for connectors the
// routing, arrowheads and label position.
Tk.Flex {
    id: section
    objectName: "diagramInspectorStyle"

    required property DiagramService diagram
    required property var spatialTransform
    readonly property var item: diagram.selected
    readonly property bool isShape: item.kind === "shape"
    readonly property bool isConnector: item.kind === "connector"
    readonly property var themeSwatches: {
        const list = diagram.themes
        for (let i = 0; i < list.length; ++i) if (list[i].id === diagram.themeId) return Array.from(list[i].swatches)
        return []
    }
    readonly property var arrowKinds: ["none", "filled", "open", "diamond", "circle"]
    readonly property var arrowLabels: [qsTr("None"), qsTr("Arrow"), qsTr("Open arrow"), qsTr("Diamond"), qsTr("Dot")]

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.md

    Tk.Flex {
        visible: section.isShape
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("POSITION AND SIZE") }
        Tk.SpatialTransformEditor {
            objectName: "diagramGeometry"
            controller: section.spatialTransform
            small: true
            decimals: 0
            tooltip: qsTr("Position, size and rotation of the selection")
        }
    }

    Tk.FillStyleEditor {
        objectName: "diagramFillEditor"
        visible: section.isShape
        capabilities: ({ gradient: false, pattern: false })
        fill: ({ kind: section.item.fill ? "solid" : "none",
                 settings: { solid: { color: section.item.fill || "#ffffff" } } })
        tooltip: qsTr("Fill of the selected shapes")
        onFillChangeRequested: function(fill) {
            if (fill.kind === "none") section.diagram.setStyle({ fill: "none" })
            else if (fill.kind === "solid") section.diagram.setStyle({ fill: String(fill.settings.solid.color) })
        }
    }

    Tk.StrokeStyleEditor {
        objectName: "diagramStrokeEditor"
        visible: section.isShape || section.isConnector
        capabilities: ({ units: ["px"], alignments: ["center"], caps: ["round"], joins: ["round"] })
        style: ({ width: section.item.lineWidth || 0, unit: "px", color: section.item.line || "#475569",
                  alignment: "center", cap: "round", join: "round", miterLimit: 4,
                  dash: section.item.pattern === "dash" ? [4, 3] : section.item.pattern === "dot" ? [1, 2] : [],
                  dashMode: section.item.pattern === "dot" ? "dot" : section.item.pattern === "solid" ? "solid" : "dash" })
        tooltip: qsTr("Line of the selection")
        onStyleChangeRequested: function(style) {
            const changed = style.changed || []
            const patch = {}
            if (changed.indexOf("width") >= 0) patch.lineWidth = style.width
            if (changed.indexOf("color") >= 0) patch.line = String(style.color)
            if (changed.indexOf("dashMode") >= 0 || changed.indexOf("dash") >= 0)
                patch.pattern = style.dashMode === "dot" ? "dot" : style.dashMode === "solid" ? "solid" : "dash"
            if (Object.keys(patch).length > 0) section.diagram.setStyle(patch)
        }
    }

    Tk.Flex {
        visible: section.isShape
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("EFFECTS") }
        Tk.PropertyRow {
            label: qsTr("Shadow")
            Tk.Switch {
                objectName: "diagramShadowSwitch"
                checked: !!section.item.shadow
                tooltip: qsTr("Soft shadow under the shape")
                onToggled: section.diagram.setStyle({ shadow: checked })
            }
        }
        Tk.PropertyRow {
            label: qsTr("Corners")
            Tk.NumberField {
                objectName: "diagramCornerField"
                value: section.item.corner || 0
                from: 0
                to: 200
                stepSize: 2
                suffix: " px"
                onValueModified: function(value) { section.diagram.setStyle({ corner: value, merge: "corner" }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Opacity")
            Tk.Slider {
                objectName: "diagramOpacitySlider"
                from: 0.1
                to: 1
                stepSize: 0.05
                value: section.item.opacity === undefined ? 1 : section.item.opacity
                showValue: true
                decimals: 2
                tooltip: qsTr("Shape opacity")
                onValueModified: function(value) { section.diagram.setStyle({ opacity: value, merge: "opacity" }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Quick style")
            Tk.ColorSwatches {
                objectName: "diagramQuickStyles"
                colors: section.themeSwatches
                columns: 6
                customButton: false
                onSelected: function(color) {
                    for (let i = 0; i < section.themeSwatches.length; ++i)
                        if (Qt.colorEqual(section.themeSwatches[i], color)) { section.diagram.quickStyle(i); return }
                }
            }
        }
    }

    Tk.Flex {
        visible: section.isConnector
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("CONNECTOR") }
        Tk.Caption {
            text: section.isConnector
                  ? qsTr("From %1 to %2").arg(section.item.fromGlued ? (section.item.fromTitle || qsTr("a shape")) : qsTr("a free end"))
                                         .arg(section.item.toGlued ? (section.item.toTitle || qsTr("a shape")) : qsTr("a free end"))
                  : ""
            wrapMode: Text.WordWrap
        }
        Tk.PropertyRow {
            label: qsTr("Routing")
            Tk.Segmented {
                objectName: "diagramRoutingSegments"
                stretch: true
                model: [{ text: qsTr("Right-angle"), value: "orthogonal" }, { text: qsTr("Straight"), value: "straight" },
                        { text: qsTr("Curved"), value: "curved" }]
                currentIndex: ["orthogonal", "straight", "curved"].indexOf(section.item.routing || "orthogonal")
                tooltip: qsTr("How the connector runs between its ends")
                onActivated: function(index) { section.diagram.setConnectorStyle({ routing: ["orthogonal", "straight", "curved"][index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Begin")
            Tk.ComboBox {
                objectName: "diagramStartArrow"
                model: section.arrowLabels
                currentIndex: section.arrowKinds.indexOf(section.item.startArrow || "none")
                tooltip: qsTr("Arrowhead at the start")
                onActivated: function(index) { section.diagram.setConnectorStyle({ startArrow: section.arrowKinds[index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("End")
            Tk.ComboBox {
                objectName: "diagramEndArrow"
                model: section.arrowLabels
                currentIndex: section.arrowKinds.indexOf(section.item.endArrow || "none")
                tooltip: qsTr("Arrowhead at the end")
                onActivated: function(index) { section.diagram.setConnectorStyle({ endArrow: section.arrowKinds[index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Label at")
            Tk.Slider {
                objectName: "diagramLabelPosition"
                from: 0
                to: 1
                stepSize: 0.05
                value: section.item.labelPosition === undefined ? 0.5 : section.item.labelPosition
                tooltip: qsTr("Where the label sits along the connector")
                onValueModified: function(value) { section.diagram.setConnectorStyle({ labelPosition: value }) }
            }
        }
    }
}
