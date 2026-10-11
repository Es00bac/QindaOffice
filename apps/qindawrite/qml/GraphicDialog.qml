// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One editor for a selected native chart, SmartArt or drawing shape.
// Apply is explicit so validation errors stay beside the unchanged input.
Tk.Dialog {
    id: dialog
    required property QtObject graphics
    property var info: ({})
    property bool loading: false
    property var nodeTexts: []
    property bool typeChanged: false
    readonly property var presets: ["rect", "roundRect", "ellipse", "triangle", "diamond", "rightArrow", "line"]
    objectName: "graphicDialog"
    title: info.kind === "chart" ? qsTr("Edit Chart") : info.kind === "smartArt" ? qsTr("Edit SmartArt Text") : qsTr("Edit Shape")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.4
    primaryText: ""
    secondaryText: qsTr("Cancel")
    function load(value) {
        loading = true
        info = value
        nodeTexts = value.nodes || []
        dataText.text = value.kind === "chart" ? value.data : value.text
        widthField.value = value.width
        heightField.value = value.height
        plot.currentIndex = 0
        chartType.currentIndex = value.chartType || 0
        preset.currentIndex = Math.max(0, presets.indexOf(value.preset))
        fill.text = value.fill || ""
        stroke.text = value.stroke || ""
        outline.value = value.strokeWidth || 0
        typeChanged = false
        loading = false
    }
    function apply() {
        const answer = { width: widthField.value, height: heightField.value }
        if (info.kind === "chart") {
            answer.data = dataText.text
            if (typeChanged) { answer.chartType = chartType.currentIndex; answer.plot = plot.currentIndex }
        } else {
            if (info.kind === "smartArt") answer.nodes = nodeTexts
            else answer.text = dataText.text
            if (info.kind === "shape") {
                if (typeChanged) answer.preset = presets[preset.currentIndex]
                answer.fill = fill.text; answer.stroke = stroke.text; answer.strokeWidth = outline.value
            }
        }
        if (graphics.apply(answer)) close()
    }
    onAccepted: apply()
    Connections {
        target: dialog.graphics
        function onDialogRequested(info) { dialog.load(info); dialog.open() }
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption {
            width: parent.width
            wrapMode: Text.Wrap
            text: dialog.info.kind === "chart"
                ? qsTr("Edit the tab-separated table. The first row names the series; the first column holds categories. Paste rows from a spreadsheet. Blank values are gaps.")
                : dialog.info.kind === "smartArt" ? qsTr("Edit each existing SmartArt node. The diagram keeps its layout and style.") : qsTr("Edit the shape's text and appearance.")
        }
        Tk.PropertyRow {
            visible: dialog.info.kind === "chart" && (dialog.info.plotKinds || []).length > 1
            label: qsTr("Plot")
            Tk.ComboBox {
                id: plot
                objectName: "graphicPlot"
                model: (dialog.info.plotKinds || []).map(function(value, index) { return qsTr("Plot %1").arg(index + 1) })
                onActivated: function(index) { chartType.currentIndex = dialog.info.plotKinds[index]; dialog.typeChanged = false }
            }
        }
        Tk.PropertyRow {
            visible: dialog.info.kind === "chart"
            label: qsTr("Chart type")
            Tk.ComboBox {
                id: chartType
                objectName: "graphicChartType"
                model: dialog.info.chartTypes || []
                onActivated: dialog.typeChanged = true
            }
        }
        Tk.PropertyRow {
            visible: dialog.info.kind === "shape"
            label: qsTr("Shape")
            Tk.ComboBox {
                id: preset
                objectName: "graphicShapeType"
                model: [qsTr("Rectangle"), qsTr("Rounded rectangle"), qsTr("Ellipse"), qsTr("Triangle"), qsTr("Diamond"), qsTr("Right arrow"), qsTr("Line")]
                onActivated: dialog.typeChanged = true
            }
        }
        Tk.TextArea {
            id: dataText
            visible: dialog.info.kind !== "smartArt"
            objectName: "graphicText"
            width: parent.width
            rows: dialog.info.kind === "chart" ? 7 : 4
            wrapMode: dialog.info.kind === "chart" ? TextEdit.NoWrap : TextEdit.Wrap
            onTextChanged: if (!dialog.loading) dialog.graphics.clearError()
            Accessible.name: dialog.info.kind === "chart" ? qsTr("Chart data") : qsTr("Graphic text")
        }
        Tk.Flex {
            visible: dialog.info.kind === "smartArt"
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Repeater {
                model: dialog.nodeTexts.length
                Tk.TextArea {
                    required property int index
                    objectName: "graphicNode" + index
                    width: parent.width
                    rows: 2
                    text: dialog.nodeTexts[index] || ""
                    Accessible.name: qsTr("SmartArt node %1").arg(index + 1)
                    onTextChanged: if (!dialog.loading && index < dialog.nodeTexts.length && text !== dialog.nodeTexts[index]) {
                        const copy = dialog.nodeTexts.slice(); copy[index] = text; dialog.nodeTexts = copy
                        dialog.graphics.clearError()
                    }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Width (px)")
            Tk.NumberField { id: widthField; objectName: "graphicWidth"; from: 24; to: 2400; stepSize: 8; onValueModified: function(v) { value = v } }
        }
        Tk.PropertyRow {
            label: qsTr("Height (px)")
            Tk.NumberField { id: heightField; objectName: "graphicHeight"; from: 24; to: 2400; stepSize: 8; onValueModified: function(v) { value = v } }
        }
        Tk.PropertyRow {
            visible: dialog.info.kind === "shape"
            label: qsTr("Fill colour")
            Tk.TextField { id: fill; objectName: "graphicFill"; placeholderText: qsTr("#RRGGBB or transparent") }
        }
        Tk.PropertyRow {
            visible: dialog.info.kind === "shape"
            label: qsTr("Outline colour")
            Tk.TextField { id: stroke; objectName: "graphicStroke"; placeholderText: qsTr("#RRGGBB or transparent") }
        }
        Tk.PropertyRow {
            visible: dialog.info.kind === "shape"
            label: qsTr("Outline (pt)")
            Tk.NumberField { id: outline; objectName: "graphicOutline"; from: 0; to: 24; decimals: 1; stepSize: 0.5; onValueModified: function(v) { value = v } }
        }
        Tk.Caption {
            objectName: "graphicError"
            width: parent.width
            text: dialog.graphics.error
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: Tk.Theme.color.danger
        }
        Tk.Button { objectName: "graphicApply"; text: qsTr("Apply"); onClicked: dialog.apply() }
    }
}
