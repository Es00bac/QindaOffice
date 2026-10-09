// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Format ▸ Borders and Shading (M70, D-079): Word's dialog for the
// selected paragraphs, or — in a table — the selected cells or the whole
// table (Apply to). Borders: a setting (None, Box, All, Grid, Custom) with
// the line style, colour and width, the edges toggled one by one, and a
// preview; Shading: the fill colour (theme-independent RGB, as Word writes
// w:shd w:fill). The host (write_borders.h) applies only the edges the user
// touched, so a mixed selection keeps the rest.
Tk.Dialog {
    id: dialog
    signal bordersApplied(var answer)
    signal customColorRequested(string what)
    property QtObject borders: null
    property string context: "paragraph"
    property var edges: ({})
    property var touched: ({})
    property bool hasInsideH: false
    property bool hasInsideV: false
    property string fill: ""
    property bool fillChanged: false
    property color penColor: "#000000"
    property bool penAuto: true
    readonly property var styleNames: borders ? borders.penStyleNames() : ["single"]
    readonly property var sizeValues: borders ? borders.penSizeValues() : [4]
    readonly property var applyChoices: context === "table" ? ["cell", "table"] : ["paragraph"]
    readonly property string applyTo: applyChoices[Math.max(0, Math.min(applyTo_.currentIndex, applyChoices.length - 1))]
    readonly property bool showInsideH: hasInsideH || applyTo === "table"
    readonly property bool showInsideV: context === "table" && (hasInsideV || applyTo === "table")
    objectName: "bordersShadingDialog"
    title: qsTr("Borders and Shading")
    dialogWidth: 520
    primaryText: qsTr("OK")

    function load(info) {
        context = info.context
        edges = info.edges
        touched = ({})
        hasInsideH = info.hasInsideH
        hasInsideV = info.hasInsideV
        fill = info.fill
        fillChanged = false
        applyTo_.currentIndex = Math.max(0, applyChoices.indexOf(info.applyTo))
        pages.currentIndex = 0
        if (borders) {
            style.currentIndex = borders.penStyleIndex
            width_.currentIndex = borders.penSizeIndex
        }
    }
    function pen() {
        return { style: styleNames[style.currentIndex], size: sizeValues[width_.currentIndex],
                 color: penAuto ? "auto" : String(penColor).substring(1).toUpperCase() }
    }
    function isOn(edge) { return edges[edge] !== undefined && edges[edge].style !== "nil" }
    function setEdge(edge, on) {
        const next = Object.assign({}, edges)
        next[edge] = on ? pen() : { style: "nil", size: 0, color: "auto" }
        edges = next
        const t = Object.assign({}, touched)
        t[edge] = true
        touched = t
    }
    // Word's settings: None clears every edge, Box draws the outside, All
    // and Grid the outside and inside (Grid with the pen outside and a
    // single ½ pt line inside).
    function applySetting(index) {
        const outside = ["top", "bottom", "left", "right"]
        const inside = (showInsideH ? ["insideH"] : []).concat(showInsideV ? ["insideV"] : [])
        for (const e of outside) setEdge(e, index !== 0)
        for (const e of inside) setEdge(e, index === 2 || index === 3)
        if (index === 3) {
            const thin = { style: "single", size: 4, color: pen().color }
            const next = Object.assign({}, edges)
            for (const e of inside) next[e] = thin
            edges = next
        }
    }
    onAccepted: {
        const sent = {}
        for (const key of Object.keys(touched)) sent[key] = edges[key]
        dialog.bordersApplied({ applyTo: applyTo, edges: sent, fill: fill, fillChanged: fillChanged })
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TabStrip {
            id: pages
            objectName: "bordersPages"
            model: [qsTr("Borders"), qsTr("Shading")]
        }
        Tk.Flex {
            visible: pages.currentIndex === 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Setting")
                Tk.Segmented {
                    id: setting
                    objectName: "bordersSetting"
                    model: [qsTr("None"), qsTr("Box"), qsTr("All"), qsTr("Grid"), qsTr("Custom")]
                    currentIndex: 4
                    onActivated: function(index) { if (index < 4) dialog.applySetting(index) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Style")
                Tk.ComboBox {
                    id: style
                    objectName: "bordersStyle"
                    model: dialog.borders ? dialog.borders.penStyles : []
                }
            }
            Tk.PropertyRow {
                label: qsTr("Colour / width")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ColorButton {
                        objectName: "bordersColor"
                        ghost: false
                        tooltip: qsTr("Border colour")
                        color: dialog.penColor
                        colors: ["#000000", "#7f7f7f", "#1f4e79", "#2f5496", "#4472c4", "#c00000", "#ed7d31", "#70ad47", "#ffc000", "#ffffff"]
                        onColorSelected: function(c) { dialog.penColor = c; dialog.penAuto = false }
                        onCustomRequested: dialog.customColorRequested("border")
                    }
                    Tk.ComboBox {
                        id: width_
                        objectName: "bordersWidth"
                        model: dialog.borders ? dialog.borders.penSizes : []
                    }
                }
            }
            Tk.Flex {
                gap: Tk.Theme.space.md
                align: Tk.Flex.Center
                // The preview: the edges as they will be drawn (paper is white, D-004).
                Rectangle {
                    id: preview
                    objectName: "bordersPreview"
                    implicitWidth: 140
                    implicitHeight: 100
                    color: "white"
                    Repeater {
                        model: [{ e: "top", h: true, at: 0.1 }, { e: "bottom", h: true, at: 0.9 }, { e: "insideH", h: true, at: 0.5 },
                                { e: "left", h: false, at: 0.1 }, { e: "right", h: false, at: 0.9 }, { e: "insideV", h: false, at: 0.5 }]
                        Rectangle {
                            readonly property var edge: dialog.edges[modelData.e]
                            readonly property real thickness: Math.max(1, edge ? edge.size / 6 : 1)
                            visible: dialog.isOn(modelData.e)
                            color: edge && edge.color !== "auto" ? "#" + edge.color : "black"
                            x: modelData.h ? preview.width * 0.1 : preview.width * modelData.at - thickness / 2
                            y: modelData.h ? preview.height * modelData.at - thickness / 2 : preview.height * 0.1
                            width: modelData.h ? preview.width * 0.8 : thickness
                            height: modelData.h ? thickness : preview.height * 0.8
                        }
                    }
                }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Repeater {
                        model: [{ e: "top", t: qsTr("Top") }, { e: "bottom", t: qsTr("Bottom") }, { e: "left", t: qsTr("Left") },
                                { e: "right", t: qsTr("Right") }, { e: "insideH", t: qsTr("Inside horizontal") },
                                { e: "insideV", t: qsTr("Inside vertical") }]
                        Tk.CheckBox {
                            objectName: "bordersEdge_" + modelData.e
                            visible: modelData.e === "insideH" ? dialog.showInsideH : modelData.e === "insideV" ? dialog.showInsideV : true
                            text: modelData.t
                            checked: dialog.isOn(modelData.e)
                            onClicked: { dialog.setEdge(modelData.e, checked); setting.currentIndex = 4 }
                        }
                    }
                }
            }
        }
        Tk.Flex {
            visible: pages.currentIndex === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Fill")
                Tk.Flex {
                    gap: Tk.Theme.space.sm
                    Tk.ColorButton {
                        objectName: "shadingFill"
                        ghost: false
                        tooltip: qsTr("Fill colour")
                        color: dialog.fill.length > 0 ? dialog.fill : "transparent"
                        colors: ["#d9e2f3", "#deeaf6", "#e2efd9", "#fff2cc", "#fbe4d5", "#ededed", "#1f4e79", "#c00000", "#ffffff"]
                        onColorSelected: function(c) { dialog.fill = String(c); dialog.fillChanged = true }
                        onCustomRequested: dialog.customColorRequested("fill")
                    }
                    Tk.Button {
                        objectName: "shadingNoColor"
                        text: qsTr("No Color")
                        onClicked: { dialog.fill = ""; dialog.fillChanged = true }
                    }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Apply to")
            Tk.ComboBox {
                id: applyTo_
                objectName: "bordersApplyTo"
                model: dialog.context === "table" ? [qsTr("Cell"), qsTr("Table")] : [qsTr("Paragraph")]
            }
        }
    }
}
